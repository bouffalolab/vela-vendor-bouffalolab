/****************************************************************************
 * vendor/bouffalolab/apps/mcu_peripheral_tests/i2s/i2s_test_main.c
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
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define I2S_TEST_PLAYBACK        "/dev/audio/pcm0"
#define I2S_TEST_CAPTURE         "/dev/audio/pcm_in0"
#define I2S_TEST_MQNAME          "/mcu_i2s_test"
#define I2S_TEST_MAX_BUFFERS     8
#define I2S_TEST_FRAME_BYTES     4          /* 16-bit stereo */
#define I2S_TEST_AMPLITUDE       16384.0f   /* -6 dBFS */
#define I2S_TEST_DEFAULT_RATE    48000
#define I2S_TEST_DEFAULT_FREQ    1000
#define I2S_TEST_DEFAULT_MS      3000
#define I2S_TEST_RATES_MS        1000
#define I2S_TEST_RESTART_COUNT   5
#define I2S_TEST_WINDOW_MS       250

/* A restart stream has the same number of frames at every rate (300 ms
 * at 48 kHz), so that it spans the same number of buffers. Playback is
 * timed from the first returned buffer and leaves out the last two: a
 * stream of three buffers cannot be timed, and with four the rate rests
 * on one buffer, where the jitter of the return times alone can exceed
 * the tolerance.
 */

#define I2S_TEST_RESTART_FRAMES  14400

/* The board microphone is on LIN2, which the codec records as the left
 * channel; the right channel (RIN2) is the microphone ground reference.
 */

#define I2S_TEST_MIC_CHANNEL     0

/* A corrupted sample shows up as an isolated spike: a magnitude of at
 * least 24576 between two neighbours below 4096, which the codec filters
 * cannot produce from a real signal. A sign bit taken from the previous
 * sample, or the codec sending one BCLK late, produces many of them.
 */

#define I2S_TEST_SPIKE_PEAK      24576
#define I2S_TEST_SPIKE_NEIGHBOR  4096

/* The codec ADC output swings for about 32000 frames after the codec
 * starts (0.7 s at 48 kHz, 2.7 s at 12 kHz); recording drops the first
 * 36000 frames.
 */

#define I2S_TEST_SETTLE_FRAMES   36000

/* Speech from the board microphone is far below full scale. Playback of a
 * recording raises its peak to -6 dBFS, by at most 40 dB so that a silent
 * recording stays quiet.
 */

#define I2S_TEST_PLAYBACK_PEAK   16384
#define I2S_TEST_PLAYBACK_GAIN   100

/* A buffer must come back within this time after the previous one */

#define I2S_TEST_STALL_MS        2000

/* The chip has no audio PLL: rates of the 44.1 kHz family run 1.2 % fast.
 * Anything further off means a wrong clock ratio or lost buffers.
 */

#define I2S_TEST_TOLERANCE_PPM   20000

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* What to play: a sine tone, or recorded frames when pcm is set */

struct i2s_source_s
{
  const int16_t *pcm;
  uint32_t total;      /* Frames to play */
  uint32_t frames;     /* Frames handed out so far */
  float phase;
  float step;
  int only;            /* Tone on channel 0 (left) or 1 (right) only, or -1 */
};

/* Buffers come back in the order they were queued, and the upper half
 * clears nbytes of a returned playback buffer, so the frame count of each
 * queued buffer is kept here.
 *
 * The rate leaves out the end of the stream: once the ES8388 driver has
 * its last buffer back it resets the codec over polled I2C at a higher
 * priority than this task, which delays the last returns by several
 * milliseconds. Playback skips the final buffer and the one before it
 * (the final one may be short); recording skips the buffers returned
 * after AUDIOIOC_STOP.
 */

struct i2s_stats_s
{
  uint32_t queued_frames[I2S_TEST_MAX_BUFFERS];
  unsigned int queue_head;
  unsigned int queue_tail;
  unsigned int final_index;
  unsigned int dequeued;
  uint64_t timed_frames; /* Frames in the timed buffers after the first */
  uint64_t first_ns;
  uint64_t last_ns;
};

