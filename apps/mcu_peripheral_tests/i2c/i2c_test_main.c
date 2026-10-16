/****************************************************************************
 * vendor/bouffalolab/apps/mcu_peripheral_tests/i2c/i2c_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>

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

#include <nuttx/i2c/i2c_master.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define I2C_TEST_DEFAULT_ADDR        0x50
#define I2C_TEST_DEFAULT_FREQUENCY   I2C_SPEED_STANDARD
#define I2C_TEST_DEFAULT_ADDR_WIDTH  2
#define I2C_TEST_DEFAULT_PAGE_SIZE   32
#define I2C_TEST_DEFAULT_LENGTH      1024
#define I2C_TEST_DEFAULT_WRITE_DELAY 10000
#define I2C_TEST_MAX_TRANSFER        1024
#define I2C_TEST_MAX_PREFIX          16
#define I2C_TEST_CONCURRENT_LENGTH   32

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct i2c_hw_config_s
{
  int bus;
  int bus2;
  uint16_t addr;
  uint16_t addr2;
  uint32_t frequency;
  uint32_t offset;
  uint32_t offset2;
  size_t addr_width;
  size_t page_size;
  size_t length;
  uint32_t capacity;
  unsigned int iterations;
  useconds_t write_delay;
  bool allow_write;
  bool capacity_set;
  bool expect_no_target;
  bool split_write;
};

struct i2c_hw_worker_s
{
  const struct i2c_hw_config_s *cfg;
  sem_t *start;
  int bus;
  uint16_t addr;
  uint32_t offset;
  size_t length;
  const uint8_t *expected;
  int result;
  unsigned int completed;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void i2c_test_usage(const char *progname)
{
  printf("Usage:\n");
  printf("  %s hw <probe|combined|boundary|eeprom|concurrent|dual> "
         "[options]\n",
         progname);
  printf("Hardware options:\n");
  printf("  --bus N              Primary bus (default: 0)\n");
  printf("  --bus2 N             Secondary bus for dual (default: 1)\n");
  printf("  --addr N             Primary 7-bit address (default: 0x50)\n");
  printf("  --addr2 N            Secondary address (default: --addr)\n");
  printf("  --freq HZ            100000 or 400000 (default: 100000)\n");
  printf("  --offset N           Primary EEPROM offset (default: 0)\n");
  printf("  --offset2 N          Secondary offset (default: --offset)\n");
  printf("  --addr-width N       EEPROM address bytes: 1 or 2 "
         "(default: 2)\n");
  printf("  --length N           Transfer region, 1..1024 "
         "(default: 1024)\n");
  printf("  --page-size N        EEPROM page bytes (default: 32)\n");
  printf("  --capacity N         EEPROM bytes (required by eeprom case)\n");
  printf("  --iterations N       Concurrent transfers (default: %d)\n",
         CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_ITERATIONS);
  printf("  --write-delay-us N   Delay after each EEPROM page "
         "(default: 10000)\n");
  printf("  --allow-write        Required by the destructive EEPROM case\n");
  printf("  --no-target          Probe expects the selected address not to "
         "ACK\n");
  printf("  --split-write        EEPROM writes send the offset and the data "
         "as two\n"
         "                       messages, the data one with NOSTART\n");
}

static int i2c_parse_u32(const char *text, uint32_t *value)
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

static int i2c_parse_hw_options(int argc, char *argv[],
                                struct i2c_hw_config_s *cfg)
{
  bool addr2_set = false;
  bool offset2_set = false;
  bool uses_offset;
  uint32_t value;
  int i;

  memset(cfg, 0, sizeof(*cfg));
  uses_offset = strcmp(argv[2], "probe") != 0;
  cfg->bus = 0;
  cfg->bus2 = 1;
  cfg->addr = I2C_TEST_DEFAULT_ADDR;
  cfg->frequency = I2C_TEST_DEFAULT_FREQUENCY;
  cfg->addr_width = I2C_TEST_DEFAULT_ADDR_WIDTH;
  cfg->page_size = I2C_TEST_DEFAULT_PAGE_SIZE;
  cfg->length = I2C_TEST_DEFAULT_LENGTH;
  cfg->iterations = CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_ITERATIONS;
  cfg->write_delay = I2C_TEST_DEFAULT_WRITE_DELAY;

  for (i = 3; i < argc; i++)
    {
      if (strcmp(argv[i], "--allow-write") == 0)
        {
          cfg->allow_write = true;
          continue;
        }

      if (strcmp(argv[i], "--no-target") == 0)
        {
          cfg->expect_no_target = true;
          continue;
        }

      if (strcmp(argv[i], "--split-write") == 0)
        {
          cfg->split_write = true;
          continue;
        }

      if (i + 1 >= argc || i2c_parse_u32(argv[i + 1], &value) < 0)
        {
          printf("I2C_TEST option FAIL name=%s value=%s\n", argv[i],
                 i + 1 < argc ? argv[i + 1] : "<missing>");
          return -EINVAL;
        }

      if (strcmp(argv[i], "--bus") == 0 && value <= INT_MAX)
        {
          cfg->bus = (int)value;
        }
      else if (strcmp(argv[i], "--bus2") == 0 && value <= INT_MAX)
        {
          cfg->bus2 = (int)value;
        }
      else if (strcmp(argv[i], "--addr") == 0 && value <= UINT16_MAX)
        {
          cfg->addr = (uint16_t)value;
        }
      else if (strcmp(argv[i], "--addr2") == 0 && value <= UINT16_MAX)
        {
          cfg->addr2 = (uint16_t)value;
          addr2_set = true;
        }
      else if (strcmp(argv[i], "--freq") == 0)
        {
          cfg->frequency = value;
        }
      else if (strcmp(argv[i], "--offset") == 0)
        {
          cfg->offset = value;
        }
      else if (strcmp(argv[i], "--offset2") == 0)
        {
          cfg->offset2 = value;
          offset2_set = true;
        }
      else if (strcmp(argv[i], "--addr-width") == 0)
        {
          cfg->addr_width = value;
        }
      else if (strcmp(argv[i], "--page-size") == 0)
        {
          cfg->page_size = value;
        }
      else if (strcmp(argv[i], "--capacity") == 0)
        {
          cfg->capacity = value;
          cfg->capacity_set = true;
        }
      else if (strcmp(argv[i], "--length") == 0)
        {
          cfg->length = value;
        }
      else if (strcmp(argv[i], "--iterations") == 0)
        {
          cfg->iterations = value;
        }
      else if (strcmp(argv[i], "--write-delay-us") == 0)
        {
          cfg->write_delay = value;
        }
      else
        {
          printf("I2C_TEST option FAIL unsupported=%s\n", argv[i]);
          return -EINVAL;
        }

      i++;
    }

  if (!addr2_set)
    {
      cfg->addr2 = cfg->addr;
    }

  if (!offset2_set)
    {
      cfg->offset2 = cfg->offset;
    }

  if (cfg->bus < 0 || cfg->bus2 < 0 || cfg->addr > 0x7f ||
      cfg->addr2 > 0x7f ||
      (cfg->frequency != I2C_SPEED_STANDARD &&
       cfg->frequency != I2C_SPEED_FAST) ||
      (cfg->addr_width != 1 && cfg->addr_width != 2) ||
      cfg->length == 0 || cfg->length > I2C_TEST_MAX_TRANSFER ||
      cfg->page_size == 0 ||
      cfg->page_size > I2C_TEST_MAX_TRANSFER - cfg->addr_width ||
      cfg->iterations == 0 || cfg->iterations > 100000 ||
      cfg->write_delay > 1000000)
    {
      printf("I2C_TEST option FAIL invalid range\n");
      return -EINVAL;
    }

  if (uses_offset &&
      ((cfg->addr_width == 1 &&
        (cfg->offset > UINT8_MAX || cfg->offset2 > UINT8_MAX)) ||
       cfg->offset > UINT16_MAX || cfg->offset2 > UINT16_MAX))
    {
      printf("I2C_TEST option FAIL offset does not fit address width\n");
      return -EINVAL;
    }

  if (uses_offset &&
      ((uint64_t)cfg->offset + cfg->length >
         (UINT64_C(1) << (cfg->addr_width * 8)) ||
       (uint64_t)cfg->offset2 + cfg->length >
         (UINT64_C(1) << (cfg->addr_width * 8))))
    {
      printf("I2C_TEST option FAIL offset+length exceeds address space\n");
      return -EINVAL;
    }

  if (strcmp(argv[2], "eeprom") == 0 &&
      (!cfg->capacity_set || cfg->capacity == 0 ||
       cfg->capacity > (UINT64_C(1) << (cfg->addr_width * 8)) ||
       (uint64_t)cfg->offset + cfg->length > cfg->capacity ||
       cfg->page_size > cfg->capacity))
    {
      printf("I2C_TEST option FAIL eeprom requires valid --capacity and "
             "offset+length within capacity\n");
      return -EINVAL;
    }

  if (cfg->expect_no_target &&
      (strcmp(argv[2], "probe") != 0 || cfg->addr < 0x08 ||
       cfg->addr > 0x77))
    {
      printf("I2C_TEST option FAIL --no-target requires probe address "
             "0x08..0x77\n");
      return -EINVAL;
    }

  return OK;
}

static int i2c_open_bus(int bus)
{
  char path[16];
  int fd;

  snprintf(path, sizeof(path), "/dev/i2c%d", bus);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      printf("I2C_TEST open FAIL path=%s errno=%d\n", path, errno);
      return -errno;
    }

  return fd;
}

static int i2c_ioctl_transfer(int fd, struct i2c_msg_s *msgs, size_t count)
{
  struct i2c_transfer_s transfer;
  int ret;

  transfer.msgv = msgs;
  transfer.msgc = count;
  ret = ioctl(fd, I2CIOC_TRANSFER,
              (unsigned long)((uintptr_t)&transfer));
  return ret < 0 ? -errno : ret;
}

static void i2c_encode_offset(uint8_t *buffer, size_t width,
                              uint32_t offset)
{
  size_t i;

  for (i = 0; i < width; i++)
    {
      buffer[width - i - 1] = offset & 0xff;
      offset >>= 8;
    }
}

static int i2c_combined_read_fd(int fd, uint16_t addr, uint32_t frequency,
                                uint32_t offset, size_t addr_width,
                                uint8_t *buffer, size_t length)
{
  uint8_t prefix[I2C_TEST_MAX_PREFIX];
  struct i2c_msg_s msgs[2];

  if (addr_width == 0 || addr_width > I2C_TEST_MAX_PREFIX ||
      length == 0 || length > I2C_TEST_MAX_TRANSFER)
    {
      return -EINVAL;
    }

  i2c_encode_offset(prefix, addr_width, offset);
  msgs[0].frequency = frequency;
  msgs[0].addr = addr;
  msgs[0].flags = I2C_M_NOSTOP;
  msgs[0].buffer = prefix;
  msgs[0].length = addr_width;
  msgs[1].frequency = frequency;
  msgs[1].addr = addr;
  msgs[1].flags = I2C_M_READ;
  msgs[1].buffer = buffer;
  msgs[1].length = length;
  return i2c_ioctl_transfer(fd, msgs, 2);
}

static int i2c_eeprom_write_fd(int fd, const struct i2c_hw_config_s *cfg,
                               uint16_t addr, uint32_t offset,
                               const uint8_t *buffer, size_t length)
{
  uint8_t *tx;
  struct i2c_msg_s msgs[2];
  size_t done = 0;
  int ret = OK;

  tx = malloc(cfg->page_size + cfg->addr_width);
  if (tx == NULL)
    {
      return -ENOMEM;
    }

  while (done < length)
    {
      uint32_t current = offset + done;
      size_t room = cfg->page_size - current % cfg->page_size;
      size_t chunk = length - done;

      if (chunk > room)
        {
          chunk = room;
        }

      if (chunk > I2C_TEST_MAX_TRANSFER - cfg->addr_width)
        {
          chunk = I2C_TEST_MAX_TRANSFER - cfg->addr_width;
        }

      if (!cfg->capacity_set || current >= cfg->capacity ||
          chunk > cfg->capacity - current)
        {
          ret = -EFBIG;
          break;
        }

      i2c_encode_offset(tx, cfg->addr_width, current);
      memcpy(tx + cfg->addr_width, buffer + done, chunk);
      msgs[0].frequency = cfg->frequency;
      msgs[0].addr = addr;
      msgs[0].flags = 0;
      msgs[0].buffer = tx;
      msgs[0].length = cfg->addr_width + chunk;
      if (cfg->split_write)
        {
          /* Same bytes on the wire: the NOSTART data message continues the
           * offset message without a new START.
           */

          msgs[0].length = cfg->addr_width;
          msgs[1] = msgs[0];
          msgs[1].flags = I2C_M_NOSTART;
          msgs[1].buffer = tx + cfg->addr_width;
          msgs[1].length = chunk;
        }

      ret = i2c_ioctl_transfer(fd, msgs, cfg->split_write ? 2 : 1);
      if (ret < 0)
        {
          break;
        }

      usleep(cfg->write_delay);
      done += chunk;
    }

  free(tx);
  return ret;
}

