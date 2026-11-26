/****************************************************************************
 * vendor/bouffalolab/components/display/nuttx/display_lcddev.c
 *
 * NuttX LCD device (struct lcd_dev_s) on top of a GRAM display of the
 * display framework.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <string.h>
#include <sys/param.h>

#include <nuttx/kmalloc.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/mutex.h>
#include <nuttx/video/fb.h>

#include "display_lcddev.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Size of the bounce buffer for pixels that cannot be sent in place: at
 * least one row of the display.
 */

#define DISPLAY_LCDDEV_BOUNCE_BYTES 4096

/* The bus DMA reads whole 32-bit words: a piece sent in place must be a
 * whole number of words, and the bounce buffer has a word of slack.
 */

#define DISPLAY_LCDDEV_WORD         sizeof(uint32_t)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct display_lcddev_s
{
  struct lcd_dev_s dev;     /* NuttX LCD device, must be first */
  display_t *display;       /* Display of the framework */
  display_info_t info;      /* Display information */
  uint8_t fmt;              /* FB_FMT_* */
  uint8_t bpp;              /* Bits per pixel */
  uint8_t power;            /* 0 .. CONFIG_LCD_MAXPOWER */
  uint8_t *bounce;          /* Bounce buffer */
  size_t bounce_bytes;      /* Usable size of the bounce buffer */
  mutex_t lock;             /* Bounce buffer, from copy to completion */
  uint8_t *runbuf;          /* One-row buffer for plane users */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: display_lcddev_rows
 *
 * Description:
 *   Number of rows of the next piece of an area: at most max_rows and the
 *   rows left, a multiple of the panel's row alignment unless it is the
 *   last piece.
 *
 ****************************************************************************/

static size_t display_lcddev_rows(const struct display_lcddev_s *priv,
                                  size_t left, size_t max_rows)
{
  size_t align = MAX(priv->info.y_align, 1);

  if (left <= max_rows)
    {
      return left;
    }

  return max_rows - max_rows % align;
}

static int display_lcddev_putarea(struct lcd_dev_s *dev,
                                  fb_coord_t row_start, fb_coord_t row_end,
                                  fb_coord_t col_start, fb_coord_t col_end,
                                  const uint8_t *buffer, fb_coord_t stride)
{
  struct display_lcddev_s *priv = (struct display_lcddev_s *)dev;
  size_t align = MAX(priv->info.buf_align, 1);
  size_t word = MAX(align, DISPLAY_LCDDEV_WORD);
  size_t row_bytes;
  size_t max_rows;
  size_t left;
  size_t rows;
  size_t i;
  display_area_t area;
  int ret = OK;

  if (row_start > row_end || col_start > col_end ||
      row_end >= priv->info.height || col_end >= priv->info.width)
    {
      return -EINVAL;
    }

  area.x = col_start;
  area.y = row_start;
  area.w = col_end - col_start + 1;
  row_bytes = (size_t)area.w * (priv->bpp >> 3);
  left = row_end - row_start + 1;

  /* Contiguous rows at an aligned address go to the bus as they are, in
   * pieces of at most max_draw_bytes that are whole words, so that the
   * next piece starts aligned and the bus reads nothing past the end.
   */

  if (stride == row_bytes && ((uintptr_t)buffer % align) == 0)
    {
      max_rows = priv->info.max_draw_bytes / row_bytes;
      if (max_rows < left)
        {
          while (max_rows > 0 && (max_rows * row_bytes) % word != 0)
            {
              max_rows--;
            }
        }

      while (max_rows > 0 && left > 0 && ret >= 0)
        {
          rows = display_lcddev_rows(priv, left, max_rows);
          if ((rows * row_bytes) % word != 0)
            {
              break;
            }

          area.h = rows;
          ret = display_draw(priv->display, &area, buffer,
                             rows * row_bytes);
          buffer += rows * row_bytes;
          area.y += rows;
          left -= rows;
        }

      if (left == 0 || ret < 0)
        {
          return ret;
        }
    }

  /* Anything else is copied row by row into the bounce buffer, which stays
   * locked until the bus has read it.
   */

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  max_rows = priv->bounce_bytes / row_bytes;
  while (left > 0 && ret >= 0)
    {
      rows = display_lcddev_rows(priv, left, max_rows);
      for (i = 0; i < rows; i++)
        {
          memcpy(priv->bounce + i * row_bytes, buffer, row_bytes);
          buffer += stride;
        }

      area.h = rows;
      ret = display_draw(priv->display, &area, priv->bounce,
                         rows * row_bytes);
      area.y += rows;
      left -= rows;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

static int display_lcddev_putrun(struct lcd_dev_s *dev, fb_coord_t row,
                                 fb_coord_t col, const uint8_t *buffer,
                                 size_t npixels)
{
  struct display_lcddev_s *priv = (struct display_lcddev_s *)dev;

  if (npixels == 0)
    {
      return -EINVAL;
    }

  return display_lcddev_putarea(dev, row, row, col, col + npixels - 1,
                                buffer, npixels * (priv->bpp >> 3));
}

/* The panel has no read path; lcd_dev emulates getarea with getrun */

static int display_lcddev_getrun(struct lcd_dev_s *dev, fb_coord_t row,
                                 fb_coord_t col, uint8_t *buffer,
                                 size_t npixels)
{
  return -ENOTSUP;
}

static int display_lcddev_getvideoinfo(struct lcd_dev_s *dev,
                                       struct fb_videoinfo_s *vinfo)
{
  struct display_lcddev_s *priv = (struct display_lcddev_s *)dev;

  memset(vinfo, 0, sizeof(*vinfo));
  vinfo->fmt = priv->fmt;
  vinfo->xres = priv->info.width;
  vinfo->yres = priv->info.height;
  vinfo->nplanes = 1;
  return OK;
}

static int display_lcddev_getplaneinfo(struct lcd_dev_s *dev,
                                       unsigned int planeno,
                                       struct lcd_planeinfo_s *pinfo)
{
  struct display_lcddev_s *priv = (struct display_lcddev_s *)dev;

  if (planeno != 0)
    {
      return -EINVAL;
    }

  memset(pinfo, 0, sizeof(*pinfo));
  pinfo->putrun = display_lcddev_putrun;
  pinfo->putarea = display_lcddev_putarea;
  pinfo->getrun = display_lcddev_getrun;
  pinfo->buffer = priv->runbuf;
  pinfo->bpp = priv->bpp;
  pinfo->dev = dev;
  return OK;
}

static int display_lcddev_getareaalign(struct lcd_dev_s *dev,
                                       struct lcddev_area_align_s *align)
{
  struct display_lcddev_s *priv = (struct display_lcddev_s *)dev;

  align->row_start_align = MAX(priv->info.y_align, 1);
  align->height_align = MAX(priv->info.y_align, 1);
  align->col_start_align = MAX(priv->info.x_align, 1);
  align->width_align = MAX(priv->info.x_align, 1);
  align->buf_align = MAX(priv->info.buf_align, 1);
  return OK;
}

static int display_lcddev_getpower(struct lcd_dev_s *dev)
{
  return ((struct display_lcddev_s *)dev)->power;
}

/* Power is the backlight level of the board */

static int display_lcddev_setpower(struct lcd_dev_s *dev, int power)
{
  struct display_lcddev_s *priv = (struct display_lcddev_s *)dev;
  int ret;

  if (power < 0 || power > CONFIG_LCD_MAXPOWER)
    {
      return -EINVAL;
    }

  ret = display_set_backlight(priv->display,
                              power * DISPLAY_BACKLIGHT_MAX /
                              CONFIG_LCD_MAXPOWER);
  if (ret >= 0)
    {
      priv->power = power;
    }

  return ret;
}

static int display_lcddev_getcontrast(struct lcd_dev_s *dev)
{
  return -ENOTSUP;
}

static int display_lcddev_setcontrast(struct lcd_dev_s *dev,
                                      unsigned int contrast)
{
  return -ENOTSUP;
}

/* The refresh rate is fixed by the panel */

static int display_lcddev_setframerate(struct lcd_dev_s *dev, int rate)
{
  return -ENOTSUP;
}

static int display_lcddev_getframerate(struct lcd_dev_s *dev)
{
  return -ENOTSUP;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

struct lcd_dev_s *display_lcddev_initialize(display_t *display)
{
  struct display_lcddev_s *priv;
  display_info_t info;
  size_t row_bytes;
  uint8_t fmt;
  uint8_t bpp;

  if (display_get_info(display, &info) < 0 ||
      info.kind != DISPLAY_KIND_GRAM)
    {
      return NULL;
    }

  switch (info.format)
    {
      case DISPLAY_FMT_RGB565:
        fmt = FB_FMT_RGB16_565;
        bpp = 16;
        break;

      case DISPLAY_FMT_RGB888:
        fmt = FB_FMT_RGB24;
        bpp = 24;
        break;

      case DISPLAY_FMT_XRGB8888:
        fmt = FB_FMT_RGB32;
        bpp = 32;
        break;

      default:
        return NULL;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return NULL;
    }

  row_bytes = (size_t)info.width * (bpp >> 3);
  priv->bounce_bytes = MAX(DISPLAY_LCDDEV_BOUNCE_BYTES / row_bytes, 1) *
                       row_bytes;
  priv->bounce = kmm_memalign(MAX(info.buf_align, sizeof(uintptr_t)),
                              priv->bounce_bytes + DISPLAY_LCDDEV_WORD);
  priv->runbuf = kmm_memalign(MAX(info.buf_align, sizeof(uintptr_t)),
                              row_bytes);
  if (priv->bounce == NULL || priv->runbuf == NULL)
    {
      kmm_free(priv->bounce);
      kmm_free(priv->runbuf);
      kmm_free(priv);
      return NULL;
    }

  nxmutex_init(&priv->lock);

  priv->dev.getvideoinfo = display_lcddev_getvideoinfo;
  priv->dev.getplaneinfo = display_lcddev_getplaneinfo;
  priv->dev.getpower = display_lcddev_getpower;
  priv->dev.setpower = display_lcddev_setpower;
  priv->dev.getcontrast = display_lcddev_getcontrast;
  priv->dev.setcontrast = display_lcddev_setcontrast;
  priv->dev.setframerate = display_lcddev_setframerate;
  priv->dev.getframerate = display_lcddev_getframerate;
  priv->dev.getareaalign = display_lcddev_getareaalign;
  priv->display = display;
  priv->info = info;
  priv->fmt = fmt;
  priv->bpp = bpp;
  return &priv->dev;
}