struct i2s_stream_s
{
  struct ap_buffer_s *buffers[I2S_TEST_MAX_BUFFERS];
  unsigned int nbuffers;
  mqd_t mq;
  int fd;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t i2s_now_ns(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

/* Fill a playback buffer with the next frames of the source and mark the
 * last one. A tone has the same signal on both channels. Returns the
 * number of frames written.
 */

static uint32_t i2s_source_fill(struct i2s_source_s *src,
                                struct ap_buffer_s *apb)
{
  int16_t *samples = (int16_t *)apb->samp;
  uint32_t frames = apb->nmaxbytes / I2S_TEST_FRAME_BYTES;
  uint32_t i;

  if (frames > src->total - src->frames)
    {
      frames = src->total - src->frames;
    }

  if (src->pcm != NULL)
    {
      memcpy(samples, src->pcm + 2 * src->frames,
             frames * I2S_TEST_FRAME_BYTES);
    }
  else
    {
      for (i = 0; i < frames; i++)
        {
          int16_t value = (int16_t)(I2S_TEST_AMPLITUDE * sinf(src->phase));

          samples[2 * i] = src->only == 1 ? 0 : value;
          samples[2 * i + 1] = src->only == 0 ? 0 : value;
          src->phase += src->step;
          if (src->phase >= 2.0f * (float)M_PI)
            {
              src->phase -= 2.0f * (float)M_PI;
            }
        }
    }

  src->frames += frames;
  apb->nbytes = frames * I2S_TEST_FRAME_BYTES;
  apb->curbyte = 0;
  if (src->frames >= src->total)
    {
      apb->flags |= AUDIO_APB_FINAL;
    }

  return frames;
}

static int i2s_enqueue(int fd, struct ap_buffer_s *apb,
                       struct i2s_stats_s *stats)
{
  struct audio_buf_desc_s desc;

  if ((apb->flags & AUDIO_APB_FINAL) != 0)
    {
      stats->final_index = stats->queue_head;
    }

  stats->queued_frames[stats->queue_head++ % I2S_TEST_MAX_BUFFERS] =
    apb->nbytes / I2S_TEST_FRAME_BYTES;

  memset(&desc, 0, sizeof(desc));
  desc.numbytes = apb->nbytes;
  desc.u.buffer = apb;
  return ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&desc) < 0 ?
         -errno : OK;
}

/* Record the return time of one buffer; 'timed' says whether it counts
 * towards the rate.
 */

static void i2s_returned(struct i2s_stats_s *stats, uint32_t frames,
                         bool timed)
{
  uint64_t now = i2s_now_ns();

  if (stats->dequeued == 0)
    {
      stats->first_ns = now;
      stats->last_ns = now;
    }
  else if (timed)
    {
      stats->timed_frames += frames;
      stats->last_ns = now;
    }

  stats->dequeued++;
}

static void i2s_stream_close(struct i2s_stream_s *stream)
{
  struct audio_buf_desc_s desc;
  unsigned int i;

  for (i = 0; i < stream->nbuffers; i++)
    {
      memset(&desc, 0, sizeof(desc));
      desc.u.buffer = stream->buffers[i];
      ioctl(stream->fd, AUDIOIOC_FREEBUFFER, (unsigned long)&desc);
    }

  if (stream->mq != (mqd_t)-1)
    {
      ioctl(stream->fd, AUDIOIOC_UNREGISTERMQ, (unsigned long)stream->mq);
      mq_close(stream->mq);
      mq_unlink(I2S_TEST_MQNAME);
    }

  ioctl(stream->fd, AUDIOIOC_RELEASE, 0);
  close(stream->fd);
}

/* Configure a reserved audio device for stereo at 'rate' with 'bits' per
 * sample.
 */

static int i2s_configure(int fd, uint8_t type, uint32_t rate, uint8_t bits)
{
  struct audio_caps_desc_s caps;

  memset(&caps, 0, sizeof(caps));
  caps.caps.ac_len = sizeof(struct audio_caps_s);
  caps.caps.ac_type = type;
  caps.caps.ac_subtype = AUDIO_FMT_PCM;
  caps.caps.ac_channels = 2;
  caps.caps.ac_controls.hw[0] = rate;
  caps.caps.ac_controls.b[2] = bits;
  caps.caps.ac_controls.b[3] = rate >> 16;
  return ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)&caps) < 0 ?
         -errno : OK;
}