static int i2c_test_hw_probe(const struct i2c_hw_config_s *cfg)
{
  struct i2c_msg_s msg;
  uint8_t value = 0;
  int fd;
  int ret;

  fd = i2c_open_bus(cfg->bus);
  if (fd < 0)
    {
      return -1;
    }

  msg.frequency = cfg->frequency;
  msg.addr = cfg->addr;
  msg.flags = cfg->expect_no_target ? 0 : I2C_M_READ;
  msg.buffer = &value;
  msg.length = 1;
  ret = i2c_ioctl_transfer(fd, &msg, 1);
  close(fd);
  if (ret < 0)
    {
      bool timeout = ret == -ETIMEDOUT;
      bool pass = cfg->expect_no_target && timeout;

      if (pass)
        {
          printf("I2C_TEST RESULT mode=hw case=probe PASS bus=%d "
                 "addr=0x%02x freq=%" PRIu32 " expected=no-target ret=%d\n",
                 cfg->bus, cfg->addr, cfg->frequency, ret);
        }
      else
        {
          printf("I2C_TEST hw-probe FAIL bus=%d addr=0x%02x freq=%" PRIu32
                 " expected=%s ret=%d\n",
                 cfg->bus, cfg->addr, cfg->frequency,
                 cfg->expect_no_target ? "no-target" : "target", ret);
        }

      return pass ? OK : -1;
    }

  if (cfg->expect_no_target)
    {
      printf("I2C_TEST hw-probe FAIL bus=%d addr=0x%02x "
             "expected=no-target reason=unexpected-ack byte=%02x\n",
             cfg->bus, cfg->addr, value);
      return -1;
    }

  printf("I2C_TEST RESULT mode=hw case=probe PASS bus=%d addr=0x%02x "
         "freq=%" PRIu32 " byte=%02x\n",
         cfg->bus, cfg->addr,
         cfg->frequency, value);
  return OK;
}

