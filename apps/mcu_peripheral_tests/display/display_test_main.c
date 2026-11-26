/****************************************************************************
 * vendor/bouffalolab/apps/mcu_peripheral_tests/display/display_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/param.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/input/touchscreen.h>
#include <nuttx/lcd/lcd_dev.h>
#include <nuttx/video/fb.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DISPLAY_TEST_DEVPATH     "/dev/lcd0"
#define DISPLAY_TEST_FRAMES      30    /* Frames of the fill case */
#define DISPLAY_TEST_STRIP_ROWS  40    /* Rows per draw of the bars case */
#define DISPLAY_TEST_MARK        24    /* Corner marker size in pixels */
#define DISPLAY_TEST_TOUCHPATH   "/dev/input0"
#define DISPLAY_TEST_TARGET      10    /* Half size of a touch target */
#define DISPLAY_TEST_TOUCH_ERR   20    /* Allowed tap error, px (~3 mm) */
#define DISPLAY_TEST_TOUCH_WAIT  15000 /* Wait for a touch, ms */
#define DISPLAY_TEST_RACE_DRAWS  200   /* Draws per thread, concurrent */

#define RGB565_BLACK   0x0000
#define RGB565_BLUE    0x001f
#define RGB565_RED     0xf800
#define RGB565_GREEN   0x07e0
#define RGB565_CYAN    0x07ff
#define RGB565_MAGENTA 0xf81f
#define RGB565_YELLOW  0xffe0
#define RGB565_WHITE   0xffff
#define RGB565_GRAY    0x8410

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct display_test_s
{
  int fd;
  uint16_t xres;
  uint16_t yres;
};