/* Open an audio device for 16-bit stereo at 'rate', optionally set the
 * output volume (playback only, volume >= 0) and allocate its buffers.
 */

static int i2s_stream_open(struct i2s_stream_s *stream, const char *path,
                           uint8_t type, uint32_t rate, int volume)
{
  struct audio_caps_desc_s caps;
  struct ap_buffer_info_s info;
  struct audio_buf_desc_s desc;
  struct mq_attr attr;
  int ret;

  memset(stream, 0, sizeof(*stream));
  stream->mq = (mqd_t)-1;
  stream->fd = open(path, O_RDWR);
  if (stream->fd < 0)
    {
      return -errno;
    }

  if (ioctl(stream->fd, AUDIOIOC_RESERVE, 0) < 0)
    {
      ret = -errno;
      close(stream->fd);
      return ret;
    }

  attr.mq_maxmsg = I2S_TEST_MAX_BUFFERS + 8;
  attr.mq_msgsize = sizeof(struct audio_msg_s);
  attr.mq_curmsgs = 0;
  attr.mq_flags = 0;
  stream->mq = mq_open(I2S_TEST_MQNAME, O_RDWR | O_CREAT, 0644, &attr);
  if (stream->mq == (mqd_t)-1)
    {
      ret = -errno;
      goto errout;
    }

  if (ioctl(stream->fd, AUDIOIOC_REGISTERMQ, (unsigned long)stream->mq) < 0)
    {
      ret = -errno;
      goto errout;
    }

  ret = i2s_configure(stream->fd, type, rate, 16);
  if (ret < 0)
    {
      goto errout;
    }

  if (volume >= 0)
    {
      memset(&caps, 0, sizeof(caps));
      caps.caps.ac_len = sizeof(struct audio_caps_s);
      caps.caps.ac_type = AUDIO_TYPE_FEATURE;
      caps.caps.ac_format.hw = AUDIO_FU_VOLUME;
      caps.caps.ac_controls.hw[0] = volume;
      if (ioctl(stream->fd, AUDIOIOC_CONFIGURE, (unsigned long)&caps) < 0)
        {
          ret = -errno;
          goto errout;
        }
    }

  if (ioctl(stream->fd, AUDIOIOC_GETBUFFERINFO, (unsigned long)&info) < 0)
    {
      ret = -errno;
      goto errout;
    }

  while (stream->nbuffers < info.nbuffers &&
         stream->nbuffers < I2S_TEST_MAX_BUFFERS)
    {
      memset(&desc, 0, sizeof(desc));
      desc.numbytes = info.buffer_size;
      desc.u.pbuffer = &stream->buffers[stream->nbuffers];
      if (ioctl(stream->fd, AUDIOIOC_ALLOCBUFFER, (unsigned long)&desc) < 0)
        {
          ret = -errno;
          goto errout;
        }

      stream->nbuffers++;
    }

  return OK;

errout:
  i2s_stream_close(stream);
  return ret;
}

static int i2s_wait(struct i2s_stream_s *stream, struct audio_msg_s *msg)
{
  struct timespec deadline;

  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += I2S_TEST_STALL_MS / 1000;
  return mq_timedreceive(stream->mq, (char *)msg, sizeof(*msg), NULL,
                         &deadline) < 0 ? -errno : OK;
}

/* Play the source through the playback device and collect the buffer
 * return times.
 */

static int i2s_play(struct i2s_source_s *src, uint32_t rate, int volume,
                    struct i2s_stats_s *stats)
{
  struct i2s_stream_s stream;
  struct audio_msg_s msg;
  bool final_queued = false;
  unsigned int i;
  int ret;

  memset(stats, 0, sizeof(*stats));
  stats->final_index = UINT_MAX;

