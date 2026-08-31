/****************************************************************************
 * apps/vendor/bouffalolab/apps/mcu_peripheral_tests/i2c/i2c_test_main.c
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

#include "bl616cl_i2c.h"

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

#define I2C_FAKE_MAX_CALLS           16
#define I2C_FAKE_MAX_MSGS            2
#define I2C_FAKE_MAX_EVENTS          64

#define I2C_FAKE_EVENT_CONFIGURE     1
#define I2C_FAKE_EVENT_TRANSFER      2
#define I2C_FAKE_EVENT_STATUS        3
#define I2C_FAKE_EVENT_CLEANUP       4

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
};

struct i2c_fake_msg_s
{
  uint16_t addr;
  uint16_t flags;
  uint16_t length;
  uint8_t first;
};

struct i2c_fake_call_s
{
  uint32_t frequency;
  int count;
  struct i2c_fake_msg_s msgs[I2C_FAKE_MAX_MSGS];
};

struct i2c_fake_parallel_s
{
  pthread_mutex_t lock;
  unsigned int active;
  unsigned int max_active;
};

struct i2c_fake_state_s
{
  pthread_mutex_t lock;
  uint32_t frequency;
  uint32_t raw_status;
  int configure_result;
  int transfer_result;
  useconds_t transfer_delay;
  unsigned int configure_count;
  unsigned int transfer_count;
  unsigned int status_count;
  unsigned int cleanup_count;
  unsigned int active;
  unsigned int max_active;
  unsigned int event_count;
  uint8_t events[I2C_FAKE_MAX_EVENTS];
  struct i2c_fake_call_s calls[I2C_FAKE_MAX_CALLS];
  struct i2c_fake_parallel_s *parallel;
};

struct i2c_fake_worker_s
{
  struct i2c_master_s *dev;
  sem_t *start;
  int id;
  int result;
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
  printf("  %s fake <flags|boundary|invalid|errors|concurrent|dual|all> "
         "[--bus N]\n",
         progname);
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

static int i2c_parse_fake_bus(int argc, char *argv[], int *bus)
{
  uint32_t value;
  int i;

  *bus = 0;
  for (i = 3; i < argc; i++)
    {
      if (strcmp(argv[i], "--bus") != 0 || i + 1 >= argc ||
          i2c_parse_u32(argv[i + 1], &value) < 0 || value > INT_MAX)
        {
          printf("I2C_TEST fake option FAIL at=%s\n", argv[i]);
          return -EINVAL;
        }

      *bus = (int)value;
      i++;
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
  struct i2c_msg_s msg;
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
      msg.frequency = cfg->frequency;
      msg.addr = addr;
      msg.flags = 0;
      msg.buffer = tx;
      msg.length = cfg->addr_width + chunk;
      ret = i2c_ioctl_transfer(fd, &msg, 1);
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

static void i2c_fake_event(struct i2c_fake_state_s *state, uint8_t event)
{
  if (state->event_count < I2C_FAKE_MAX_EVENTS)
    {
      state->events[state->event_count] = event;
    }

  state->event_count++;
}

static int i2c_fake_configure(void *arg, uint32_t frequency)
{
  struct i2c_fake_state_s *state = arg;
  int result;

  pthread_mutex_lock(&state->lock);
  state->frequency = frequency;
  state->configure_count++;
  i2c_fake_event(state, I2C_FAKE_EVENT_CONFIGURE);
  result = state->configure_result;
  pthread_mutex_unlock(&state->lock);
  return result;
}

static int i2c_fake_transfer(void *arg,
                             const struct bl616cl_i2c_test_msg_s *msgs,
                             int count)
{
  struct i2c_fake_state_s *state = arg;
  struct i2c_fake_call_s *call = NULL;
  useconds_t delay;
  unsigned int index;
  int result;
  int i;

  pthread_mutex_lock(&state->lock);
  index = state->transfer_count++;
  if (index < I2C_FAKE_MAX_CALLS)
    {
      call = &state->calls[index];
      call->frequency = state->frequency;
      call->count = count;
      for (i = 0; i < count && i < I2C_FAKE_MAX_MSGS; i++)
        {
          call->msgs[i].addr = msgs[i].addr;
          call->msgs[i].flags = msgs[i].flags;
          call->msgs[i].length = msgs[i].length;
          call->msgs[i].first = msgs[i].buffer[0];
        }
    }

  state->active++;
  if (state->active > state->max_active)
    {
      state->max_active = state->active;
    }

  i2c_fake_event(state, I2C_FAKE_EVENT_TRANSFER);
  result = state->transfer_result;
  delay = state->transfer_delay;
  pthread_mutex_unlock(&state->lock);

  if (state->parallel != NULL)
    {
      pthread_mutex_lock(&state->parallel->lock);
      state->parallel->active++;
      if (state->parallel->active > state->parallel->max_active)
        {
          state->parallel->max_active = state->parallel->active;
        }

      pthread_mutex_unlock(&state->parallel->lock);
    }

  if (delay != 0)
    {
      usleep(delay);
    }

  if (result >= 0)
    {
      for (i = 0; i < count; i++)
        {
          size_t j;

          if ((msgs[i].flags & I2C_M_READ) != 0)
            {
              for (j = 0; j < msgs[i].length; j++)
                {
                  msgs[i].buffer[j] = (uint8_t)(0xa0 + j);
                }
            }
        }
    }

  if (state->parallel != NULL)
    {
      pthread_mutex_lock(&state->parallel->lock);
      state->parallel->active--;
      pthread_mutex_unlock(&state->parallel->lock);
    }

  pthread_mutex_lock(&state->lock);
  state->active--;
  pthread_mutex_unlock(&state->lock);
  return result;
}

static uint32_t i2c_fake_status(void *arg)
{
  struct i2c_fake_state_s *state = arg;
  uint32_t status;

  pthread_mutex_lock(&state->lock);
  state->status_count++;
  i2c_fake_event(state, I2C_FAKE_EVENT_STATUS);
  status = state->raw_status;
  pthread_mutex_unlock(&state->lock);
  return status;
}

static void i2c_fake_cleanup(void *arg)
{
  struct i2c_fake_state_s *state = arg;

  pthread_mutex_lock(&state->lock);
  state->cleanup_count++;
  i2c_fake_event(state, I2C_FAKE_EVENT_CLEANUP);
  pthread_mutex_unlock(&state->lock);
}

static const struct bl616cl_i2c_test_ops_s g_i2c_fake_ops =
{
  .configure = i2c_fake_configure,
  .transfer = i2c_fake_transfer,
  .status = i2c_fake_status,
  .cleanup = i2c_fake_cleanup,
};

static void i2c_fake_reset(struct i2c_fake_state_s *state)
{
  pthread_mutex_lock(&state->lock);
  state->frequency = 0;
  state->configure_result = OK;
  state->transfer_result = OK;
  state->transfer_delay = 0;
  state->configure_count = 0;
  state->transfer_count = 0;
  state->status_count = 0;
  state->cleanup_count = 0;
  state->active = 0;
  state->max_active = 0;
  state->event_count = 0;
  memset(state->events, 0, sizeof(state->events));
  memset(state->calls, 0, sizeof(state->calls));
  pthread_mutex_unlock(&state->lock);
}

static int i2c_fake_begin(int bus, struct i2c_fake_state_s *state,
                          struct i2c_master_s **dev)
{
  int ret;

  *dev = NULL;
  memset(state, 0, sizeof(*state));
  pthread_mutex_init(&state->lock, NULL);
  ret = bl616cl_i2c_test_install(bus, &g_i2c_fake_ops, state);
  if (ret < 0)
    {
      pthread_mutex_destroy(&state->lock);
      return ret;
    }

  *dev = bl616cl_i2c_test_device(bus);
  if (*dev == NULL)
    {
      bl616cl_i2c_test_install(bus, NULL, NULL);
      pthread_mutex_destroy(&state->lock);
      return -ENODEV;
    }

  return OK;
}

static void i2c_fake_end(int bus, struct i2c_fake_state_s *state)
{
  bl616cl_i2c_test_install(bus, NULL, NULL);
  pthread_mutex_destroy(&state->lock);
}

static int i2c_fake_expect_rejected(struct i2c_master_s *dev,
                                    struct i2c_fake_state_s *state,
                                    struct i2c_msg_s *msgs, int count,
                                    int expected, const char *name)
{
  int ret;

  i2c_fake_reset(state);
  ret = I2C_TRANSFER(dev, msgs, count);
  if (ret != expected || state->configure_count != 0 ||
      state->transfer_count != 0)
    {
      printf("I2C_TEST fake-invalid FAIL name=%s ret=%d expected=%d "
             "configure=%u transfer=%u\n",
             name, ret, expected,
             state->configure_count, state->transfer_count);
      return -1;
    }

  printf("I2C_TEST fake-invalid PASS name=%s ret=%d hardware_calls=0\n",
         name, ret);
  return OK;
}

static int i2c_test_fake_flags(int bus)
{
  struct i2c_fake_state_s state;
  struct i2c_master_s *dev;
  struct i2c_msg_s msgs[2];
  uint8_t tx = 0x35;
  uint8_t rx[4];
  int ret;

  ret = i2c_fake_begin(bus, &state, &dev);
  if (ret < 0)
    {
      printf("I2C_TEST fake-flags FAIL install=%d\n", ret);
      return -1;
    }

  msgs[0].frequency = I2C_SPEED_STANDARD;
  msgs[0].addr = I2C_TEST_DEFAULT_ADDR;
  msgs[0].flags = 0;
  msgs[0].buffer = &tx;
  msgs[0].length = 1;
  msgs[1].frequency = I2C_SPEED_FAST;
  msgs[1].addr = I2C_TEST_DEFAULT_ADDR;
  msgs[1].flags = I2C_M_READ;
  msgs[1].buffer = rx;
  msgs[1].length = sizeof(rx);
  ret = I2C_TRANSFER(dev, msgs, 2);
  if (ret < 0 || state.configure_count != 2 || state.transfer_count != 2 ||
      state.calls[0].count != 1 || state.calls[1].count != 1 ||
      state.calls[0].frequency != I2C_SPEED_STANDARD ||
      state.calls[1].frequency != I2C_SPEED_FAST ||
      state.calls[0].msgs[0].flags != 0 ||
      state.calls[1].msgs[0].flags != I2C_M_READ || rx[0] != 0xa0)
    {
      printf("I2C_TEST fake-flags FAIL separate ret=%d configure=%u "
             "transfer=%u\n",
             ret, state.configure_count,
             state.transfer_count);
      i2c_fake_end(bus, &state);
      return -1;
    }

  i2c_fake_reset(&state);
  msgs[0].frequency = I2C_SPEED_FAST;
  msgs[0].flags = I2C_M_NOSTOP;
  msgs[1].frequency = I2C_SPEED_FAST;
  ret = I2C_TRANSFER(dev, msgs, 2);
  if (ret < 0 || state.configure_count != 1 || state.transfer_count != 1 ||
      state.calls[0].count != 2 ||
      state.calls[0].msgs[0].flags != I2C_M_NOSTOP ||
      state.calls[0].msgs[1].flags != I2C_M_READ)
    {
      printf("I2C_TEST fake-flags FAIL combined ret=%d configure=%u "
             "transfer=%u count=%d flags=%04x/%04x\n",
             ret,
             state.configure_count, state.transfer_count,
             state.calls[0].count, state.calls[0].msgs[0].flags,
             state.calls[0].msgs[1].flags);
      i2c_fake_end(bus, &state);
      return -1;
    }

  printf("I2C_TEST RESULT mode=fake case=flags PASS "
         "separate_calls=2 combined_calls=1\n");
  i2c_fake_end(bus, &state);
  return OK;
}

static int i2c_test_fake_boundary(int bus)
{
  struct i2c_fake_state_s state;
  struct i2c_master_s *dev;
  struct i2c_msg_s msgs[2];
  uint8_t *tx;
  uint8_t *rx;
  uint8_t prefix[I2C_TEST_MAX_PREFIX];
  const size_t lengths[] = {
    1, I2C_TEST_MAX_TRANSFER
  };

  const size_t prefixes[] = {
    1, I2C_TEST_MAX_PREFIX
  };

  size_t i;
  size_t j;
  int ret;

  ret = i2c_fake_begin(bus, &state, &dev);
  if (ret < 0)
    {
      printf("I2C_TEST fake-boundary FAIL install=%d\n", ret);
      return -1;
    }

  tx = malloc(I2C_TEST_MAX_TRANSFER);
  rx = malloc(I2C_TEST_MAX_TRANSFER);
  if (tx == NULL || rx == NULL)
    {
      free(tx);
      free(rx);
      i2c_fake_end(bus, &state);
      return -1;
    }

  memset(tx, 0x3c, I2C_TEST_MAX_TRANSFER);
  memset(rx, 0, I2C_TEST_MAX_TRANSFER);
  for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
    {
      i2c_fake_reset(&state);
      msgs[0].frequency = I2C_SPEED_STANDARD;
      msgs[0].addr = I2C_TEST_DEFAULT_ADDR;
      msgs[0].flags = 0;
      msgs[0].buffer = tx;
      msgs[0].length = lengths[i];
      ret = I2C_TRANSFER(dev, &msgs[0], 1);
      if (ret < 0 || state.transfer_count != 1 ||
          state.calls[0].count != 1 ||
          state.calls[0].msgs[0].length != lengths[i])
        {
          printf("I2C_TEST fake-boundary FAIL single-write length=%zu "
                 "ret=%d calls=%u\n",
                 lengths[i], ret,
                 state.transfer_count);
          free(tx);
          free(rx);
          i2c_fake_end(bus, &state);
          return -1;
        }

      i2c_fake_reset(&state);
      msgs[0].flags = I2C_M_READ;
      msgs[0].buffer = rx;
      ret = I2C_TRANSFER(dev, &msgs[0], 1);
      if (ret < 0 || state.transfer_count != 1 ||
          state.calls[0].count != 1 ||
          state.calls[0].msgs[0].length != lengths[i] ||
          rx[0] != 0xa0)
        {
          printf("I2C_TEST fake-boundary FAIL single-read length=%zu "
                 "ret=%d calls=%u\n",
                 lengths[i], ret,
                 state.transfer_count);
          free(tx);
          free(rx);
          i2c_fake_end(bus, &state);
          return -1;
        }
    }

  for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
    {
      for (j = 0; j < sizeof(lengths) / sizeof(lengths[0]); j++)
        {
          i2c_fake_reset(&state);
          memset(prefix, 0x4f, sizeof(prefix));
          msgs[0].frequency = I2C_SPEED_FAST;
          msgs[0].addr = I2C_TEST_DEFAULT_ADDR;
          msgs[0].flags = I2C_M_NOSTOP;
          msgs[0].buffer = prefix;
          msgs[0].length = prefixes[i];
          msgs[1].frequency = I2C_SPEED_FAST;
          msgs[1].addr = I2C_TEST_DEFAULT_ADDR;
          msgs[1].flags = I2C_M_READ;
          msgs[1].buffer = rx;
          msgs[1].length = lengths[j];
          ret = I2C_TRANSFER(dev, msgs, 2);
          if (ret < 0 || state.transfer_count != 1 ||
              state.calls[0].count != 2 ||
              state.calls[0].msgs[0].length != prefixes[i] ||
              state.calls[0].msgs[1].length != lengths[j] ||
              state.calls[0].msgs[0].flags != I2C_M_NOSTOP ||
              state.calls[0].msgs[1].flags != I2C_M_READ)
            {
              printf("I2C_TEST fake-boundary FAIL combined prefix=%zu "
                     "read=%zu ret=%d calls=%u\n",
                     prefixes[i],
                     lengths[j], ret, state.transfer_count);
              free(tx);
              free(rx);
              i2c_fake_end(bus, &state);
              return -1;
            }
        }
    }

  printf("I2C_TEST RESULT mode=fake case=boundary PASS "
         "single_lengths=1,1024 combined_prefixes=1,16 "
         "read_lengths=1,1024 bus=%d\n",
         bus);
  free(tx);
  free(rx);
  i2c_fake_end(bus, &state);
  return OK;
}

static int i2c_test_fake_invalid(int bus)
{
  struct i2c_fake_state_s state;
  struct i2c_master_s *dev;
  struct i2c_msg_s msgs[3];
  uint8_t buffer[I2C_TEST_MAX_PREFIX + 1];
  int failed = 0;
  int ret;

  ret = i2c_fake_begin(bus, &state, &dev);
  if (ret < 0)
    {
      printf("I2C_TEST fake-invalid FAIL install=%d\n", ret);
      return -1;
    }

  memset(msgs, 0, sizeof(msgs));
  msgs[0].frequency = I2C_SPEED_STANDARD;
  msgs[0].addr = I2C_TEST_DEFAULT_ADDR;
  msgs[0].buffer = buffer;
  msgs[0].length = 1;

  failed += i2c_fake_expect_rejected(dev, &state, NULL, 1, -EINVAL,
                                     "null-vector") < 0;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 0, -EINVAL,
                                     "zero-count") < 0;
  msgs[0].frequency = 0;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 1, -EINVAL,
                                     "zero-frequency") < 0;
  msgs[0].frequency = 200000;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 1, -EINVAL,
                                     "unsupported-frequency") < 0;
  msgs[0].frequency = I2C_SPEED_STANDARD;
  msgs[0].addr = 0x80;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 1, -EINVAL,
                                     "address-128") < 0;
  msgs[0].addr = I2C_TEST_DEFAULT_ADDR;
  msgs[0].flags = I2C_M_TEN;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 1, -EOPNOTSUPP,
                                     "ten-bit") < 0;
  msgs[0].flags = I2C_M_NOSTART;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 1, -EOPNOTSUPP,
                                     "nostart") < 0;
  msgs[0].flags = 0x8000;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 1, -EINVAL,
                                     "unknown-flags") < 0;
  msgs[0].flags = 0;
  msgs[0].length = 0;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 1, -EINVAL,
                                     "zero-length") < 0;
  msgs[0].length = I2C_TEST_MAX_TRANSFER + 1;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 1, -EINVAL,
                                     "oversize") < 0;
  msgs[0].length = 1;
  msgs[0].buffer = NULL;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 1, -EINVAL,
                                     "null-buffer") < 0;
  msgs[0].buffer = buffer;
  msgs[0].flags = I2C_M_NOSTOP;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 1, -EOPNOTSUPP,
                                     "terminal-nostop") < 0;

  msgs[1] = msgs[0];
  msgs[0].flags = I2C_M_NOSTOP;
  msgs[0].length = I2C_TEST_MAX_PREFIX + 1;
  msgs[1].flags = I2C_M_READ;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 2, -EOPNOTSUPP,
                                     "combined-prefix-17") < 0;
  msgs[0].length = 1;
  msgs[1].addr++;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 2, -EINVAL,
                                     "combined-address-mismatch") < 0;
  msgs[1].addr = msgs[0].addr;
  msgs[1].frequency = I2C_SPEED_FAST;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 2, -EINVAL,
                                     "combined-frequency-mismatch") < 0;
  msgs[1].frequency = msgs[0].frequency;
  msgs[2] = msgs[1];
  msgs[1].flags = I2C_M_READ | I2C_M_NOSTOP;
  failed += i2c_fake_expect_rejected(dev, &state, msgs, 3, -EOPNOTSUPP,
                                     "three-phase-chain") < 0;

  i2c_fake_end(bus, &state);
  if (failed != 0)
    {
      printf("I2C_TEST fake-invalid FAIL count=%d\n",
             failed);
      return -1;
    }

  printf("I2C_TEST RESULT mode=fake case=invalid PASS rejected=16\n");
  return OK;
}

static int i2c_test_fake_errors(int bus)
{
  struct i2c_fake_state_s state;
  struct i2c_master_s *dev;
  struct i2c_msg_s msg;
  uint8_t buffer = 0;
  int ret;

  ret = i2c_fake_begin(bus, &state, &dev);
  if (ret < 0)
    {
      printf("I2C_TEST fake-errors FAIL install=%d\n", ret);
      return -1;
    }

  msg.frequency = I2C_SPEED_STANDARD;
  msg.addr = I2C_TEST_DEFAULT_ADDR;
  msg.flags = I2C_M_READ;
  msg.buffer = &buffer;
  msg.length = 1;

  state.configure_result = -ERANGE;
  state.raw_status = BL616CL_I2C_TEST_STATUS_TIMEOUT;
  ret = I2C_TRANSFER(dev, &msg, 1);
  if (ret != -ERANGE || state.event_count != 3 ||
      state.events[0] != I2C_FAKE_EVENT_CONFIGURE ||
      state.events[1] != I2C_FAKE_EVENT_STATUS ||
      state.events[2] != I2C_FAKE_EVENT_CLEANUP ||
      state.transfer_count != 0 || state.status_count != 1 ||
      state.cleanup_count != 1 ||
      bl616cl_i2c_test_last_status(bus) != state.raw_status)
    {
      printf("I2C_TEST fake-errors FAIL configure ret=%d events=%u "
             "transfer=%u status=%u cleanup=%u\n",
             ret, state.event_count,
             state.transfer_count, state.status_count, state.cleanup_count);
      i2c_fake_end(bus, &state);
      return -1;
    }

  printf("I2C_TEST fake-errors configure-injected ret=%d "
         "raw_status=%08" PRIx32 " order=configure,status,cleanup\n",
         ret, state.raw_status);
  i2c_fake_reset(&state);
  state.transfer_result = -EIO;
  state.raw_status = BL616CL_I2C_TEST_STATUS_NACK |
                     BL616CL_I2C_TEST_STATUS_FER;
  ret = I2C_TRANSFER(dev, &msg, 1);
  if (ret != -EIO || state.event_count != 4 ||
      state.events[0] != I2C_FAKE_EVENT_CONFIGURE ||
      state.events[1] != I2C_FAKE_EVENT_TRANSFER ||
      state.events[2] != I2C_FAKE_EVENT_STATUS ||
      state.events[3] != I2C_FAKE_EVENT_CLEANUP ||
      state.status_count != 1 || state.cleanup_count != 1 ||
      bl616cl_i2c_test_last_status(bus) != state.raw_status)
    {
      printf("I2C_TEST fake-errors FAIL ret=%d events=%u status=%u "
             "cleanup=%u\n",
             ret, state.event_count, state.status_count,
             state.cleanup_count);
      i2c_fake_end(bus, &state);
      return -1;
    }

  printf("I2C_TEST fake-errors injected ret=%d raw_status=%08" PRIx32
         " order=configure,transfer,status,cleanup\n",
         ret,
         state.raw_status);
  i2c_fake_reset(&state);
  ret = I2C_TRANSFER(dev, &msg, 1);
  if (ret < 0 || state.configure_count != 1 || state.transfer_count != 1 ||
      state.status_count != 0 || state.cleanup_count != 0 ||
      bl616cl_i2c_test_last_status(bus) != 0)
    {
      printf("I2C_TEST fake-errors FAIL recovery ret=%d configure=%u "
             "transfer=%u status=%u cleanup=%u\n",
             ret,
             state.configure_count, state.transfer_count,
             state.status_count, state.cleanup_count);
      i2c_fake_end(bus, &state);
      return -1;
    }

  printf("I2C_TEST RESULT mode=fake case=errors PASS "
         "configure_errno=-%d transfer_errno=-%d recovered=1\n",
         ERANGE,
         EIO);
  i2c_fake_end(bus, &state);
  return OK;
}

static void *i2c_fake_worker(void *arg)
{
  struct i2c_fake_worker_s *worker = arg;
  struct i2c_msg_s msg;
  uint8_t buffer;
  int i;

  msg.frequency = I2C_SPEED_STANDARD;
  msg.addr = I2C_TEST_DEFAULT_ADDR;
  msg.flags = I2C_M_READ;
  msg.buffer = &buffer;
  msg.length = 1;
  worker->result = OK;
  sem_wait(worker->start);
  for (i = 0; i < CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_ITERATIONS; i++)
    {
      if (I2C_TRANSFER(worker->dev, &msg, 1) < 0)
        {
          worker->result = -1;
          break;
        }
    }

  return NULL;
}

static int i2c_test_fake_concurrent(int bus)
{
  struct i2c_fake_worker_s
    workers[CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS];
  pthread_t threads[CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS];
  struct i2c_fake_state_s state;
  struct i2c_master_s *dev;
  sem_t start;
  int created = 0;
  int failed = 0;
  int ret;
  int i;

  ret = i2c_fake_begin(bus, &state, &dev);
  if (ret < 0)
    {
      printf("I2C_TEST fake-concurrent FAIL install=%d\n", ret);
      return -1;
    }

  state.transfer_delay = 1000;
  sem_init(&start, 0, 0);
  for (i = 0; i < CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS; i++)
    {
      workers[i].dev = dev;
      workers[i].start = &start;
      workers[i].id = i;
      workers[i].result = -1;
      ret = pthread_create(&threads[i], NULL, i2c_fake_worker, &workers[i]);
      if (ret != 0)
        {
          printf("I2C_TEST fake-concurrent FAIL create=%d ret=%d\n", i,
                 ret);
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
  if (created != CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS || failed != 0 ||
      state.max_active != 1 ||
      state.transfer_count !=
        CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS *
          CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_ITERATIONS)
    {
      printf("I2C_TEST fake-concurrent FAIL threads=%d failed=%d "
             "calls=%u max_active=%u\n",
             created, failed,
             state.transfer_count, state.max_active);
      i2c_fake_end(bus, &state);
      return -1;
    }

  printf("I2C_TEST RESULT mode=fake case=concurrent PASS threads=%d "
         "iterations=%d calls=%u max_active=%u\n",
         CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS,
         CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_ITERATIONS,
         state.transfer_count, state.max_active);
  i2c_fake_end(bus, &state);
  return OK;
}

static int i2c_test_fake_dual(void)
{
  struct i2c_fake_state_s states[2];
  struct i2c_master_s *devices[2];
  struct i2c_fake_worker_s workers[2];
  pthread_t threads[2];
  struct i2c_fake_parallel_s parallel;
  sem_t start;
  int created = 0;
  int failed = 0;
  int ret;
  int i;

  memset(&parallel, 0, sizeof(parallel));
  pthread_mutex_init(&parallel.lock, NULL);
  ret = i2c_fake_begin(0, &states[0], &devices[0]);
  if (ret >= 0)
    {
      ret = i2c_fake_begin(1, &states[1], &devices[1]);
    }

  if (ret < 0)
    {
      printf("I2C_TEST fake-dual FAIL install ret=%d "
             "requires both I2C0/I2C1\n",
             ret);
      if (devices[0] != NULL)
        {
          i2c_fake_end(0, &states[0]);
        }

      pthread_mutex_destroy(&parallel.lock);
      return -1;
    }

  states[0].parallel = &parallel;
  states[1].parallel = &parallel;
  states[0].transfer_delay = 1000;
  states[1].transfer_delay = 1000;
  sem_init(&start, 0, 0);
  for (i = 0; i < 2; i++)
    {
      workers[i].dev = devices[i];
      workers[i].start = &start;
      workers[i].id = i;
      workers[i].result = -1;
      ret = pthread_create(&threads[i], NULL, i2c_fake_worker, &workers[i]);
      if (ret != 0)
        {
          printf("I2C_TEST fake-dual FAIL create=%d ret=%d\n", i, ret);
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
  if (created != 2 || failed != 0 || states[0].max_active != 1 ||
      states[1].max_active != 1 || parallel.max_active < 2)
    {
      printf("I2C_TEST fake-dual FAIL threads=%d failed=%d "
             "bus0_active=%u bus1_active=%u cross_bus_active=%u\n",
             created,
             failed, states[0].max_active, states[1].max_active,
             parallel.max_active);
      i2c_fake_end(1, &states[1]);
      i2c_fake_end(0, &states[0]);
      pthread_mutex_destroy(&parallel.lock);
      return -1;
    }

  printf("I2C_TEST RESULT mode=fake case=dual PASS bus0_calls=%u "
         "bus1_calls=%u per_bus_max_active=1 cross_bus_max_active=%u\n",
         states[0].transfer_count, states[1].transfer_count,
         parallel.max_active);
  i2c_fake_end(1, &states[1]);
  i2c_fake_end(0, &states[0]);
  pthread_mutex_destroy(&parallel.lock);
  return OK;
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
      uint32_t raw_status = bl616cl_i2c_test_last_status(cfg->bus);
      bool timeout = ret == -ETIMEDOUT;
      bool pass = cfg->expect_no_target && timeout;

      if (pass)
        {
          printf("I2C_TEST RESULT mode=hw case=probe PASS bus=%d "
                 "addr=0x%02x freq=%" PRIu32 " expected=no-target ret=%d "
                 "raw_status=%08" PRIx32 "\n",
                 cfg->bus, cfg->addr, cfg->frequency, ret, raw_status);
        }
      else
        {
          printf("I2C_TEST hw-probe FAIL bus=%d addr=0x%02x freq=%" PRIu32
                 " expected=%s ret=%d raw_status=%08" PRIx32 "\n",
                 cfg->bus, cfg->addr, cfg->frequency,
                 cfg->expect_no_target ? "no-target" : "target", ret,
                 raw_status);
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
             "offset=%" PRIu32 " length=%zu backup=verified "
             "restore=verified\n",
             cfg->bus, cfg->addr, cfg->offset,
             cfg->length);
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

static int i2c_run_fake(const char *name, int bus)
{
  int failed = 0;

  if (strcmp(name, "flags") == 0)
    {
      return i2c_test_fake_flags(bus);
    }

  if (strcmp(name, "boundary") == 0)
    {
      return i2c_test_fake_boundary(bus);
    }

  if (strcmp(name, "invalid") == 0)
    {
      return i2c_test_fake_invalid(bus);
    }

  if (strcmp(name, "errors") == 0)
    {
      return i2c_test_fake_errors(bus);
    }

  if (strcmp(name, "concurrent") == 0)
    {
      return i2c_test_fake_concurrent(bus);
    }

  if (strcmp(name, "dual") == 0)
    {
      return i2c_test_fake_dual();
    }

  if (strcmp(name, "all") != 0)
    {
      return -EINVAL;
    }

  failed += i2c_test_fake_flags(bus) < 0;
  failed += i2c_test_fake_boundary(bus) < 0;
  failed += i2c_test_fake_invalid(bus) < 0;
  failed += i2c_test_fake_errors(bus) < 0;
  failed += i2c_test_fake_concurrent(bus) < 0;
  if (failed != 0)
    {
      printf("I2C_TEST fake-all FAIL failed=%d\n", failed);
      return -1;
    }

  printf("I2C_TEST RESULT mode=fake case=all PASS failed=0\n");
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

  if (strcmp(argv[1], "fake") == 0)
    {
      int bus;

      ret = i2c_parse_fake_bus(argc, argv, &bus);
      if (ret < 0)
        {
          i2c_test_usage(argv[0]);
          printf("I2C_TEST RESULT mode=fake case=%s FAIL ret=%d\n",
                 argv[2], ret);
          return EXIT_FAILURE;
        }

      ret = i2c_run_fake(argv[2], bus);
    }
  else if (strcmp(argv[1], "hw") == 0)
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