struct display_race_s
{
  struct display_test_s *t;
  uint16_t color;
  int x;
  int ret;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int64_t display_now_us(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void display_fill16(uint16_t *buf, size_t npixels, uint16_t color)
{
  size_t i;

  for (i = 0; i < npixels; i++)
    {
      buf[i] = color;
    }
}

/* Draw w x h pixels at (x, y) from a buffer whose rows are stride bytes
 * apart.
 */

static int display_put(struct display_test_s *t, int x, int y, int w, int h,
                       const void *buf, int stride)
{
  struct lcddev_area_s area;

  area.row_start = y;
  area.row_end = y + h - 1;
  area.col_start = x;
  area.col_end = x + w - 1;
  area.stride = stride;
  area.data = (uint8_t *)buf;
  return ioctl(t->fd, LCDDEVIO_PUTAREA, (unsigned long)&area) < 0 ?
         -errno : OK;
}

/* Fill a rectangle with one color, from a buffer of one row */

static int display_rect(struct display_test_s *t, int x, int y, int w,
                        int h, uint16_t color)
{
  uint16_t *row = malloc(w * sizeof(uint16_t));
  int ret = OK;
  int i;

  if (row == NULL)
    {
      return -ENOMEM;
    }

  display_fill16(row, w, color);
  for (i = 0; i < h && ret == OK; i++)
    {
      ret = display_put(t, x, y + i, w, 1, row, w * sizeof(uint16_t));
    }

  free(row);
  return ret;
}

/* DISP-001: device information */

static int display_info(struct display_test_s *t)
{
  struct fb_videoinfo_s vinfo;
  struct lcd_planeinfo_s pinfo;
  struct lcddev_area_align_s align;
  int rate = 0;
  int set;
  int ret = OK;

  if (ioctl(t->fd, LCDDEVIO_GETVIDEOINFO, (unsigned long)&vinfo) < 0 ||
      ioctl(t->fd, LCDDEVIO_GETPLANEINFO, (unsigned long)&pinfo) < 0 ||
      ioctl(t->fd, LCDDEVIO_GETAREAALIGN, (unsigned long)&align) < 0)
    {
      ret = -errno;
    }
  else
    {
      /* A panel with a fixed refresh rate answers both with an error */

      ioctl(t->fd, LCDDEVIO_GETFRAMERATE, (unsigned long)&rate);
      set = ioctl(t->fd, LCDDEVIO_SETFRAMERATE, 60) < 0 ? -errno : OK;
      t->xres = vinfo.xres;
      t->yres = vinfo.yres;
      printf("DISP-001 info: %ux%u fmt=%u bpp=%u planes=%u align "
             "row=%u h=%u col=%u w=%u buf=%u framerate get=%d set=%d\n",
             vinfo.xres, vinfo.yres, vinfo.fmt, pinfo.bpp, vinfo.nplanes,
             align.row_start_align, align.height_align,
             align.col_start_align, align.width_align, align.buf_align,
             rate, set);
      if (vinfo.fmt != FB_FMT_RGB16_565 || pinfo.bpp != 16 ||
          vinfo.xres == 0 || vinfo.yres == 0)
        {
          ret = -ENOTSUP;
        }
    }

  printf("DISP-001 info %s ret=%d\n", ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}

/* DISP-002: eight vertical color bars, then corner markers: red top left,
 * green top right, blue bottom left, white bottom right.
 */

static int display_bars(struct display_test_s *t)
{
  static const uint16_t colors[8] =
  {
    RGB565_WHITE, RGB565_YELLOW, RGB565_CYAN, RGB565_GREEN,
    RGB565_MAGENTA, RGB565_RED, RGB565_BLUE, RGB565_BLACK
  };

  int rows = MIN(DISPLAY_TEST_STRIP_ROWS, t->yres);
  uint16_t *buf = malloc((size_t)t->xres * rows * sizeof(uint16_t));
  int ret = OK;
  int x;
  int y;

  if (buf == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  for (x = 0; x < t->xres; x++)
    {
      buf[x] = colors[x * 8 / t->xres];
    }

  for (y = 1; y < rows; y++)
    {
      memcpy(buf + y * t->xres, buf, t->xres * sizeof(uint16_t));
    }

  for (y = 0; y < t->yres && ret == OK; y += rows)
    {
      ret = display_put(t, 0, y, t->xres, MIN(rows, t->yres - y), buf,
                        t->xres * sizeof(uint16_t));
    }

  free(buf);
  if (ret == OK)
    {
      ret = display_rect(t, 0, 0, DISPLAY_TEST_MARK, DISPLAY_TEST_MARK,
                         RGB565_RED);
    }

  if (ret == OK)
    {
      ret = display_rect(t, t->xres - DISPLAY_TEST_MARK, 0,
                         DISPLAY_TEST_MARK, DISPLAY_TEST_MARK,
                         RGB565_GREEN);
    }

  if (ret == OK)
    {
      ret = display_rect(t, 0, t->yres - DISPLAY_TEST_MARK,
                         DISPLAY_TEST_MARK, DISPLAY_TEST_MARK, RGB565_BLUE);
    }

  if (ret == OK)
    {
      ret = display_rect(t, t->xres - DISPLAY_TEST_MARK,
                         t->yres - DISPLAY_TEST_MARK, DISPLAY_TEST_MARK,
                         DISPLAY_TEST_MARK, RGB565_WHITE);
    }

out:
  printf("DISP-002 bars: left to right white yellow cyan green magenta "
         "red blue black; corners red TL, green TR, blue BL, white BR "
         "%s ret=%d\n", ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}

/* DISP-003: full-screen fills in rows-high draws from one buffer, red,
 * green and blue in turn; reports the frame rate with and without the
 * time the CPU spends filling the buffer.
 */

static int display_fill(struct display_test_s *t, int rows)
{
  static const uint16_t colors[3] =
  {
    RGB565_RED, RGB565_GREEN, RGB565_BLUE
  };

  size_t npixels;
  uint16_t *buf;
  int64_t start;
  int64_t us = 0;
  int64_t draw_us = 0;
  int frame;
  int ret = OK;
  int y;

  rows = rows > 0 ? MIN(rows, t->yres) : t->yres;
  npixels = (size_t)t->xres * rows;
  buf = malloc(npixels * sizeof(uint16_t));
  if (buf == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  start = display_now_us();
  for (frame = 0; frame < DISPLAY_TEST_FRAMES && ret == OK; frame++)
    {
      int64_t draw_start;

      display_fill16(buf, npixels, colors[frame % 3]);
      draw_start = display_now_us();
      for (y = 0; y < t->yres && ret == OK; y += rows)
        {
          ret = display_put(t, 0, y, t->xres, MIN(rows, t->yres - y), buf,
                            t->xres * sizeof(uint16_t));
        }

      draw_us += display_now_us() - draw_start;
    }

  us = display_now_us() - start;
  free(buf);

out:
  printf("DISP-003 fill: %d frames, %d rows per draw, buffer %p, "
         "%lld us (draws %lld us), %lld fps, draws alone %lld fps %s "
         "ret=%d\n",
         DISPLAY_TEST_FRAMES, rows, buf, (long long)us, (long long)draw_us,
         us ? (long long)(DISPLAY_TEST_FRAMES * 1000000LL / us) : 0,
         draw_us ? (long long)(DISPLAY_TEST_FRAMES * 1000000LL / draw_us) :
                   0,
         ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}

/* DISP-004: areas on a gray background: an 8x8 red square at the top left
 * corner, a 33x21 green rectangle at (101, 57), a 20x20 blue square at
 * (40, 120) from a buffer two bytes off alignment, a 30x10 window at
 * (160, 200) cut out of a wider image (left half magenta, right half
 * cyan), and a 50-pixel yellow line at (10, 300) with PUTRUN. Nothing else
 * may change.
 */

static int display_area(struct display_test_s *t)
{
  struct lcddev_run_s run;
  uint16_t *buf;
  int ret;
  int i;

  /* Room for the widest piece: the 64 x 10 image of the window */

  buf = malloc(64 * 21 * sizeof(uint16_t) + 4);
  if (buf == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  ret = display_rect(t, 0, 0, t->xres, t->yres, RGB565_GRAY);
  if (ret == OK)
    {
      display_fill16(buf, 8 * 8, RGB565_RED);
      ret = display_put(t, 0, 0, 8, 8, buf, 8 * sizeof(uint16_t));
    }

  if (ret == OK)
    {
      display_fill16(buf, 33 * 21, RGB565_GREEN);
      ret = display_put(t, 101, 57, 33, 21, buf, 33 * sizeof(uint16_t));
    }

  if (ret == OK)
    {
      uint16_t *odd = (uint16_t *)((uint8_t *)buf + 2);

      display_fill16(odd, 20 * 20, RGB565_BLUE);
      ret = display_put(t, 40, 120, 20, 20, odd, 20 * sizeof(uint16_t));
    }

  if (ret == OK)
    {
      for (i = 0; i < 64 * 10; i++)
        {
          buf[i] = (i % 64) < 32 ? RGB565_MAGENTA : RGB565_CYAN;
        }

      /* Columns 17..46 of the image: 15 magenta, then 15 cyan */

      ret = display_put(t, 160, 200, 30, 10, buf + 17,
                        64 * sizeof(uint16_t));
    }

  if (ret == OK)
    {
      display_fill16(buf, 50, RGB565_YELLOW);
      run.row = 300;
      run.col = 10;
      run.data = (uint8_t *)buf;
      run.npixels = 50;
      ret = ioctl(t->fd, LCDDEVIO_PUTRUN, (unsigned long)&run) < 0 ?
            -errno : OK;
    }

  free(buf);

out:
  printf("DISP-004 area: gray, red 8x8 at (0,0), green 33x21 at (101,57), "
         "blue 20x20 at (40,120), magenta|cyan 30x10 at (160,200), "
         "yellow line 50 at (10,300) %s ret=%d\n",
         ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}

/* DISP-007: buffers of the exact length of the area, whose end is not a
 * whole 32-bit word: a white pixel at (10, 440) from 2 bytes, a 3-pixel
 * red run at (20, 440) from 6 bytes and a 5x3 green area at (30, 440) from
 * 30 bytes. The bus must not read past them.
 */

static int display_edge(struct display_test_s *t)
{
  /* Columns: x, width, height, color */

  const int pieces[3][4] =
  {
    { 10, 1, 1, RGB565_WHITE },
    { 20, 3, 1, RGB565_RED },
    { 30, 5, 3, RGB565_GREEN }
  };

  int ret = OK;
  int i;

  for (i = 0; i < 3 && ret == OK; i++)
    {
      size_t n = pieces[i][1] * pieces[i][2];
      uint16_t *buf = malloc(n * sizeof(uint16_t));

      if (buf == NULL)
        {
          ret = -ENOMEM;
          break;
        }

      display_fill16(buf, n, pieces[i][3]);
      ret = display_put(t, pieces[i][0], 440, pieces[i][1], pieces[i][2],
                        buf, pieces[i][1] * sizeof(uint16_t));
      free(buf);
    }

  printf("DISP-007 edge: white 1x1 at (10,440), red 3x1 at (20,440), "
         "green 5x3 at (30,440), exact-length buffers %s ret=%d\n",
         ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}

static void *display_race_thread(void *arg)
{
  struct display_race_s *r = arg;
  uint16_t *img = malloc(64 * 10 * sizeof(uint16_t));
  int i;

  r->ret = img == NULL ? -ENOMEM : OK;
  if (img != NULL)
    {
      display_fill16(img, 64 * 10, r->color);
      for (i = 0; i < DISPLAY_TEST_RACE_DRAWS && r->ret == OK; i++)
        {
          r->ret = display_put(r->t, r->x, 450, 30, 10, img + 17,
                               64 * sizeof(uint16_t));
        }
    }

  free(img);
  return NULL;
}

/* DISP-008: two threads draw a 30x10 window cut out of a wider image (so
 * through the driver's bounce buffer) DISPLAY_TEST_RACE_DRAWS times each,
 * magenta at (20, 450) and cyan at (200, 450). Each block must stay one
 * color.
 */

static int display_concurrent(struct display_test_s *t)
{
  struct display_race_s race[2];
  pthread_t thread[2];
  int ret = OK;
  int n = 0;

  race[0].t = t;
  race[0].color = RGB565_MAGENTA;
  race[0].x = 20;
  race[1].t = t;
  race[1].color = RGB565_CYAN;
  race[1].x = 200;

  while (n < 2 && ret == OK)
    {
      ret = -pthread_create(&thread[n], NULL, display_race_thread,
                            &race[n]);
      n += ret == OK;
    }

  while (n-- > 0)
    {
      pthread_join(thread[n], NULL);
      ret = ret == OK ? race[n].ret : ret;
    }

  printf("DISP-008 concurrent: 2 threads x %d strided draws, magenta at "
         "(20,450), cyan at (200,450) %s ret=%d\n", DISPLAY_TEST_RACE_DRAWS,
         ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}

#ifdef CONFIG_INPUT_TOUCHSCREEN
/* Wait for one touch sample; returns its flags, 0 on timeout or a negated
 * errno value.
 */

static int display_touch_read(int fd, int timeout_ms, int *x, int *y)
{
  struct touch_sample_s sample;
  struct pollfd pfd;
  int ret;

  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  ret = poll(&pfd, 1, timeout_ms);
  if (ret <= 0)
    {
      return ret < 0 ? -errno : 0;
    }

  ret = read(fd, &sample, sizeof(sample));
  if (ret < (int)sizeof(sample))
    {
      return ret < 0 ? -errno : -EIO;
    }

  *x = sample.point[0].x;
  *y = sample.point[0].y;
  printf("  touch %s%s%s x=%d y=%d\n",
         sample.point[0].flags & TOUCH_DOWN ? "DOWN" : "",
         sample.point[0].flags & TOUCH_MOVE ? "MOVE" : "",
         sample.point[0].flags & TOUCH_UP ? "UP" : "", *x, *y);
  return sample.point[0].flags;
}

/* One contact: wait for DOWN, follow MOVEs until UP. Returns the number of
 * MOVEs, -ETIMEDOUT or a negated errno value; *x0, *y0 is the DOWN position
 * and *x1, *y1 the last one.
 */

static int display_touch_contact(int fd, int *x0, int *y0, int *x1,
                                 int *y1)
{
  int moves = 0;
  int flags;

  do
    {
      flags = display_touch_read(fd, DISPLAY_TEST_TOUCH_WAIT, x0, y0);
      if (flags <= 0)
        {
          return flags < 0 ? flags : -ETIMEDOUT;
        }
    }
  while (!(flags & TOUCH_DOWN));

  *x1 = *x0;
  *y1 = *y0;
  do
    {
      flags = display_touch_read(fd, DISPLAY_TEST_TOUCH_WAIT, x1, y1);
      if (flags <= 0)
        {
          return flags < 0 ? flags : -ETIMEDOUT;
        }

      moves += (flags & TOUCH_MOVE) != 0;
    }
  while (!(flags & TOUCH_UP));

  return moves;
}

/* DISP-005: tap five targets (four corners and the center), each a white
 * cross on black; the lift position must be within DISPLAY_TEST_TOUCH_ERR
 * pixels of the target. All five are tapped before judging. Then drag from
 * the left edge to the right edge: at least 5 MOVEs and half the screen
 * width covered.
 */

static int display_touch(struct display_test_s *t)
{
  const int m = DISPLAY_TEST_TARGET * 2;
  const int targets[5][2] =
  {
    { m, m },
    { t->xres - 1 - m, m },
    { t->xres / 2, t->yres / 2 },
    { m, t->yres - 1 - m },
    { t->xres - 1 - m, t->yres - 1 - m }
  };

  int x0;
  int y0;
  int x1;
  int y1;
  int miss = 0;
  int fd;
  int i;
  int ret;

  fd = open(DISPLAY_TEST_TOUCHPATH, O_RDONLY);
  if (fd < 0)
    {
      ret = -errno;
      goto out;
    }

  ret = OK;
  for (i = 0; i < 5 && ret == OK; i++)
    {
      int x = targets[i][0];
      int y = targets[i][1];
      int err;

      ret = display_rect(t, 0, 0, t->xres, t->yres, RGB565_BLACK);
      if (ret == OK)
        {
          ret = display_rect(t, x - DISPLAY_TEST_TARGET, y,
                             DISPLAY_TEST_TARGET * 2 + 1, 1, RGB565_WHITE);
        }

      if (ret == OK)
        {
          ret = display_rect(t, x, y - DISPLAY_TEST_TARGET, 1,
                             DISPLAY_TEST_TARGET * 2 + 1, RGB565_WHITE);
        }

      if (ret != OK)
        {
          break;
        }

      printf("DISP-005 touch: tap target %d at (%d,%d)\n", i + 1, x, y);
      ret = display_touch_contact(fd, &x0, &y0, &x1, &y1);
      if (ret < 0)
        {
          break;
        }

      err = MAX(abs(x1 - x), abs(y1 - y));
      printf("DISP-005 target %d (%d,%d): down (%d,%d) up (%d,%d) "
             "error %d px %s\n", i + 1, x, y, x0, y0, x1, y1, err,
             err <= DISPLAY_TEST_TOUCH_ERR ? "PASS" : "FAIL");
      miss += err > DISPLAY_TEST_TOUCH_ERR;
    }

  if (ret == OK)
    {
      ret = display_rect(t, 0, t->yres / 2, t->xres, 1, RGB565_WHITE);
    }

  if (ret == OK)
    {
      printf("DISP-005 touch: drag along the line from left to right\n");
      ret = display_touch_contact(fd, &x0, &y0, &x1, &y1);
      if (ret >= 0)
        {
          printf("DISP-005 drag: %d moves from (%d,%d) to (%d,%d)\n",
                 ret, x0, y0, x1, y1);
          ret = ret >= 5 && x1 - x0 >= t->xres / 2 ? OK : -ERANGE;
        }
    }

  if (ret == OK && miss > 0)
    {
      ret = -ERANGE;
    }

  close(fd);

out:
  printf("DISP-005 touch %s ret=%d\n", ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}

/* DISP-006: no touch event for seconds while nobody touches the panel */

static int display_idle(int seconds)
{
  int64_t end = display_now_us() + (int64_t)seconds * 1000000;
  int events = 0;
  int x;
  int y;
  int fd;
  int ret = OK;

  fd = open(DISPLAY_TEST_TOUCHPATH, O_RDONLY);
  if (fd < 0)
    {
      ret = -errno;
      goto out;
    }

  printf("DISP-006 idle: do not touch the panel for %d s\n", seconds);
  while (ret == OK && display_now_us() < end)
    {
      int left = (int)((end - display_now_us()) / 1000);
      int flags = display_touch_read(fd, MAX(left, 1), &x, &y);

      if (flags < 0)
        {
          ret = flags;
        }
      else
        {
          events += flags > 0;
        }
    }

  close(fd);
  if (ret == OK && events > 0)
    {
      ret = -EIO;
    }

out:
  printf("DISP-006 idle: %d events in %d s %s ret=%d\n", events, seconds,
         ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}
#endif

static void display_usage(const char *progname)
{
  printf("Usage: %s [info|bars|fill|area|edge|concurrent|touch|idle|all] "
         "[-r rows] [-t seconds]\n", progname);
  printf("  -r rows     rows per draw of the fill case (default: full "
         "screen)\n");
  printf("  -t seconds  length of the idle case (default: 10)\n");
  printf("  all runs the drawing cases; touch and idle need a person\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  struct display_test_s t;
  const char *progname = argv[0];
  const char *test = "all";
  bool all;
  int failures = 0;
  int rows = 0;
  int seconds = 10;
  int opt;

  /* Options follow the optional test name */

  if (argc > 1 && argv[1][0] != '-')
    {
      test = argv[1];
      argc--;
      argv++;
    }

  while ((opt = getopt(argc, argv, "r:t:h")) != -1)
    {
      switch (opt)
        {
          case 'r':
            rows = atoi(optarg);
            break;

          case 't':
            seconds = atoi(optarg);
            break;

          default:
            display_usage(progname);
            return EXIT_FAILURE;
        }
    }

  all = strcmp(test, "all") == 0;
  if (optind < argc || rows < 0 || seconds <= 0 ||
      (!all && strcmp(test, "info") != 0 && strcmp(test, "bars") != 0 &&
       strcmp(test, "fill") != 0 && strcmp(test, "area") != 0 &&
       strcmp(test, "edge") != 0 && strcmp(test, "concurrent") != 0 &&
       strcmp(test, "touch") != 0 && strcmp(test, "idle") != 0))
    {
      display_usage(progname);
      return EXIT_FAILURE;
    }

  memset(&t, 0, sizeof(t));
  t.fd = open(DISPLAY_TEST_DEVPATH, O_RDWR);
  if (t.fd < 0)
    {
      printf("Failed to open %s: %d\n", DISPLAY_TEST_DEVPATH, -errno);
      return EXIT_FAILURE;
    }

  /* Every case needs the resolution */

  if (display_info(&t) < 0)
    {
      close(t.fd);
      return EXIT_FAILURE;
    }

  if (all || strcmp(test, "bars") == 0)
    {
      failures += display_bars(&t) < 0;
    }

  if (all || strcmp(test, "fill") == 0)
    {
      failures += display_fill(&t, rows) < 0;
    }

  if (all || strcmp(test, "area") == 0)
    {
      failures += display_area(&t) < 0;
    }

  if (all || strcmp(test, "edge") == 0)
    {
      failures += display_edge(&t) < 0;
    }

  if (all || strcmp(test, "concurrent") == 0)
    {
      failures += display_concurrent(&t) < 0;
    }

  if (strcmp(test, "touch") == 0)
    {
#ifdef CONFIG_INPUT_TOUCHSCREEN
      failures += display_touch(&t) < 0;
#else
      printf("DISP-005 touch FAIL: no touchscreen support\n");
      failures++;
#endif
    }

  if (strcmp(test, "idle") == 0)
    {
#ifdef CONFIG_INPUT_TOUCHSCREEN
      failures += display_idle(seconds) < 0;
#else
      printf("DISP-006 idle FAIL: no touchscreen support\n");
      failures++;
#endif
    }

  close(t.fd);
  printf("DISPLAY summary: %s failed=%d\n", failures == 0 ? "PASS" : "FAIL",
         failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