  ret = i2s_stream_open(&stream, I2S_TEST_PLAYBACK, AUDIO_TYPE_OUTPUT,
                        rate, volume);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < stream.nbuffers && !final_queued; i++)
    {
      if (i2s_source_fill(src, stream.buffers[i]) == 0)
        {
          break;
        }

      final_queued = (stream.buffers[i]->flags & AUDIO_APB_FINAL) != 0;
      ret = i2s_enqueue(stream.fd, stream.buffers[i], stats);
      if (ret < 0)
        {
          goto out;
        }
    }

  if (ioctl(stream.fd, AUDIOIOC_START, 0) < 0)
    {
      ret = -errno;
      goto out;
    }

  for (; ; )
    {
      ret = i2s_wait(&stream, &msg);
      if (ret < 0 || msg.msg_id == AUDIO_MSG_COMPLETE)
        {
          break;
        }

      if (msg.msg_id == AUDIO_MSG_DEQUEUE)
        {
          struct ap_buffer_s *apb = msg.u.ptr;
          unsigned int index = stats->queue_tail++;

          i2s_returned(stats,
                       stats->queued_frames[index % I2S_TEST_MAX_BUFFERS],
                       stats->final_index == UINT_MAX ||
                       index + 1 < stats->final_index);

          if (!final_queued && i2s_source_fill(src, apb) > 0)
            {
              final_queued = (apb->flags & AUDIO_APB_FINAL) != 0;
              ret = i2s_enqueue(stream.fd, apb, stats);
              if (ret < 0)
                {
                  break;
                }
            }
        }
    }

  ioctl(stream.fd, AUDIOIOC_STOP, 0);

out:
  i2s_stream_close(&stream);
  return ret;
}

/* An empty or partial-frame FINAL buffer must follow the preceding data,
 * be returned exactly once, and end the stream without an explicit STOP.
 */

static int i2s_final(uint32_t rate, int volume, unsigned int data_buffers,
                      unsigned int final_bytes)
{
  struct i2s_stream_s stream;
  struct i2s_stats_s stats;
  struct audio_msg_s msg;
  unsigned int count = data_buffers + 1;
  unsigned int returned = 0;
  unsigned int i;
  bool complete = false;
  bool opened = false;
  int ret;

  memset(&stats, 0, sizeof(stats));
  ret = i2s_stream_open(&stream, I2S_TEST_PLAYBACK, AUDIO_TYPE_OUTPUT,
                        rate, volume);
  if (ret < 0)
    {
      goto report;
    }

  opened = true;
  if (count > stream.nbuffers ||
      final_bytes > stream.buffers[count - 1]->nmaxbytes)
    {
      ret = -ENOBUFS;
      goto report;
    }

  for (i = 0; i < count; i++)
    {
      struct ap_buffer_s *apb = stream.buffers[i];

      memset(apb->samp, 0, apb->nmaxbytes);
      apb->curbyte = 0;
      apb->nbytes = i < data_buffers ? apb->nmaxbytes : final_bytes;
      apb->flags &= ~AUDIO_APB_FINAL;
      if (i == data_buffers)
        {
          apb->flags |= AUDIO_APB_FINAL;
        }

      ret = i2s_enqueue(stream.fd, apb, &stats);
      if (ret < 0)
        {
          goto report;
        }
    }

  if (ioctl(stream.fd, AUDIOIOC_START, 0) < 0)
    {
      ret = -errno;
      goto report;
    }

  while ((ret = i2s_wait(&stream, &msg)) == OK)
    {
      if (msg.msg_id == AUDIO_MSG_DEQUEUE)
        {
          if (returned >= count || msg.u.ptr != stream.buffers[returned])
            {
              ret = -EIO;
              break;
            }

          returned++;
        }
      else if (msg.msg_id == AUDIO_MSG_COMPLETE)
        {
          complete = true;
          ret = returned == count ? OK : -EIO;
          break;
        }
    }

  /* Report before STOP: a stream that never completes leaves a buffer in
   * flight, and the codec driver's STOP then waits for it forever.
   */

report:
  printf("[I2S-005] final data=%u bytes=%u returned=%u/%u complete=%d"
         " %s ret=%d\n", data_buffers, final_bytes, returned, count,
         complete, ret == OK ? "PASS" : "FAIL", ret);
  if (opened)
    {
      ioctl(stream.fd, AUDIOIOC_STOP, 0);
      i2s_stream_close(&stream);
    }

  return ret;
}