static int i2c_test_hw_combined(const struct i2c_hw_config_s *cfg)
{
  uint8_t *buffer;
  int fd;
  int ret;

  buffer = malloc(cfg->length);
  if (buffer == NULL)
    {
      return -1;
    }

  fd = i2c_open_bus(cfg->bus);
  if (fd < 0)
    {
      free(buffer);
      return -1;
    }

  memset(buffer, 0xcc, cfg->length);
  ret = i2c_combined_read_fd(fd, cfg->addr, cfg->frequency, cfg->offset,
                             cfg->addr_width, buffer, cfg->length);
  close(fd);
  if (ret < 0)
    {
      printf("I2C_TEST hw-combined FAIL bus=%d "
             "addr=0x%02x offset=%" PRIu32 " length=%zu ret=%d\n",
             cfg->bus, cfg->addr, cfg->offset, cfg->length, ret);
      free(buffer);
      return -1;
    }

  printf("I2C_TEST RESULT mode=hw case=combined PASS bus=%d addr=0x%02x "
         "freq=%" PRIu32 " offset=%" PRIu32 " prefix=%zu length=%zu "
         "first=%02x last=%02x\n",
         cfg->bus, cfg->addr, cfg->frequency,
         cfg->offset, cfg->addr_width, cfg->length, buffer[0],
         buffer[cfg->length - 1]);
  free(buffer);
  return OK;
}

