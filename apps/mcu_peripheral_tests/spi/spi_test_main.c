/****************************************************************************
 * vendor/bouffalolab/apps/mcu_peripheral_tests/spi/spi_test_main.c
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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SPI_TEST_DEFAULT_FREQUENCY 400000
#define SPI_TEST_DEFAULT_LENGTH    32
#define SPI_TEST_MAX_LENGTH        4096
#define SPI_TEST_TARGET0           SPIDEV_USER(0)

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

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void spi_test_usage(const char *progname)
{
  printf("Usage:\n");
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
  if (ret >= 0 && memcmp(tx, rx, bytes) != 0)
    {
      ret = -EIO;
    }

  /* TX-only must leave no stale RX data: the RX-only read below would see
   * it instead of the 0xff dummy bytes looped back from MOSI.
   */

  if (ret >= 0)
    {
      ret = spi_ioctl_transfer(fd, cfg, tx, NULL, cfg->length);
    }

  if (ret >= 0)
    {
      memset(rx, 0, bytes);
      ret = spi_ioctl_transfer(fd, cfg, NULL, rx, cfg->length);
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

  if (strcmp(argv[1], "hw") == 0 &&
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