/* Record 'frames' 16-bit stereo frames from the capture device into pcm
 * after dropping the first 'skip' frames, and collect the buffer return
 * times.
 */

static int i2s_record(uint32_t rate, int16_t *pcm, uint32_t frames,
                      uint32_t skip, struct i2s_stats_s *stats)
{
  struct i2s_stream_s stream;
  struct audio_msg_s msg;
  uint32_t recorded = 0;
  bool stopping = false;
  unsigned int i;
  int ret;

  memset(stats, 0, sizeof(*stats));
  stats->final_index = UINT_MAX;

  ret = i2s_stream_open(&stream, I2S_TEST_CAPTURE, AUDIO_TYPE_INPUT, rate,
                        -1);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < stream.nbuffers; i++)
    {
      stream.buffers[i]->nbytes = stream.buffers[i]->nmaxbytes;
      ret = i2s_enqueue(stream.fd, stream.buffers[i], stats);
      if (ret < 0)
        {
          goto out;
        }
    }

  if (ioctl(stream.fd, AUDIOIOC_START, 0) < 0)
    {
      ret = -errno;
      goto out;
    }

  for (; ; )
    {
      ret = i2s_wait(&stream, &msg);
      if (ret < 0 || msg.msg_id == AUDIO_MSG_COMPLETE)
        {
          break;
        }

      if (msg.msg_id == AUDIO_MSG_DEQUEUE && !stopping)
        {
          struct ap_buffer_s *apb = msg.u.ptr;
          uint32_t got = apb->nbytes / I2S_TEST_FRAME_BYTES;
          uint32_t drop = MIN(got, skip);
          uint32_t keep = MIN(got - drop, frames - recorded);

          i2s_returned(stats, got, true);
          memcpy(pcm + 2 * recorded,
                 apb->samp + drop * I2S_TEST_FRAME_BYTES,
                 keep * I2S_TEST_FRAME_BYTES);
          skip -= drop;
          recorded += keep;
          if (drop > 0 && skip == 0)
            {
              printf("  speak now\n");
            }

          if (recorded < frames)
            {
              apb->nbytes = apb->nmaxbytes;
              ret = i2s_enqueue(stream.fd, apb, stats);
              if (ret < 0)
                {
                  break;
                }
            }
          else
            {
              ioctl(stream.fd, AUDIOIOC_STOP, 0);
              stopping = true;
            }
        }
    }

  if (!stopping)
    {
      ioctl(stream.fd, AUDIOIOC_STOP, 0);
    }

  if (ret == OK && recorded < frames)
    {
      ret = -ENODATA;
    }

out:
  i2s_stream_close(&stream);
  return ret;
}

/* Check a finished stream: it completed and its frame rate matches the
 * request. Prints one result line.
 */