static int i2c_test_hw_boundary(const struct i2c_hw_config_s *cfg)
{
  static const size_t lengths[] = {
    1, 4, 31, 32, 33, 255, 256, 1024
  };

  uint8_t *buffer;
  size_t completed = 0;
  size_t i;
  int fd;
  int ret;

  buffer = malloc(cfg->length);
  if (buffer == NULL)
    {
      return -1;
    }

  fd = i2c_open_bus(cfg->bus);
  if (fd < 0)
    {
      free(buffer);
      return -1;
    }

  for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
    {
      if (lengths[i] > cfg->length)
        {
          continue;
        }

      memset(buffer, 0xcc, lengths[i]);
      ret = i2c_combined_read_fd(fd, cfg->addr, cfg->frequency, cfg->offset,
                                 cfg->addr_width, buffer, lengths[i]);
      if (ret < 0)
        {
          printf("I2C_TEST boundary FAIL length=%zu ret=%d\n", lengths[i],
                 ret);
          close(fd);
          free(buffer);
          return -1;
        }

      completed++;
      printf("I2C_TEST boundary PASS length=%zu first=%02x last=%02x\n",
             lengths[i], buffer[0], buffer[lengths[i] - 1]);
    }

  close(fd);
  free(buffer);
  printf("I2C_TEST RESULT mode=hw case=boundary PASS bus=%d addr=0x%02x "
         "freq=%" PRIu32 " count=%zu max_length=%zu\n",
         cfg->bus,
         cfg->addr, cfg->frequency, completed, cfg->length);
  return OK;
}

