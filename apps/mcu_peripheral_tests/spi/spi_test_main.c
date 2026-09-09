/****************************************************************************
 * apps/vendor/bouffalolab/apps/mcu_peripheral_tests/spi/spi_test_main.c
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
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>

#include "bl616cl_spi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SPI_TEST_DEFAULT_FREQUENCY 400000
#define SPI_TEST_DEFAULT_LENGTH    32
#define SPI_TEST_MAX_LENGTH        4096
#define SPI_TEST_FAKE_BUFFER_SIZE  128
#define SPI_TEST_TARGET0           SPIDEV_USER(0)
#define SPI_TEST_TARGET1           SPIDEV_USER(1)
#define SPI_TEST_UNKNOWN_TARGET    SPIDEV_USER(UINT16_MAX)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct spi_test_config_s
{
  int bus;
  uint32_t frequency;
  uint32_t devid;
  size_t length;
  uint8_t mode;
  uint8_t nbits;
  bool lsbfirst;
};

struct spi_fake_state_s
{
  pthread_mutex_t lock;
  struct spi_fake_tracker_s *tracker;
  enum bl616cl_spi_test_feature_e last_feature;
  uint32_t last_value;
  uint32_t feature_count;
  uint32_t exchange_count;
  uint32_t recover_count;
  uint32_t select_count;
  uint32_t deselect_count;
  uint32_t rejected_select_count;
  uint32_t active;
  uint32_t max_active;
  uint32_t mode_exchange_mask;
  uint32_t bits_exchange_mask;
  uint32_t order_exchange_mask;
  uint32_t frequency_exchange_mask;
  uint32_t current_frequency;
  uint32_t current_mode;
  uint32_t current_bits;
  uint32_t current_order;
  uint32_t last_select_devid;
  uint32_t last_deselect_devid;
  size_t last_nbytes;
  useconds_t exchange_delay;
  int feature_result;
  int exchange_result;
};

struct spi_fake_tracker_s
{
  pthread_mutex_t lock;
  uint32_t active;
  uint32_t max_active;
};

struct spi_fake_snapshot_s
{
  uint32_t feature_count;
  uint32_t exchange_count;
  uint32_t recover_count;
  uint32_t select_count;
  uint32_t deselect_count;
  uint32_t rejected_select_count;
  uint32_t max_active;
  uint32_t mode_exchange_mask;
  uint32_t bits_exchange_mask;
  uint32_t order_exchange_mask;
  uint32_t frequency_exchange_mask;
  uint32_t last_select_devid;
  uint32_t last_deselect_devid;
  size_t last_nbytes;
};

struct spi_fake_worker_s
{
  struct spi_dev_s *spi;
  sem_t *start;
  unsigned int id;
  uint8_t mode;
  uint8_t nbits;
  uint32_t frequency;
  int result;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void spi_test_usage(const char *progname)
{
  printf("Usage:\n");
  printf("  %s fake <lifecycle|config|exchange|sequence|errors|\n"
         "         concurrent|dual|all> [--bus N]\n",
         progname);
  printf("  %s hw <loopback|boundary|paths|sequence> [options]\n", progname);
  printf("Options:\n");
  printf("  --bus N       SPI bus (default: 0)\n");
  printf("  --freq HZ     Requested frequency (default: 400000)\n");
  printf("  --mode N      SPI mode 0..3 (default: 0)\n");
  printf("  --bits N      Word width 8 or 16 (default: 8)\n");
  printf("  --length N    Word count 1..4096 (default: 32)\n");
  printf("  --target N    SPIDEV_USER index (default: board setting)\n");
  printf("  --lsb         Transfer least-significant bit first\n");
}

static int spi_test_parse_u32(const char *text, uint32_t *value)
{
  char *end;
  unsigned long parsed;

  errno = 0;
  parsed = strtoul(text, &end, 0);
  if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX)
    {
      return -EINVAL;
    }

  *value = (uint32_t)parsed;
  return OK;
}

static int spi_test_parse_options(int argc, char *argv[],
                                  struct spi_test_config_s *cfg)
{
  uint32_t value;
  int i;

  memset(cfg, 0, sizeof(*cfg));
  cfg->bus = 0;
  cfg->frequency = SPI_TEST_DEFAULT_FREQUENCY;
  cfg->devid = SPI_TEST_TARGET0;
  cfg->length = SPI_TEST_DEFAULT_LENGTH;
  cfg->nbits = 8;

  for (i = 3; i < argc; i++)
    {
      if (strcmp(argv[i], "--lsb") == 0)
        {
          cfg->lsbfirst = true;
          continue;
        }

      if (i + 1 >= argc || spi_test_parse_u32(argv[i + 1], &value) < 0)
        {
          printf("SPI_TEST option FAIL name=%s\n", argv[i]);
          return -EINVAL;
        }

      if (strcmp(argv[i], "--bus") == 0 && value <= INT_MAX)
        {
          cfg->bus = (int)value;
        }
      else if (strcmp(argv[i], "--freq") == 0)
        {
          cfg->frequency = value;
        }
      else if (strcmp(argv[i], "--mode") == 0 && value <= UINT8_MAX)
        {
          cfg->mode = (uint8_t)value;
        }
      else if (strcmp(argv[i], "--bits") == 0 && value <= UINT8_MAX)
        {
          cfg->nbits = (uint8_t)value;
        }
      else if (strcmp(argv[i], "--length") == 0)
        {
          cfg->length = value;
        }
      else if (strcmp(argv[i], "--target") == 0 && value <= UINT16_MAX)
        {
          cfg->devid = SPIDEV_USER(value);
        }
      else
        {
          printf("SPI_TEST option FAIL unsupported=%s\n", argv[i]);
          return -EINVAL;
        }

      i++;
    }

  if (cfg->bus < 0 || cfg->frequency == 0 || cfg->mode > 3 ||
      (cfg->nbits != 8 && cfg->nbits != 16) || cfg->length == 0 ||
      cfg->length > SPI_TEST_MAX_LENGTH)
    {
      printf("SPI_TEST option FAIL invalid range\n");
      return -EINVAL;
    }

  return OK;
}

static void spi_fake_snapshot(struct spi_fake_state_s *state,
                              struct spi_fake_snapshot_s *snapshot)
{
  pthread_mutex_lock(&state->lock);
  snapshot->feature_count = state->feature_count;
  snapshot->exchange_count = state->exchange_count;
  snapshot->recover_count = state->recover_count;
  snapshot->select_count = state->select_count;
  snapshot->deselect_count = state->deselect_count;
  snapshot->rejected_select_count = state->rejected_select_count;
  snapshot->max_active = state->max_active;
  snapshot->mode_exchange_mask = state->mode_exchange_mask;
  snapshot->bits_exchange_mask = state->bits_exchange_mask;
  snapshot->order_exchange_mask = state->order_exchange_mask;
  snapshot->frequency_exchange_mask = state->frequency_exchange_mask;
  snapshot->last_select_devid = state->last_select_devid;
  snapshot->last_deselect_devid = state->last_deselect_devid;
  snapshot->last_nbytes = state->last_nbytes;
  pthread_mutex_unlock(&state->lock);
}

static void spi_fake_reset_observations(struct spi_fake_state_s *state)
{
  pthread_mutex_lock(&state->lock);
  state->feature_count = 0;
  state->exchange_count = 0;
  state->recover_count = 0;
  state->select_count = 0;
  state->deselect_count = 0;
  state->rejected_select_count = 0;
  state->active = 0;
  state->max_active = 0;
  state->mode_exchange_mask = 0;
  state->bits_exchange_mask = 0;
  state->order_exchange_mask = 0;
  state->frequency_exchange_mask = 0;
  state->last_select_devid = 0;
  state->last_deselect_devid = 0;
  state->last_nbytes = 0;
  pthread_mutex_unlock(&state->lock);
}

static void spi_fake_report(const char *name, int bus, int ret,
                            struct spi_fake_state_s *state)
{
  struct spi_fake_snapshot_s snapshot;

  spi_fake_snapshot(state, &snapshot);
  printf("SPI_TEST fake %s bus=%d result=%s features=%" PRIu32
         " exchanges=%" PRIu32 " recoveries=%" PRIu32 "\n",
         name, bus, ret == OK ? "PASS" : "FAIL", snapshot.feature_count,
         snapshot.exchange_count, snapshot.recover_count);
}

static void spi_fake_set_feature_result(struct spi_fake_state_s *state,
                                        int result)
{
  pthread_mutex_lock(&state->lock);
  state->feature_result = result;
  pthread_mutex_unlock(&state->lock);
}

static bool spi_fake_known_target(uint32_t devid)
{
  return devid == SPI_TEST_TARGET0 || devid == SPI_TEST_TARGET1;
}

static bool spi_fake_select(void *arg, uint32_t devid, bool selected)
{
  struct spi_fake_state_s *state = arg;
  bool accepted;

  pthread_mutex_lock(&state->lock);
  accepted = spi_fake_known_target(devid);
  if (!accepted && selected)
    {
      state->rejected_select_count++;
    }
  else if (selected)
    {
      state->select_count++;
      state->last_select_devid = devid;
    }
  else
    {
      state->deselect_count++;
      state->last_deselect_devid = devid;
    }

  pthread_mutex_unlock(&state->lock);
  return accepted;
}

static int spi_fake_feature(void *arg,
                            enum bl616cl_spi_test_feature_e feature,
                            uint32_t value)
{
  struct spi_fake_state_s *state = arg;
  int ret;

  pthread_mutex_lock(&state->lock);
  state->last_feature = feature;
  state->last_value = value;
  state->feature_count++;
  switch (feature)
    {
      case BL616CL_SPI_TEST_FREQUENCY:
        state->current_frequency = value;
        break;
      case BL616CL_SPI_TEST_MODE:
        state->current_mode = value;
        break;
      case BL616CL_SPI_TEST_BITS:
        state->current_bits = value;
        break;
      case BL616CL_SPI_TEST_BITORDER:
        state->current_order = value;
        break;
      default:
        break;
    }

  ret = state->feature_result;
  pthread_mutex_unlock(&state->lock);
  return ret;
}

static int spi_fake_exchange(void *arg, const void *txbuffer, void *rxbuffer,
                             size_t nbytes)
{
  struct spi_fake_state_s *state = arg;
  struct spi_fake_tracker_s *tracker;
  useconds_t delay;
  int ret;

  pthread_mutex_lock(&state->lock);
  state->exchange_count++;
  state->last_nbytes = nbytes;
  state->mode_exchange_mask |= 1 << state->current_mode;
  state->bits_exchange_mask |= state->current_bits == 16 ? 2 : 1;
  state->order_exchange_mask |= state->current_order ? 2 : 1;
  state->frequency_exchange_mask |=
    state->current_frequency >= 1000000 ? 2 : 1;
  state->active++;
  if (state->active > state->max_active)
    {
      state->max_active = state->active;
    }

  delay = state->exchange_delay;
  ret = state->exchange_result;
  tracker = state->tracker;
  pthread_mutex_unlock(&state->lock);

  if (tracker != NULL)
    {
      pthread_mutex_lock(&tracker->lock);
      tracker->active++;
      if (tracker->active > tracker->max_active)
        {
          tracker->max_active = tracker->active;
        }

      pthread_mutex_unlock(&tracker->lock);
    }

  if (delay > 0)
    {
      usleep(delay);
    }

  if (ret >= 0 && rxbuffer != NULL)
    {
      if (txbuffer != NULL)
        {
          memcpy(rxbuffer, txbuffer, nbytes);
        }
      else
        {
          memset(rxbuffer, 0xff, nbytes);
        }
    }

  if (tracker != NULL)
    {
      pthread_mutex_lock(&tracker->lock);
      tracker->active--;
      pthread_mutex_unlock(&tracker->lock);
    }

  pthread_mutex_lock(&state->lock);
  state->active--;
  pthread_mutex_unlock(&state->lock);
  return ret;
}

static void spi_fake_recover(void *arg)
{
  struct spi_fake_state_s *state = arg;

  pthread_mutex_lock(&state->lock);
  state->recover_count++;
  pthread_mutex_unlock(&state->lock);
}

static const struct bl616cl_spi_test_ops_s g_spi_fake_ops =
{
  .select = spi_fake_select,
  .feature = spi_fake_feature,
  .exchange = spi_fake_exchange,
  .recover = spi_fake_recover,
};

static int spi_fake_lifecycle(int bus, struct spi_dev_s *spi,
                              struct spi_fake_state_s *state,
                              bool test_owned)
{
  struct bl616cl_spi_test_diag_s diag;
  int ret;

  if (spi == NULL || bl616cl_spi_test_device(bus) != spi)
    {
      return -EIO;
    }

  if (bl616cl_spi_test_initialize(-1) != -ENODEV ||
      bl616cl_spi_test_initialize(2) != -ENODEV ||
      bl616cl_spibus_uninitialize(NULL) != -EINVAL)
    {
      return -EIO;
    }

  ret = test_owned ? bl616cl_spi_test_initialize(bus) :
                     bl616cl_spi_test_addref(bus);
  if (ret < 0)
    {
      return ret;
    }

  if (bl616cl_spi_test_device(bus) != spi)
    {
      (void)bl616cl_spibus_uninitialize(spi);
      return -EIO;
    }

  if (bl616cl_spibus_uninitialize(spi) < 0 ||
      bl616cl_spi_test_device(bus) != spi)
    {
      return -EIO;
    }

  ret = bl616cl_spi_test_get_diag(bus, &diag);
  if (ret < 0 || diag.nbits != 8 || diag.mode != SPIDEV_MODE0)
    {
      return -EIO;
    }

  ret = bl616cl_spi_test_install(bus, NULL, NULL);
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cl_spi_test_install(bus, &g_spi_fake_ops, state);
  return ret;
}

static int spi_fake_config(int bus, struct spi_dev_s *spi,
                           struct spi_fake_state_s *state)
{
  struct bl616cl_spi_test_diag_s diag;
  struct spi_fake_snapshot_s snapshot;
  struct spi_fake_snapshot_s after;
  unsigned int mode;
  uint32_t actual;

  for (mode = 0; mode < 4; mode++)
    {
      SPI_SETMODE(spi, (enum spi_mode_e)mode);
      SPI_EXCHANGE(spi, NULL, NULL, 1);
    }

  SPI_SETBITS(spi, 8);
  SPI_EXCHANGE(spi, NULL, NULL, 1);
  SPI_SETBITS(spi, 16);
  SPI_EXCHANGE(spi, NULL, NULL, 1);

  actual = SPI_SETFREQUENCY(spi, 100000);
  if (actual == 0)
    {
      return -EIO;
    }

  SPI_EXCHANGE(spi, NULL, NULL, 1);
  actual = SPI_SETFREQUENCY(spi, 1000000);
  if (actual == 0)
    {
      return -EIO;
    }

  SPI_EXCHANGE(spi, NULL, NULL, 1);
  actual = SPI_SETFREQUENCY(spi, 1100000);
  if (actual == 0 || actual > 1100000)
    {
      return -EIO;
    }

  SPI_EXCHANGE(spi, NULL, NULL, 1);
  if (SPI_SETFREQUENCY(spi, 1) != 0 ||
      bl616cl_spi_test_get_diag(bus, &diag) < 0 ||
      diag.last_error != -EIO)
    {
      return -EIO;
    }

  actual = SPI_SETFREQUENCY(spi, UINT32_MAX);
  if (actual == 0 || bl616cl_spi_test_get_diag(bus, &diag) < 0 ||
      diag.actual_frequency != actual)
    {
      return -EIO;
    }

  if (SPI_SETFREQUENCY(spi, 0) != 0 ||
      bl616cl_spi_test_get_diag(bus, &diag) < 0 ||
      diag.last_error != -EINVAL)
    {
      return -EIO;
    }

  if (SPI_SETFREQUENCY(spi, SPI_TEST_DEFAULT_FREQUENCY) == 0)
    {
      return -EIO;
    }

  spi_fake_set_feature_result(state, -EIO);
  spi_fake_snapshot(state, &snapshot);
  SPI_SETMODE(spi, SPIDEV_MODE1);
  SPI_EXCHANGE(spi, NULL, NULL, 1);
  if (bl616cl_spi_test_get_diag(bus, &diag) < 0 ||
      diag.last_error != -EIO)
    {
      return -EIO;
    }

  spi_fake_snapshot(state, &after);
  if (after.exchange_count != snapshot.exchange_count)
    {
      return -EIO;
    }

  spi_fake_set_feature_result(state, OK);
  SPI_SETMODE(spi, SPIDEV_MODE1);
#ifdef CONFIG_SPI_BITORDER
  if (SPI_HWFEATURES(spi, HWFEAT_LSBFIRST) < 0)
    {
      return -EIO;
    }

  SPI_EXCHANGE(spi, NULL, NULL, 1);
  if (SPI_HWFEATURES(spi, 0) < 0)
    {
      return -EIO;
    }

  SPI_EXCHANGE(spi, NULL, NULL, 1);
#endif

  SPI_SETMODE(spi, (enum spi_mode_e)4);
  SPI_SETBITS(spi, 0);
  SPI_SETBITS(spi, 7);
  SPI_SETBITS(spi, 24);
  SPI_SETBITS(spi, 32);
  if (bl616cl_spi_test_get_diag(bus, &diag) < 0 ||
      diag.last_error != -EINVAL || diag.mode != SPIDEV_MODE1 ||
      diag.nbits != 16)
    {
      return -EIO;
    }

  SPI_SETMODE(spi, SPIDEV_MODE1);
  SPI_SETBITS(spi, 16);

  spi_fake_snapshot(state, &snapshot);
  if (snapshot.mode_exchange_mask != 0x0f ||
      snapshot.bits_exchange_mask != 0x03 ||
      snapshot.frequency_exchange_mask != 0x03
#ifdef CONFIG_SPI_BITORDER
      || snapshot.order_exchange_mask != 0x03
#endif
  )
    {
      return -EIO;
    }

  return OK;
}

static int spi_fake_exchange_case(int bus, struct spi_dev_s *spi,
                                  struct spi_fake_state_s *state)
{
  struct bl616cl_spi_test_diag_s diag;
  struct spi_fake_snapshot_s snapshot;
  struct spi_fake_snapshot_s after;
  uint16_t tx16[4] = {
    0x0123, 0x4567, 0x89ab, 0xcdef
  };

  uint16_t rx16[4] = {
    0
  };

  uint8_t tx[SPI_TEST_FAKE_BUFFER_SIZE];
  uint8_t rx[SPI_TEST_FAKE_BUFFER_SIZE];
  size_t lengths[] = {
    1, 31, 32, 33, SPI_TEST_FAKE_BUFFER_SIZE
  };

  size_t i;

  SPI_SETMODE(spi, SPIDEV_MODE0);
  SPI_SETBITS(spi, 8);
  for (i = 0; i < sizeof(tx); i++)
    {
      tx[i] = (uint8_t)(i * 13 + 7);
    }

  spi_fake_snapshot(state, &snapshot);
  SPI_EXCHANGE(spi, tx, rx, 0);
  spi_fake_snapshot(state, &after);
  if (after.exchange_count != snapshot.exchange_count)
    {
      return -EIO;
    }

  for (i = 0; i < nitems(lengths); i++)
    {
      memset(rx, 0, sizeof(rx));
      SPI_EXCHANGE(spi, tx, rx, lengths[i]);
      spi_fake_snapshot(state, &snapshot);
      if (memcmp(tx, rx, lengths[i]) != 0 ||
          snapshot.last_nbytes != lengths[i])
        {
          return -EIO;
        }
    }

  SPI_EXCHANGE(spi, tx, NULL, sizeof(tx));
  SPI_EXCHANGE(spi, NULL, rx, sizeof(rx));
  for (i = 0; i < sizeof(rx); i++)
    {
      if (rx[i] != 0xff)
        {
          return -EIO;
        }
    }

  SPI_EXCHANGE(spi, NULL, NULL, 1);
  SPI_SETBITS(spi, 16);
  SPI_EXCHANGE(spi, tx16, rx16, nitems(tx16));
  if (memcmp(tx16, rx16, sizeof(tx16)) != 0 ||
      SPI_SEND(spi, 0xa55a) != 0xa55a)
    {
      return -EIO;
    }

  spi_fake_snapshot(state, &snapshot);
  SPI_EXCHANGE(spi, tx + 1, rx + 1, 1);
  if (bl616cl_spi_test_get_diag(bus, &diag) < 0 ||
      diag.last_error != -EINVAL)
    {
      return -EIO;
    }

  spi_fake_snapshot(state, &after);
  if (after.exchange_count != snapshot.exchange_count)
    {
      return -EIO;
    }

  SPI_SETBITS(spi, 8);
  SPI_EXCHANGE(spi, tx, rx, 1);
  return bl616cl_spi_test_get_diag(bus, &diag) < 0 ||
             diag.last_error != OK ?
           -EIO :
           OK;
}

static int spi_fake_sequence(int bus, struct spi_dev_s *spi,
                             struct spi_fake_state_s *state)
{
  struct spi_trans_s trans[2];
  struct spi_sequence_s seq;
  uint8_t tx[2] = {
    0x35, 0xca
  };

  uint8_t rx[2] = {
    0
  };

  uint32_t selects;
  uint32_t deselects;
  struct spi_fake_snapshot_s before;
  struct spi_fake_snapshot_s after;
  struct bl616cl_spi_test_diag_s diag;
  int ret;

  memset(&trans, 0, sizeof(trans));
  memset(&seq, 0, sizeof(seq));
  trans[0].deselect = false;
  trans[0].nwords = 1;
  trans[0].txbuffer = &tx[0];
  trans[0].rxbuffer = &rx[0];
  trans[1].deselect = true;
  trans[1].nwords = 1;
  trans[1].txbuffer = &tx[1];
  trans[1].rxbuffer = &rx[1];
  seq.dev = SPI_TEST_TARGET0;
  seq.mode = SPIDEV_MODE0;
  seq.nbits = 8;
  seq.ntrans = nitems(trans);
  seq.frequency = SPI_TEST_DEFAULT_FREQUENCY;
  seq.trans = trans;

  pthread_mutex_lock(&state->lock);
  selects = state->select_count;
  deselects = state->deselect_count;
  pthread_mutex_unlock(&state->lock);
  ret = spi_transfer(spi, &seq);
  if (ret < 0 || memcmp(tx, rx, sizeof(tx)) != 0)
    {
      return -EIO;
    }

  if (bl616cl_spi_test_get_diag(bus, &diag) < 0 || diag.last_error != OK)
    {
      return -EIO;
    }

  spi_fake_snapshot(state, &after);
  ret = after.select_count == selects + 3 &&
        after.deselect_count == deselects + 2 ?
        OK :
        -EIO;

  if (ret == OK)
    {
      memset(rx, 0, sizeof(rx));
      trans[0].deselect = true;
      spi_fake_snapshot(state, &before);
      ret = spi_transfer(spi, &seq);
      spi_fake_snapshot(state, &after);
      if (ret < 0 || memcmp(tx, rx, sizeof(tx)) != 0 ||
          after.select_count != before.select_count + 3 ||
          after.deselect_count != before.deselect_count + 3)
        {
          ret = -EIO;
        }

      if (ret == OK &&
          (bl616cl_spi_test_get_diag(bus, &diag) < 0 ||
           diag.last_error != OK))
        {
          ret = -EIO;
        }

      trans[0].deselect = false;
    }

  if (ret == OK)
    {
      spi_fake_snapshot(state, &before);
      SPI_SELECT(spi, SPI_TEST_TARGET1, true);
      SPI_SELECT(spi, SPI_TEST_TARGET1, true);
      SPI_SELECT(spi, SPI_TEST_TARGET1, false);
      SPI_SELECT(spi, SPI_TEST_TARGET1, false);
      spi_fake_snapshot(state, &after);
      if (after.select_count != before.select_count + 2 ||
          after.deselect_count != before.deselect_count + 2 ||
          after.last_select_devid != SPI_TEST_TARGET1 ||
          after.last_deselect_devid != SPI_TEST_TARGET1)
        {
          ret = -EIO;
        }
    }

  if (ret == OK)
    {
      spi_fake_snapshot(state, &before);
      seq.dev = SPI_TEST_UNKNOWN_TARGET;
      ret = spi_transfer(spi, &seq);
      spi_fake_snapshot(state, &after);
      if (ret < 0 || after.exchange_count != before.exchange_count ||
          after.rejected_select_count != before.rejected_select_count + 3)
        {
          ret = -EIO;
        }

      if (ret == OK && bl616cl_spi_test_get_diag(bus, &diag) < 0)
        {
          ret = -EIO;
        }
      else if (ret == OK && diag.last_error != -ENODEV)
        {
          ret = -EIO;
        }

      SPI_SELECT(spi, SPI_TEST_TARGET0, true);
      SPI_SELECT(spi, SPI_TEST_TARGET0, false);
    }

  return ret;
}

static void *spi_fake_worker(void *arg)
{
  struct spi_fake_worker_s *worker = arg;
  uint8_t tx[16];
  uint8_t rx[16];
  unsigned int i;

  for (i = 0; i < sizeof(tx); i++)
    {
      tx[i] = (uint8_t)(worker->id * 61 + i);
    }

  while (sem_wait(worker->start) < 0 && errno == EINTR)
    {
    }

  worker->result = OK;
  for (i = 0; i < CONFIG_BL_MCU_PERIPHERAL_TESTS_SPI_ITERATIONS; i++)
    {
      if (SPI_LOCK(worker->spi, true) < 0)
        {
          worker->result = -EIO;
          break;
        }

      SPI_SETMODE(worker->spi, worker->mode);
      SPI_SETBITS(worker->spi, worker->nbits);
      if (SPI_SETFREQUENCY(worker->spi, worker->frequency) == 0)
        {
          SPI_LOCK(worker->spi, false);
          worker->result = -EIO;
          break;
        }

      memset(rx, 0, sizeof(rx));
      SPI_EXCHANGE(worker->spi, tx, rx,
                   sizeof(tx) / (worker->nbits == 16 ? 2 : 1));
      SPI_LOCK(worker->spi, false);
      if (memcmp(tx, rx, sizeof(tx)) != 0)
        {
          worker->result = -EIO;
          break;
        }
    }

  return NULL;
}

static int spi_fake_concurrent(struct spi_dev_s *spi,
                               struct spi_fake_state_s *state)
{
  struct spi_fake_worker_s workers[2];
  pthread_t threads[2];
  sem_t start;
  struct spi_fake_snapshot_s snapshot;
  unsigned int created = 0;
  unsigned int i;
  int ret = OK;

  if (sem_init(&start, 0, 0) < 0)
    {
      return -errno;
    }

  pthread_mutex_lock(&state->lock);
  state->exchange_delay = 1000;
  pthread_mutex_unlock(&state->lock);
  for (i = 0; i < nitems(workers); i++)
    {
      workers[i].spi = spi;
      workers[i].start = &start;
      workers[i].id = i;
      workers[i].mode = i == 0 ? SPIDEV_MODE0 : SPIDEV_MODE3;
      workers[i].nbits = i == 0 ? 8 : 16;
      workers[i].frequency = i == 0 ? 100000 : 1000000;
      workers[i].result = -EINPROGRESS;
      if (pthread_create(&threads[i], NULL, spi_fake_worker,
                         &workers[i]) != 0)
        {
          ret = -EIO;
          break;
        }

      created++;
    }

  for (i = 0; i < created; i++)
    {
      sem_post(&start);
    }

  for (i = 0; i < created; i++)
    {
      pthread_join(threads[i], NULL);
      if (workers[i].result < 0)
        {
          ret = workers[i].result;
        }
    }

  pthread_mutex_lock(&state->lock);
  state->exchange_delay = 0;
  pthread_mutex_unlock(&state->lock);
  sem_destroy(&start);
  spi_fake_snapshot(state, &snapshot);
  return ret == OK && snapshot.max_active == 1 &&
             snapshot.mode_exchange_mask == 0x9 &&
             snapshot.bits_exchange_mask == 0x3 &&
             snapshot.frequency_exchange_mask == 0x3 ?
           OK :
           -EIO;
}

static int spi_fake_errors(int bus, struct spi_dev_s *spi,
                           struct spi_fake_state_s *state)
{
  struct bl616cl_spi_test_diag_s before;
  struct bl616cl_spi_test_diag_s after;
  struct spi_fake_snapshot_s snapshot;
  uint8_t tx = 0x5a;
  uint8_t rx = 0;
  int ret;

  SPI_SETBITS(spi, 8);
  ret = bl616cl_spi_test_get_diag(bus, &before);
  if (ret < 0)
    {
      return ret;
    }

  pthread_mutex_lock(&state->lock);
  state->exchange_result = -116;
  pthread_mutex_unlock(&state->lock);
  SPI_EXCHANGE(spi, &tx, &rx, 1);
  ret = bl616cl_spi_test_get_diag(bus, &after);
  spi_fake_snapshot(state, &snapshot);
  if (ret < 0 || after.last_error != -ETIMEDOUT ||
      after.error_count != before.error_count + 1 ||
      snapshot.recover_count != 1)
    {
      printf("SPI_TEST fake timeout FAIL ret=%d error=%d before=%" PRIu32
             " after=%" PRIu32 " recover=%" PRIu32 "\n",
             ret, after.last_error, before.error_count, after.error_count,
             snapshot.recover_count);
      return -EIO;
    }

  pthread_mutex_lock(&state->lock);
  state->exchange_result = OK;
  pthread_mutex_unlock(&state->lock);
  SPI_EXCHANGE(spi, &tx, &rx, 1);
  ret = bl616cl_spi_test_get_diag(bus, &after);
  if (ret < 0 || after.last_error != OK || rx != tx)
    {
      printf("SPI_TEST fake timeout recovery FAIL ret=%d error=%d"
             " tx=0x%02x rx=0x%02x\n",
             ret, after.last_error, tx, rx);
      return -EIO;
    }

  ret = bl616cl_spi_test_get_diag(bus, &before);
  if (ret < 0)
    {
      return ret;
    }

  pthread_mutex_lock(&state->lock);
  state->exchange_result = -EIO;
  pthread_mutex_unlock(&state->lock);
  SPI_EXCHANGE(spi, &tx, &rx, 1);
  ret = bl616cl_spi_test_get_diag(bus, &after);
  spi_fake_snapshot(state, &snapshot);
  if (ret < 0 || after.last_error != -EIO ||
      after.error_count != before.error_count + 1 ||
      snapshot.recover_count != 2)
    {
      printf("SPI_TEST fake I/O FAIL ret=%d error=%d before=%" PRIu32
             " after=%" PRIu32 " recover=%" PRIu32 "\n",
             ret, after.last_error, before.error_count, after.error_count,
             snapshot.recover_count);
      return -EIO;
    }

  pthread_mutex_lock(&state->lock);
  state->exchange_result = OK;
  pthread_mutex_unlock(&state->lock);
  rx = 0;
  SPI_EXCHANGE(spi, &tx, &rx, 1);
  ret = bl616cl_spi_test_get_diag(bus, &after);
  if (ret < 0 || after.last_error != OK || rx != tx)
    {
      printf("SPI_TEST fake I/O recovery FAIL ret=%d error=%d"
             " tx=0x%02x rx=0x%02x\n",
             ret, after.last_error, tx, rx);
      return -EIO;
    }

  return OK;
}

static int spi_fake_dual(void)
{
  struct spi_fake_tracker_s tracker;
  struct spi_fake_state_s states[2];
  struct spi_fake_worker_s workers[2];
  struct spi_dev_s *spis[2] = {
    NULL, NULL
  };

  pthread_t threads[2];
  sem_t start;
  bool owns[2] = {
    false, false
  };

  unsigned int installed = 0;
  unsigned int initialized = 0;
  unsigned int created = 0;
  unsigned int i;
  int ret = OK;

  memset(&tracker, 0, sizeof(tracker));
  ret = pthread_mutex_init(&tracker.lock, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  memset(states, 0, sizeof(states));
  if (sem_init(&start, 0, 0) < 0)
    {
      pthread_mutex_destroy(&tracker.lock);
      return -errno;
    }

  for (i = 0; i < nitems(states); i++)
    {
      ret = pthread_mutex_init(&states[i].lock, NULL);
      if (ret != 0)
        {
          ret = -ret;
          break;
        }

      initialized++;
      states[i].tracker = &tracker;
      states[i].exchange_delay = 10000;
      spis[i] = bl616cl_spi_test_device(i);
      if (spis[i] == NULL)
        {
          ret = bl616cl_spi_test_initialize(i);
          if (ret < 0)
            {
              break;
            }

          owns[i] = true;
          spis[i] = bl616cl_spi_test_device(i);
        }

      if (spis[i] == NULL)
        {
          ret = -ENODEV;
          break;
        }

      ret = bl616cl_spi_test_install(i, &g_spi_fake_ops, &states[i]);
      if (ret < 0)
        {
          break;
        }

      installed++;
      workers[i].spi = spis[i];
      workers[i].start = &start;
      workers[i].id = i;
      workers[i].mode = i == 0 ? SPIDEV_MODE0 : SPIDEV_MODE3;
      workers[i].nbits = i == 0 ? 8 : 16;
      workers[i].frequency = i == 0 ? 100000 : 1000000;
      workers[i].result = -EINPROGRESS;
      if (pthread_create(&threads[i], NULL, spi_fake_worker,
                         &workers[i]) != 0)
        {
          ret = -EIO;
          break;
        }

      created++;
    }

  for (i = 0; i < created; i++)
    {
      sem_post(&start);
    }

  for (i = 0; i < created; i++)
    {
      pthread_join(threads[i], NULL);
      if (workers[i].result < 0)
        {
          ret = workers[i].result;
        }
    }

  if (ret == OK && (created != 2 || tracker.max_active != 2))
    {
      ret = -EIO;
    }

  for (i = 0; i < installed; i++)
    {
      (void)bl616cl_spi_test_install(i, NULL, NULL);
    }

  for (i = 0; i < nitems(states); i++)
    {
      if (owns[i])
        {
          (void)bl616cl_spibus_uninitialize(spis[i]);
        }
    }

  for (i = 0; i < initialized; i++)
    {
      pthread_mutex_destroy(&states[i].lock);
    }

  sem_destroy(&start);
  pthread_mutex_destroy(&tracker.lock);
  printf("SPI_TEST fake dual result=%s overlap=%" PRIu32 "\n",
         ret == OK ? "PASS" : "FAIL", tracker.max_active);
  return ret;
}

static int spi_run_fake(const char *name, int bus)
{
  struct spi_fake_state_s state;
  struct spi_dev_s *spi;
  bool initialized = false;
  bool run_all = strcmp(name, "all") == 0;
  unsigned int cases = 0;
  int ret = OK;

  memset(&state, 0, sizeof(state));
  pthread_mutex_init(&state.lock, NULL);
  spi = bl616cl_spi_test_device(bus);
  if (spi == NULL && bl616cl_spi_test_initialize(bus) == OK)
    {
      spi = bl616cl_spi_test_device(bus);
      initialized = spi != NULL;
    }

  ret = spi == NULL ? -ENODEV :
                      bl616cl_spi_test_install(bus, &g_spi_fake_ops, &state);
  if (ret < 0)
    {
      if (initialized)
        {
          (void)bl616cl_spibus_uninitialize(spi);
        }

      printf("SPI_TEST fake FAIL bus=%d reason=no-device\n", bus);
      pthread_mutex_destroy(&state.lock);
      return ret;
    }

  if (strcmp(name, "lifecycle") == 0 || run_all)
    {
      spi_fake_reset_observations(&state);
      ret = spi_fake_lifecycle(bus, spi, &state, initialized);
      cases++;
      if (run_all)
        {
          spi_fake_report("lifecycle", bus, ret, &state);
        }
    }

  if (ret == OK && (strcmp(name, "config") == 0 ||
                    run_all))
    {
      spi_fake_reset_observations(&state);
      ret = spi_fake_config(bus, spi, &state);
      cases++;
      if (run_all)
        {
          spi_fake_report("config", bus, ret, &state);
        }
    }

  if (ret == OK && (strcmp(name, "exchange") == 0 ||
                    run_all))
    {
      spi_fake_reset_observations(&state);
      ret = spi_fake_exchange_case(bus, spi, &state);
      cases++;
      if (run_all)
        {
          spi_fake_report("exchange", bus, ret, &state);
        }
    }

  if (ret == OK && (strcmp(name, "sequence") == 0 ||
                    run_all))
    {
      spi_fake_reset_observations(&state);
      ret = spi_fake_sequence(bus, spi, &state);
      cases++;
      if (run_all)
        {
          spi_fake_report("sequence", bus, ret, &state);
        }
    }

  if (ret == OK && (strcmp(name, "errors") == 0 ||
                    run_all))
    {
      spi_fake_reset_observations(&state);
      ret = spi_fake_errors(bus, spi, &state);
      cases++;
      if (run_all)
        {
          spi_fake_report("errors", bus, ret, &state);
        }
    }

  if (ret == OK && (strcmp(name, "concurrent") == 0 ||
                    run_all))
    {
      spi_fake_reset_observations(&state);
      ret = spi_fake_concurrent(spi, &state);
      cases++;
      if (run_all)
        {
          spi_fake_report("concurrent", bus, ret, &state);
        }
    }

  if (strcmp(name, "lifecycle") != 0 && strcmp(name, "config") != 0 &&
      strcmp(name, "exchange") != 0 && strcmp(name, "sequence") != 0 &&
      strcmp(name, "errors") != 0 && strcmp(name, "concurrent") != 0 &&
      !run_all)
    {
      ret = -EINVAL;
    }

  (void)bl616cl_spi_test_install(bus, NULL, NULL);
  if (initialized)
    {
      (void)bl616cl_spibus_uninitialize(spi);
    }

  if (run_all)
    {
      printf("SPI_TEST fake all bus=%d result=%s cases=%u\n", bus,
             ret == OK ? "PASS" : "FAIL", cases);
    }
  else
    {
      spi_fake_report(name, bus, ret, &state);
    }

  pthread_mutex_destroy(&state.lock);
  return ret;
}

static int spi_ioctl_transfer(int fd, const struct spi_test_config_s *cfg,
                              const void *txbuffer, void *rxbuffer,
                              size_t nwords)
{
  struct spi_trans_s trans;
  struct spi_sequence_s seq;

  memset(&trans, 0, sizeof(trans));
  memset(&seq, 0, sizeof(seq));
  trans.deselect = true;
  trans.nwords = nwords;
  trans.txbuffer = txbuffer;
  trans.rxbuffer = rxbuffer;
#ifdef CONFIG_SPI_HWFEATURES
  trans.hwfeat = cfg->lsbfirst ? HWFEAT_LSBFIRST : 0;
#endif
  seq.dev = cfg->devid;
  seq.mode = cfg->mode;
  seq.nbits = cfg->nbits;
  seq.ntrans = 1;
  seq.frequency = cfg->frequency;
  seq.trans = &trans;
  return ioctl(fd, SPIIOC_TRANSFER, (unsigned long)((uintptr_t)&seq));
}

static int spi_hw_check_diag(int bus)
{
#ifdef CONFIG_BL616CL_SPI_TEST
  struct bl616cl_spi_test_diag_s diag;

  return bl616cl_spi_test_get_diag(bus, &diag) < 0 ||
         diag.last_error != OK ?
       -EIO :
       OK;
#else
  (void)bus;
  return OK;
#endif
}

static int spi_hw_loopback(const struct spi_test_config_s *cfg)
{
  char path[32];
  size_t bytes = cfg->length * (cfg->nbits == 16 ? 2 : 1);
  uint8_t *tx;
  uint8_t *rx;
  size_t i;
  int fd;
  int ret;

  tx = malloc(bytes);
  rx = malloc(bytes);
  if (tx == NULL || rx == NULL)
    {
      free(tx);
      free(rx);
      return -ENOMEM;
    }

  for (i = 0; i < bytes; i++)
    {
      tx[i] = (uint8_t)(i * 53 + 0x29);
    }

  memset(rx, 0, bytes);
  snprintf(path, sizeof(path), "/dev/spi%d", cfg->bus);
  fd = open(path, O_RDWR);
  if (fd < 0)
    {
      ret = -errno;
      goto out;
    }

  ret = spi_ioctl_transfer(fd, cfg, tx, rx, cfg->length);
  if (ret >= 0)
    {
      ret = spi_hw_check_diag(cfg->bus);
    }

  if (ret >= 0 && memcmp(tx, rx, bytes) != 0)
    {
      ret = -EIO;
    }

  close(fd);
out:
  printf("SPI_TEST hw loopback bus=%d mode=%u bits=%u freq=%" PRIu32
         " words=%zu order=%s result=%s\n",
         cfg->bus, cfg->mode, cfg->nbits, cfg->frequency, cfg->length,
         cfg->lsbfirst ? "lsb" : "msb", ret >= 0 ? "PASS" : "FAIL");
  free(tx);
  free(rx);
  return ret < 0 ? ret : OK;
}

static int spi_hw_paths(const struct spi_test_config_s *cfg)
{
  char path[32];
  size_t bytes = cfg->length * (cfg->nbits == 16 ? 2 : 1);
  uint8_t *tx;
  uint8_t *rx;
  size_t i;
  int fd;
  int ret;

  tx = malloc(bytes);
  rx = malloc(bytes);
  if (tx == NULL || rx == NULL)
    {
      free(tx);
      free(rx);
      return -ENOMEM;
    }

  for (i = 0; i < bytes; i++)
    {
      tx[i] = (uint8_t)(i * 17 + 3);
    }

  snprintf(path, sizeof(path), "/dev/spi%d", cfg->bus);
  fd = open(path, O_RDWR);
  if (fd < 0)
    {
      ret = -errno;
      goto out;
    }

  memset(rx, 0, bytes);
  ret = spi_ioctl_transfer(fd, cfg, tx, rx, cfg->length);
  if (ret >= 0)
    {
      ret = spi_hw_check_diag(cfg->bus);
    }

  if (ret >= 0 && memcmp(tx, rx, bytes) != 0)
    {
      ret = -EIO;
    }

  if (ret >= 0)
    {
      ret = spi_ioctl_transfer(fd, cfg, tx, NULL, cfg->length);
      if (ret >= 0)
        {
          ret = spi_hw_check_diag(cfg->bus);
        }
    }

  if (ret >= 0)
    {
      memset(rx, 0, bytes);
      ret = spi_ioctl_transfer(fd, cfg, NULL, rx, cfg->length);
      if (ret >= 0)
        {
          ret = spi_hw_check_diag(cfg->bus);
        }

      for (i = 0; ret >= 0 && i < bytes; i++)
        {
          if (rx[i] != 0xff)
            {
              ret = -EIO;
            }
        }
    }

  close(fd);
out:
  printf("SPI_TEST hw paths bus=%d mode=%u bits=%u freq=%" PRIu32
         " words=%zu result=%s\n",
         cfg->bus, cfg->mode, cfg->nbits, cfg->frequency, cfg->length,
         ret >= 0 ? "PASS" : "FAIL");
  free(tx);
  free(rx);
  return ret < 0 ? ret : OK;
}

static int spi_hw_sequence(const struct spi_test_config_s *cfg)
{
  char path[32];
  struct spi_trans_s trans[2];
  struct spi_sequence_s seq;
  uint16_t tx[2] = {
    0x1235, 0xcafe
  };

  uint16_t rx[2] = {
    0
  };

  size_t bytes;
  int fd;
  int ret;
  bool hold_pass = false;
  bool release_pass = false;

  snprintf(path, sizeof(path), "/dev/spi%d", cfg->bus);
  fd = open(path, O_RDWR);
  if (fd < 0)
    {
      ret = -errno;
      goto out;
    }

  memset(&trans, 0, sizeof(trans));
  memset(&seq, 0, sizeof(seq));
  trans[0].deselect = false;
#ifdef CONFIG_SPI_HWFEATURES
  trans[0].hwfeat = cfg->lsbfirst ? HWFEAT_LSBFIRST : 0;
#endif
  trans[0].nwords = 1;
  trans[0].txbuffer = cfg->nbits == 16 ? (void *)&tx[0] :
                                         (void *)((uint8_t *)tx + 0);
  trans[0].rxbuffer = cfg->nbits == 16 ? (void *)&rx[0] :
                                         (void *)((uint8_t *)rx + 0);
  trans[1].deselect = true;
#ifdef CONFIG_SPI_HWFEATURES
  trans[1].hwfeat = cfg->lsbfirst ? HWFEAT_LSBFIRST : 0;
#endif
  trans[1].nwords = 1;
  trans[1].txbuffer = cfg->nbits == 16 ? (void *)&tx[1] :
                                         (void *)((uint8_t *)tx + 1);
  trans[1].rxbuffer = cfg->nbits == 16 ? (void *)&rx[1] :
                                         (void *)((uint8_t *)rx + 1);
  seq.dev = cfg->devid;
  seq.mode = cfg->mode;
  seq.nbits = cfg->nbits;
  seq.frequency = cfg->frequency;
  seq.ntrans = nitems(trans);
  seq.trans = trans;
  ret = ioctl(fd, SPIIOC_TRANSFER, (unsigned long)((uintptr_t)&seq));
  if (ret >= 0)
    {
      ret = spi_hw_check_diag(cfg->bus);
    }

  bytes = cfg->nbits == 16 ? sizeof(tx) : 2;
  if (ret >= 0 && memcmp(tx, rx, bytes) != 0)
    {
      ret = -EIO;
    }

  hold_pass = ret >= 0;

  if (ret >= 0)
    {
      memset(rx, 0, sizeof(rx));
      trans[0].deselect = true;
      ret = ioctl(fd, SPIIOC_TRANSFER, (unsigned long)((uintptr_t)&seq));
      if (ret >= 0)
        {
          ret = spi_hw_check_diag(cfg->bus);
        }

      if (ret >= 0 && memcmp(tx, rx, bytes) != 0)
        {
          ret = -EIO;
        }

      release_pass = ret >= 0;
    }

  close(fd);

out:
  printf("SPI_TEST hw sequence bus=%d mode=%u bits=%u freq=%" PRIu32
         " cs=hold:%s release:%s result=%s\n",
         cfg->bus, cfg->mode, cfg->nbits, cfg->frequency,
         hold_pass ? "PASS" : "FAIL", release_pass ? "PASS" : "FAIL",
         ret >= 0 ? "PASS" : "FAIL");
  return ret < 0 ? ret : OK;
}

static int spi_hw_boundary(struct spi_test_config_s *cfg)
{
  const size_t lengths[] = {
    1, 31, 32, 33, 256, 1024
  };

  unsigned int mode;
  unsigned int bits;
  unsigned int i;
  int ret;

  for (mode = 0; mode < 4; mode++)
    {
      cfg->mode = mode;
      for (bits = 8; bits <= 16; bits += 8)
        {
          cfg->nbits = bits;
          for (i = 0; i < nitems(lengths); i++)
            {
              cfg->length = lengths[i];
              ret = spi_hw_loopback(cfg);
              if (ret < 0)
                {
                  return ret;
                }
            }
        }
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  struct spi_test_config_s cfg;
  int ret;

  if (argc < 3 || spi_test_parse_options(argc, argv, &cfg) < 0)
    {
      spi_test_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "fake") == 0)
    {
      ret = strcmp(argv[2], "dual") == 0 ?
              spi_fake_dual() : spi_run_fake(argv[2], cfg.bus);
    }
  else if (strcmp(argv[1], "hw") == 0 &&
           strcmp(argv[2], "loopback") == 0)
    {
      ret = spi_hw_loopback(&cfg);
    }
  else if (strcmp(argv[1], "hw") == 0 &&
           strcmp(argv[2], "boundary") == 0)
    {
      ret = spi_hw_boundary(&cfg);
    }
  else if (strcmp(argv[1], "hw") == 0 &&
           strcmp(argv[2], "paths") == 0)
    {
      ret = spi_hw_paths(&cfg);
    }
  else if (strcmp(argv[1], "hw") == 0 &&
           strcmp(argv[2], "sequence") == 0)
    {
      ret = spi_hw_sequence(&cfg);
    }
  else
    {
      spi_test_usage(argv[0]);
      return EXIT_FAILURE;
    }

  return ret == OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