static int i2s_report(const char *name, const char *what, uint32_t rate,
                      uint32_t ms, const struct i2s_stats_s *stats, int ret)
{
  uint64_t elapsed = stats->last_ns - stats->first_ns;
  uint32_t measured = 0;
  int32_t ppm = 0;

  if (ret == OK && stats->timed_frames > 0 && elapsed > 0)
    {
      measured = (uint32_t)(stats->timed_frames * 1000000000ull / elapsed);
      ppm = (int32_t)(((int64_t)measured - rate) * 1000000 / rate);
      if (ppm > I2S_TEST_TOLERANCE_PPM || ppm < -I2S_TEST_TOLERANCE_PPM)
        {
          ret = -ERANGE;
        }
    }
  else if (ret == OK)
    {
      ret = -ENODATA;
    }

  printf("[%s] %s rate=%" PRIu32 " ms=%" PRIu32 " buffers=%u measured=%"
         PRIu32 " ppm=%" PRId32 " %s ret=%d\n", name, what, rate, ms,
         stats->dequeued, measured, ppm, ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}

static int i2s_tone(const char *name, uint32_t rate, uint32_t freq,
                    uint32_t ms, int volume, int only)
{
  struct i2s_source_s src;
  struct i2s_stats_s stats;
  char what[24];
  int ret;

  memset(&src, 0, sizeof(src));
  src.total = (uint32_t)((uint64_t)rate * ms / 1000);
  src.step = 2.0f * (float)M_PI * freq / rate;
  src.only = only;
  ret = i2s_play(&src, rate, volume, &stats);
  snprintf(what, sizeof(what), "tone=%" PRIu32 "Hz%s", freq,
           only < 0 ? "" : only == 0 ? "/left" : "/right");
  return i2s_report(name, what, rate, ms, &stats, ret);
}

/* Print the peak, RMS, DC offset and spike count of each channel and the
 * microphone RMS per window, then copy the microphone channel to both so
 * that playback reaches the speaker (it plays the right channel only).
 * Returns the microphone peak and the number of spikes.
 */

static uint32_t i2s_analyze(int16_t *pcm, uint32_t frames, uint32_t rate,
                            int *micpeak)
{
  uint32_t window = rate * I2S_TEST_WINDOW_MS / 1000;
  double sum[2] =
  {
    0
  };

  double dc[2] =
  {
    0
  };

  int peak[2] =
  {
    0
  };

  uint32_t spikes[2] =
  {
    0
  };

  uint32_t i;
  int ch;

  for (i = 0; i < frames; i++)
    {
      for (ch = 0; ch < 2; ch++)
        {
          int value = pcm[2 * i + ch];

          dc[ch] += value;
          sum[ch] += (double)value * value;
          peak[ch] = MAX(peak[ch], abs(value));
          if (i > 0 && i + 1 < frames && abs(value) >= I2S_TEST_SPIKE_PEAK &&
              abs(pcm[2 * (i - 1) + ch]) < I2S_TEST_SPIKE_NEIGHBOR &&
              abs(pcm[2 * (i + 1) + ch]) < I2S_TEST_SPIKE_NEIGHBOR)
            {
              spikes[ch]++;
            }
        }
    }

  for (ch = 0; ch < 2; ch++)
    {
      printf("  %s peak=%d rms=%d dc=%d spikes=%" PRIu32 "\n",
             ch ? "right" : "left", peak[ch], (int)sqrt(sum[ch] / frames),
             (int)(dc[ch] / frames), spikes[ch]);
    }

  printf("  mic rms per %d ms:", I2S_TEST_WINDOW_MS);

  for (i = 0; i + window <= frames; i += window)
    {
      double wsum = 0;
      uint32_t j;

      for (j = i; j < i + window; j++)
        {
          int value = pcm[2 * j + I2S_TEST_MIC_CHANNEL];

          wsum += (double)value * value;
        }

      printf(" %d", (int)sqrt(wsum / window));
    }

  printf("\n");

  for (i = 0; i < frames; i++)
    {
      pcm[2 * i + !I2S_TEST_MIC_CHANNEL] = pcm[2 * i + I2S_TEST_MIC_CHANNEL];
    }

  *micpeak = peak[I2S_TEST_MIC_CHANNEL];
  return spikes[0] + spikes[1];
}

/* Scale a recording with microphone peak 'peak' for playback */

static void i2s_amplify(int16_t *pcm, uint32_t frames, int peak)
{
  int gain = peak > 0 ? I2S_TEST_PLAYBACK_PEAK / peak : INT_MAX;
  uint32_t i;

  gain = MAX(MIN(gain, I2S_TEST_PLAYBACK_GAIN), 1);
  printf("  playback gain=%d\n", gain);
  for (i = 0; i < 2 * frames; i++)
    {
      pcm[i] = (int16_t)(pcm[i] * gain);
    }
}

static int i2s_capture(const char *name, uint32_t rate, uint32_t ms,
                       int volume, bool playback)
{
  struct i2s_source_s src;
  struct i2s_stats_s stats;
  uint32_t frames = (uint32_t)((uint64_t)rate * ms / 1000);
  int16_t *pcm;
  int ret;

  memset(&stats, 0, sizeof(stats));
  pcm = malloc((size_t)frames * I2S_TEST_FRAME_BYTES);
  if (pcm == NULL)
    {
      return i2s_report(name, "record", rate, ms, &stats, -ENOMEM);
    }

  printf("  recording %" PRIu32 " ms from %s after %" PRIu32
         " ms for the codec to settle\n", ms, I2S_TEST_CAPTURE,
         (uint32_t)((uint64_t)I2S_TEST_SETTLE_FRAMES * 1000 / rate));
  ret = i2s_record(rate, pcm, frames, I2S_TEST_SETTLE_FRAMES, &stats);
  ret = i2s_report(name, "record", rate, ms, &stats, ret);
  if (ret == OK)
    {
      uint32_t spikes;
      int peak;

      spikes = i2s_analyze(pcm, frames, rate, &peak);
      if (spikes > 0)
        {
          ret = -EIO;
        }

      printf("[%s] record data spikes=%" PRIu32 " %s ret=%d\n", name,
             spikes, ret == OK ? "PASS" : "FAIL", ret);
      if (ret == OK && playback)
        {
          i2s_amplify(pcm, frames, peak);
          memset(&src, 0, sizeof(src));
          src.pcm = pcm;
          src.total = frames;
          ret = i2s_play(&src, rate, volume, &stats);
          ret = i2s_report(name, "playback", rate, ms, &stats, ret);
        }
    }

  free(pcm);
  return ret;
}

/* The ES8388 driver accepts 18 to 32-bit samples, but the BL616CL I2S
 * driver carries 16-bit samples only. AUDIOIOC_CONFIGURE must fail with
 * EINVAL for the others; succeeding would leave the codec and the I2S on
 * different widths, and the samples would be misread.
 */

static int i2s_width(const char *path, uint8_t type, uint32_t rate,
                     uint8_t bits)
{
  int ret;
  int fd;

  fd = open(path, O_RDWR);
  if (fd < 0)
    {
      ret = -errno;
    }
  else if (ioctl(fd, AUDIOIOC_RESERVE, 0) < 0)
    {
      ret = -errno;
      close(fd);
    }
  else
    {
      ret = i2s_configure(fd, type, rate, bits);
      ret = ret == -EINVAL ? OK : ret == OK ? -EPROTO : ret;
      ioctl(fd, AUDIOIOC_RELEASE, 0);
      close(fd);
    }

  printf("[I2S-006] width=%u %s rejected %s ret=%d\n", bits, path,
         ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}

static void i2s_usage(const char *progname)
{
  printf("Usage: %s [tone|rates|restart|record|final|width|all] [-r rate]"
         " [-f freq] [-t ms] [-v volume] [-c left|right] [-p]\n", progname);
  printf("  tone     play one tone (default %d Hz, %d Hz tone, %d ms)\n",
         I2S_TEST_DEFAULT_RATE, I2S_TEST_DEFAULT_FREQ,
         I2S_TEST_DEFAULT_MS);
  printf("  rates    %d ms at each ES8388 rate, 11025..48000 Hz\n",
         I2S_TEST_RATES_MS);
  printf("  restart  %d back-to-back streams of %d frames"
         " (%d ms at 48 kHz)\n", I2S_TEST_RESTART_COUNT,
         I2S_TEST_RESTART_FRAMES, I2S_TEST_RESTART_FRAMES / 48);
  printf("  record   record from the microphone and print levels;"
         " -p plays it back\n");
  printf("  final    check empty FINAL buffers and incomplete frames\n");
  printf("  width    check that 24 and 32-bit samples are rejected\n");
  printf("  all      tone, rates, restart, record, final and width"
         " (default)\n");
  printf("  -v       output volume 0..1000 (default: driver setting)\n");
  printf("  -c       play tones on one channel only\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  static const uint32_t rates[] =
  {
    11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
  };

  const char *progname = argv[0];
  const char *test = "all";
  uint32_t rate = I2S_TEST_DEFAULT_RATE;
  uint32_t freq = I2S_TEST_DEFAULT_FREQ;
  uint32_t ms = I2S_TEST_DEFAULT_MS;
  bool playback = false;
  bool all;
  int volume = -1;
  int only = -1;
  int failures = 0;
  unsigned int i;
  int opt;

  /* Options follow the optional test name; parse them as their own
   * argument vector.
   */

  if (argc > 1 && argv[1][0] != '-')
    {
      test = argv[1];
      argc--;
      argv++;
    }

  while ((opt = getopt(argc, argv, "r:f:t:v:c:ph")) != -1)
    {
      switch (opt)
        {
          case 'r':
            rate = strtoul(optarg, NULL, 0);
            break;

          case 'f':
            freq = strtoul(optarg, NULL, 0);
            break;

          case 't':
            ms = strtoul(optarg, NULL, 0);
            break;

          case 'v':
            volume = atoi(optarg);
            break;

          case 'c':
            only = strcmp(optarg, "left") == 0 ? 0 :
                   strcmp(optarg, "right") == 0 ? 1 : 2;
            break;

          case 'p':
            playback = true;
            break;

          default:
            i2s_usage(progname);
            return EXIT_FAILURE;
        }
    }

  /* getopt() stops at the first non-option, so a test name given after
   * the options would otherwise be ignored and all tests run.
   */

  if (optind < argc)
    {
      printf("Unexpected argument: %s (the test name goes first)\n",
             argv[optind]);
      i2s_usage(progname);
      return EXIT_FAILURE;
    }

  all = strcmp(test, "all") == 0;
  if ((!all && strcmp(test, "tone") != 0 && strcmp(test, "rates") != 0 &&
       strcmp(test, "restart") != 0 && strcmp(test, "record") != 0 &&
       strcmp(test, "final") != 0 && strcmp(test, "width") != 0) ||
      rate == 0 || rate > UINT16_MAX || freq == 0 || freq >= rate / 2 ||
      ms == 0 || volume > 1000 || only > 1)
    {
      i2s_usage(progname);
      return EXIT_FAILURE;
    }

  if (all || strcmp(test, "tone") == 0)
    {
      failures += i2s_tone("I2S-001", rate, freq, ms, volume, only) < 0;
    }

  if (all || strcmp(test, "rates") == 0)
    {
      for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++)
        {
          failures += i2s_tone("I2S-002", rates[i], freq,
                               I2S_TEST_RATES_MS, volume, only) < 0;
        }
    }

  if (all || strcmp(test, "restart") == 0)
    {
      for (i = 0; i < I2S_TEST_RESTART_COUNT; i++)
        {
          failures += i2s_tone("I2S-003", rate, freq,
                               I2S_TEST_RESTART_FRAMES * 1000 / rate,
                               volume, only) < 0;
        }
    }

  if (all || strcmp(test, "record") == 0)
    {
      failures += i2s_capture("I2S-004", rate, ms, volume, playback) < 0;
    }

  if (all || strcmp(test, "final") == 0)
    {
      failures += i2s_final(rate, volume, 0, 0) < 0;
      failures += i2s_final(rate, volume, 2, 0) < 0;
      failures += i2s_final(rate, volume, 2, I2S_TEST_FRAME_BYTES - 1) < 0;
      failures += i2s_final(rate, volume, 2, I2S_TEST_FRAME_BYTES + 1) < 0;
    }

  if (all || strcmp(test, "width") == 0)
    {
      failures += i2s_width(I2S_TEST_PLAYBACK, AUDIO_TYPE_OUTPUT, rate,
                            24) < 0;
      failures += i2s_width(I2S_TEST_PLAYBACK, AUDIO_TYPE_OUTPUT, rate,
                            32) < 0;
      failures += i2s_width(I2S_TEST_CAPTURE, AUDIO_TYPE_INPUT, rate,
                            24) < 0;
      failures += i2s_width(I2S_TEST_CAPTURE, AUDIO_TYPE_INPUT, rate,
                            32) < 0;
    }

  printf("I2S summary: %s failed=%d\n", failures == 0 ? "PASS" : "FAIL",
         failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