static int i2c_test_hw_eeprom(const struct i2c_hw_config_s *cfg)
{
  uint8_t *backup;
  uint8_t *pattern;
  uint8_t *verify;
  bool write_started = false;
  bool pattern_verified = false;
  bool restore_verified = false;
  int restore_ret = OK;
  int result = -1;
  int fd;
  int ret;
  size_t i;

  if (!cfg->allow_write)
    {
      printf("I2C_TEST hw-eeprom FAIL restore=not-required reason="
             "--allow-write-required\n");
      return -1;
    }

  backup = malloc(cfg->length);
  pattern = malloc(cfg->length);
  verify = malloc(cfg->length);
  if (backup == NULL || pattern == NULL || verify == NULL)
    {
      printf("I2C_TEST hw-eeprom FAIL restore=not-required reason=malloc "
             "length=%zu\n",
             cfg->length);
      free(backup);
      free(pattern);
      free(verify);
      return -1;
    }

  fd = i2c_open_bus(cfg->bus);
  if (fd < 0)
    {
      printf("I2C_TEST hw-eeprom FAIL restore=not-required reason=open "
             "bus=%d\n",
             cfg->bus);
      free(backup);
      free(pattern);
      free(verify);
      return -1;
    }

  ret = i2c_combined_read_fd(fd, cfg->addr, cfg->frequency, cfg->offset,
                             cfg->addr_width, backup, cfg->length);
  if (ret < 0)
    {
      printf("I2C_TEST eeprom FAIL backup-read ret=%d; no write attempted\n",
             ret);
      goto out;
    }

  printf("I2C_TEST eeprom backup PASS offset=%" PRIu32 " length=%zu\n",
         cfg->offset, cfg->length);
  for (i = 0; i < cfg->length; i++)
    {
      pattern[i] = (uint8_t)(0x5a ^ (i * 29u) ^ (cfg->offset + i));
    }

  write_started = true;
  ret = i2c_eeprom_write_fd(fd, cfg, cfg->addr, cfg->offset, pattern,
                            cfg->length);
  if (ret < 0)
    {
      printf("I2C_TEST eeprom FAIL pattern-write ret=%d; restoring backup\n",
             ret);
      goto restore;
    }

  memset(verify, 0, cfg->length);
  ret = i2c_combined_read_fd(fd, cfg->addr, cfg->frequency, cfg->offset,
                             cfg->addr_width, verify, cfg->length);
  if (ret < 0 || memcmp(pattern, verify, cfg->length) != 0)
    {
      printf("I2C_TEST eeprom FAIL pattern-verify ret=%d match=%d; "
             "restoring backup\n",
             ret,
             ret >= 0 && memcmp(pattern, verify, cfg->length) == 0);
      goto restore;
    }

  pattern_verified = true;
  printf("I2C_TEST eeprom pattern PASS offset=%" PRIu32 " length=%zu "
         "page=%zu\n",
         cfg->offset, cfg->length, cfg->page_size);

restore:
  if (write_started)
    {
      restore_ret = i2c_eeprom_write_fd(fd, cfg, cfg->addr, cfg->offset,
                                        backup, cfg->length);
      if (restore_ret >= 0)
        {
          memset(verify, 0, cfg->length);
          restore_ret = i2c_combined_read_fd(fd, cfg->addr, cfg->frequency,
                                             cfg->offset, cfg->addr_width,
                                             verify, cfg->length);
        }

      if (restore_ret < 0 || memcmp(backup, verify, cfg->length) != 0)
        {
          printf("I2C_TEST eeprom RESTORE FAIL ret=%d match=%d "
                 "data-may-be-modified\n",
                 restore_ret,
                 restore_ret >= 0 &&
                   memcmp(backup, verify, cfg->length) == 0);
          goto out;
        }

      restore_verified = true;
      printf("I2C_TEST eeprom restore PASS verified=%zu\n", cfg->length);
    }

  if (pattern_verified && restore_verified)
    {
      result = OK;
    }

out:
  close(fd);
  free(backup);
  free(pattern);
  free(verify);
  if (result == OK)
    {
      printf("I2C_TEST RESULT mode=hw case=eeprom PASS bus=%d addr=0x%02x "
             "offset=%" PRIu32 " length=%zu write=%s backup=verified "
             "restore=verified\n",
             cfg->bus, cfg->addr, cfg->offset,
             cfg->length, cfg->split_write ? "split" : "single");
    }
  else
    {
      printf("I2C_TEST hw-eeprom FAIL restore=%s\n",
             !write_started   ? "not-required" :
             restore_verified ? "verified" :
                                "failed");
    }

  return result;
}

static void *i2c_hw_worker(void *arg)
{
  struct i2c_hw_worker_s *worker = arg;
  uint8_t buffer[I2C_TEST_CONCURRENT_LENGTH];
  unsigned int i;
  int fd;
  int ret;

  worker->result = -1;
  worker->completed = 0;
  fd = i2c_open_bus(worker->bus);
  if (fd < 0)
    {
      return NULL;
    }

  sem_wait(worker->start);
  for (i = 0; i < worker->cfg->iterations; i++)
    {
      ret = i2c_combined_read_fd(fd, worker->addr,
                                 worker->cfg->frequency, worker->offset,
                                 worker->cfg->addr_width, buffer,
                                 worker->length);
      if (ret < 0 || memcmp(buffer, worker->expected, worker->length) != 0)
        {
          printf("I2C_TEST worker FAIL bus=%d iteration=%u ret=%d "
                 "match=%d\n",
                 worker->bus, i, ret,
                 ret >= 0 &&
                   memcmp(buffer, worker->expected, worker->length) == 0);
          close(fd);
          return NULL;
        }

      worker->completed++;
    }

  close(fd);
  worker->result = OK;
  return NULL;
}

static int i2c_read_reference(const struct i2c_hw_config_s *cfg, int bus,
                              uint16_t addr, uint32_t offset,
                              uint8_t *buffer, size_t length)
{
  int fd;
  int ret;

  fd = i2c_open_bus(bus);
  if (fd < 0)
    {
      return fd;
    }

  ret = i2c_combined_read_fd(fd, addr, cfg->frequency, offset,
                             cfg->addr_width, buffer, length);
  close(fd);
  return ret;
}

static int i2c_test_hw_concurrent(const struct i2c_hw_config_s *cfg)
{
  struct i2c_hw_worker_s
    workers[CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS];
  pthread_t threads[CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS];
  uint8_t expected[I2C_TEST_CONCURRENT_LENGTH];
  size_t length = cfg->length;
  sem_t start;
  int created = 0;
  int failed = 0;
  int ret;
  int i;

  if (length > sizeof(expected))
    {
      length = sizeof(expected);
    }

  ret = i2c_read_reference(cfg, cfg->bus, cfg->addr, cfg->offset, expected,
                           length);
  if (ret < 0)
    {
      printf("I2C_TEST hw-concurrent FAIL reference ret=%d\n", ret);
      return -1;
    }

  sem_init(&start, 0, 0);
  for (i = 0; i < CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS; i++)
    {
      workers[i].cfg = cfg;
      workers[i].start = &start;
      workers[i].bus = cfg->bus;
      workers[i].addr = cfg->addr;
      workers[i].offset = cfg->offset;
      workers[i].length = length;
      workers[i].expected = expected;
      ret = pthread_create(&threads[i], NULL, i2c_hw_worker, &workers[i]);
      if (ret != 0)
        {
          printf("I2C_TEST hw-concurrent FAIL create=%d ret=%d\n", i, ret);
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
      failed += workers[i].result < 0;
    }

  sem_destroy(&start);
  if (created != CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS || failed != 0)
    {
      printf("I2C_TEST hw-concurrent FAIL threads=%d "
             "failed=%d\n",
             created, failed);
      return -1;
    }

  printf("I2C_TEST RESULT mode=hw case=concurrent PASS bus=%d threads=%d "
         "iterations=%u length=%zu atomic_combined_reads=%u\n",
         cfg->bus,
         created, cfg->iterations, length,
         created * cfg->iterations);
  return OK;
}

static int i2c_test_hw_dual(const struct i2c_hw_config_s *cfg)
{
  struct i2c_hw_worker_s workers[2];
  pthread_t threads[2];
  uint8_t expected[2][I2C_TEST_CONCURRENT_LENGTH];
  size_t length = cfg->length;
  sem_t start;
  int created = 0;
  int failed = 0;
  int ret;
  int i;

  if (cfg->bus == cfg->bus2)
    {
      printf("I2C_TEST dual FAIL bus and bus2 must differ\n");
      return -1;
    }

  if (length > I2C_TEST_CONCURRENT_LENGTH)
    {
      length = I2C_TEST_CONCURRENT_LENGTH;
    }

  ret = i2c_read_reference(cfg, cfg->bus, cfg->addr, cfg->offset,
                           expected[0], length);
  if (ret >= 0)
    {
      ret = i2c_read_reference(cfg, cfg->bus2, cfg->addr2, cfg->offset2,
                               expected[1], length);
    }

  if (ret < 0)
    {
      printf("I2C_TEST dual FAIL reference ret=%d\n", ret);
      return -1;
    }

  sem_init(&start, 0, 0);
  memset(workers, 0, sizeof(workers));
  for (i = 0; i < 2; i++)
    {
      workers[i].cfg = cfg;
      workers[i].start = &start;
      workers[i].bus = i == 0 ? cfg->bus : cfg->bus2;
      workers[i].addr = i == 0 ? cfg->addr : cfg->addr2;
      workers[i].offset = i == 0 ? cfg->offset : cfg->offset2;
      workers[i].length = length;
      workers[i].expected = expected[i];
      ret = pthread_create(&threads[i], NULL, i2c_hw_worker, &workers[i]);
      if (ret != 0)
        {
          printf("I2C_TEST dual FAIL create=%d ret=%d\n", i, ret);
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
      failed += workers[i].result < 0;
    }

  sem_destroy(&start);
  if (created != 2 || failed != 0)
    {
      printf("I2C_TEST hw-dual FAIL threads=%d failed=%d\n",
             created, failed);
      return -1;
    }

  printf("I2C_TEST RESULT mode=hw case=dual PASS bus0=%d addr0=0x%02x "
         "bus1=%d addr1=0x%02x iterations=%u length=%zu\n",
         cfg->bus,
         cfg->addr, cfg->bus2, cfg->addr2, cfg->iterations, length);
  return OK;
}

static int i2c_run_hw(const char *name, const struct i2c_hw_config_s *cfg)
{
  if (strcmp(name, "probe") == 0)
    {
      return i2c_test_hw_probe(cfg);
    }

  if (strcmp(name, "combined") == 0)
    {
      return i2c_test_hw_combined(cfg);
    }

  if (strcmp(name, "boundary") == 0)
    {
      return i2c_test_hw_boundary(cfg);
    }

  if (strcmp(name, "eeprom") == 0)
    {
      return i2c_test_hw_eeprom(cfg);
    }

  if (strcmp(name, "concurrent") == 0)
    {
      return i2c_test_hw_concurrent(cfg);
    }

  if (strcmp(name, "dual") == 0)
    {
      return i2c_test_hw_dual(cfg);
    }

  return -EINVAL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  struct i2c_hw_config_s cfg;
  int ret;

  if (argc < 3)
    {
      i2c_test_usage(argv[0]);
      printf("I2C_TEST RESULT mode=invalid case=invalid FAIL ret=-%d\n",
             EINVAL);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "hw") == 0)
    {
      ret = i2c_parse_hw_options(argc, argv, &cfg);
      if (ret >= 0)
        {
          ret = i2c_run_hw(argv[2], &cfg);
        }
    }
  else
    {
      ret = -EINVAL;
    }

  if (ret == -EINVAL)
    {
      i2c_test_usage(argv[0]);
    }

  if (ret < 0)
    {
      printf("I2C_TEST RESULT mode=%s case=%s FAIL ret=%d\n",
             argv[1], argv[2], ret);
    }

  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
