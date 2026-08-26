/****************************************************************************
 * apps/vendor/bouffalolab/apps/mcu_peripheral_tests/gpio/gpio_case_main.c
 *
 * MCU Peripheral GPIO Test Cases
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <nuttx/ioexpander/gpio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DEFAULT_KEY0_DEVPATH   "/dev/gpio8"
#define DEFAULT_KEY1_DEVPATH   "/dev/gpio9"
#define DEFAULT_LED_DEVPATH    "/dev/gpio18"
#define DEFAULT_OUT_DEVPATH    "/dev/gpio31"
#define DEFAULT_IN_DEVPATH     "/dev/gpio32"

#define DEFAULT_PERIOD_US      (100000)
#define DEFAULT_COUNT          (20)
#define DEFAULT_HOLD_MS        (5000)
#define DEFAULT_ARM_MS         (3000)
#define DEFAULT_IRQ_TIMEOUT_MS (5000)
#define LED_BLINK_US           (100000)
#define STABLE_READ_SAMPLES    (8)
#define STABLE_READ_DELAY_US   (5000)
#define KEY_POLL_INTERVAL_US   (100000)
#define IRQ_ARM_SETTLE_US      (10000)

#define CASE_BOARD             "board"
#define CASE_PAIR              "pair"
#define CASE_001               "001"
#define CASE_002               "002"
#define CASE_003               "003"
#define CASE_004               "004"
#define CASE_005               "005"
#define CASE_007               "007"
#define CASE_008               "008"
#define CASE_010               "010"
#define CASE_ALL               "all"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct gpio_handle_s
{
  const char *path;
  int fd;
  enum gpio_pintype_e original_type;
  bool original_type_valid;
};

struct app_config_s
{
  const char *case_id;
  const char *key0_path;
  const char *key1_path;
  const char *led_path;
  const char *out_path;
  const char *in_path;
  uint32_t period_us;
  uint32_t count;
  uint32_t hold_ms;
  uint32_t arm_ms;
  uint32_t irq_timeout_ms;
  bool restore;
  bool verbose;
  bool interactive;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void print_usage(const char *progname);
static bool case_is_supported(const char *case_id);
static int parse_u32_arg(const char *name, const char *arg,
                         uint32_t *value);
static int parse_args(int argc, char *argv[], struct app_config_s *cfg);
static void init_default_config(struct app_config_s *cfg);
static const char *pintype_name(enum gpio_pintype_e type);
static int gpio_open_handle(struct gpio_handle_s *gpio, const char *path,
                            bool optional);
static void gpio_close_handle(struct gpio_handle_s *gpio,
                              bool restore_original);
static int gpio_get_type(struct gpio_handle_s *gpio,
                         enum gpio_pintype_e *type);
static int gpio_set_type(struct gpio_handle_s *gpio,
                         enum gpio_pintype_e type);
static int gpio_write_value(struct gpio_handle_s *gpio, bool value);
static int gpio_read_value(struct gpio_handle_s *gpio, bool *value);
static int stable_read(struct gpio_handle_s *gpio, bool *value);
static int led_set_active_low(struct gpio_handle_s *led, bool on);
static int led_blink(struct gpio_handle_s *led, unsigned int count);
static int wait_for_key_state(struct gpio_handle_s *key, bool expected,
                              uint32_t timeout_ms, const char *prompt);
static bool path_equal(const char *lhs, const char *rhs);
static bool case_uses_pair(const struct app_config_s *cfg);
static int validate_config_paths(const struct app_config_s *cfg);
static int validate_record_path_unique(const char *label,
                                       const char *path,
                                       const char *const *labels,
                                       const char *const *paths,
                                       size_t count);
static int wait_gpio_signal(int signo, uint32_t timeout_ms);
static int register_gpio_signal(struct gpio_handle_s *gpio,
                                struct sigevent *event);
static int unregister_gpio_signal(struct gpio_handle_s *gpio);
static int run_key_irq_case(struct gpio_handle_s *key,
                            struct gpio_handle_s *led,
                            enum gpio_pintype_e irq_type,
                            uint32_t timeout_ms,
                            const char *prompt,
                            const char *case_id,
                            unsigned int blink_count);
static int run_board_case(const struct app_config_s *cfg);
static int run_case_001_pair(const struct app_config_s *cfg);
static int run_case_002_pair(const struct app_config_s *cfg);
static int run_case_003_pair(const struct app_config_s *cfg);
static int run_irq_pair_case(const struct app_config_s *cfg,
                             enum gpio_pintype_e irq_type,
                             bool initial_level, bool trigger_level,
                             const char *case_id);
static int run_case_004_pair(const struct app_config_s *cfg);
static int run_case_005_pair(const struct app_config_s *cfg);
static int run_pair_case(const struct app_config_s *cfg);
static int run_case_007_measure(const struct app_config_s *cfg);
static int run_case_008_measure(const struct app_config_s *cfg);
static int record_one_gpio(const char *path);
static int run_case_010_record(const struct app_config_s *cfg);
static int run_selected_case(const struct app_config_s *cfg);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: print_usage
 *
 * Description:
 *   Print the command-line usage and the list of supported options.
 *
 * Input Parameters:
 *   progname - Program name to show in the usage line (argv[0]).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void print_usage(const char *progname)
{
  printf("Usage: %s [options]\n", progname);
  printf("Options:\n");
  printf("  -c <id>        board, pair, 001, 002, 003, 004, 005, "
         "007, 008, 010, all\n");
  printf("                 default: all\n");
  printf("  --key0 <dev>   KEY0 GPIO path (default: %s)\n",
         DEFAULT_KEY0_DEVPATH);
  printf("  --key1 <dev>   KEY1 GPIO path (default: %s)\n",
         DEFAULT_KEY1_DEVPATH);
  printf("  --led <dev>    LED GPIO path, active-low (default: %s)\n",
         DEFAULT_LED_DEVPATH);
  printf("  --out <dev>    Pair output GPIO path (default: %s)\n",
         DEFAULT_OUT_DEVPATH);
  printf("  --in <dev>     Pair input/IRQ GPIO path (default: %s)\n",
         DEFAULT_IN_DEVPATH);
  printf("  -p <us>        Toggle period in us (default: %u)\n",
         DEFAULT_PERIOD_US);
  printf("  -n <count>     Toggle or IRQ count (default: %u)\n",
         DEFAULT_COUNT);
  printf("  -H <ms>        Measurement hold time (default: %u)\n",
         DEFAULT_HOLD_MS);
  printf("  -A <ms>        Arm delay before execution (default: %u)\n",
         DEFAULT_ARM_MS);
  printf("  -T <ms>        Key/IRQ wait timeout (default: %u)\n",
         DEFAULT_IRQ_TIMEOUT_MS);
  printf("  -r             Restore original pintype when closing GPIOs\n");
  printf("  -v             Verbose output\n");
  printf("  --interactive Run manual key press/release checks "
         "in board case\n");
  printf("  -h             Show this help\n");
}

/****************************************************************************
 * Name: case_is_supported
 *
 * Description:
 *   Test whether the given case identifier is one of the supported cases.
 *
 * Input Parameters:
 *   case_id - Case identifier string to validate.
 *
 * Returned Value:
 *   True if the case is supported, false otherwise.
 *
 ****************************************************************************/

static bool case_is_supported(const char *case_id)
{
  return strcmp(case_id, CASE_BOARD) == 0 ||
         strcmp(case_id, CASE_PAIR) == 0 ||
         strcmp(case_id, CASE_001) == 0 ||
         strcmp(case_id, CASE_002) == 0 ||
         strcmp(case_id, CASE_003) == 0 ||
         strcmp(case_id, CASE_004) == 0 ||
         strcmp(case_id, CASE_005) == 0 ||
         strcmp(case_id, CASE_007) == 0 ||
         strcmp(case_id, CASE_008) == 0 ||
         strcmp(case_id, CASE_010) == 0 ||
         strcmp(case_id, CASE_ALL) == 0;
}

/****************************************************************************
 * Name: parse_u32_arg
 *
 * Description:
 *   Parse a string into a uint32_t, accepting decimal, hex or octal
 *   (strtoul base 0) and rejecting trailing garbage or overflow.
 *
 * Input Parameters:
 *   name  - Human-readable argument name used in error messages.
 *   arg   - Argument string to parse.
 *   value - Receives the parsed value on success.
 *
 * Returned Value:
 *   Zero on success; -EINVAL if the string is not a valid uint32_t.
 *
 ****************************************************************************/

static int parse_u32_arg(const char *name, const char *arg,
                         uint32_t *value)
{
  char *endptr = NULL;
  unsigned long parsed;

  parsed = strtoul(arg, &endptr, 0);
  if (endptr == arg || *endptr != '\0' || parsed > UINT32_MAX)
    {
      printf("ERROR: invalid %s '%s'\n", name, arg);
      return -EINVAL;
    }

  *value = (uint32_t)parsed;
  return 0;
}

/****************************************************************************
 * Name: init_default_config
 *
 * Description:
 *   Populate the configuration with the built-in default device paths and
 *   timing parameters.
 *
 * Input Parameters:
 *   cfg - Configuration structure to initialize.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void init_default_config(struct app_config_s *cfg)
{
  cfg->case_id = CASE_ALL;
  cfg->key0_path = DEFAULT_KEY0_DEVPATH;
  cfg->key1_path = DEFAULT_KEY1_DEVPATH;
  cfg->led_path = DEFAULT_LED_DEVPATH;
  cfg->out_path = DEFAULT_OUT_DEVPATH;
  cfg->in_path = DEFAULT_IN_DEVPATH;
  cfg->period_us = DEFAULT_PERIOD_US;
  cfg->count = DEFAULT_COUNT;
  cfg->hold_ms = DEFAULT_HOLD_MS;
  cfg->arm_ms = DEFAULT_ARM_MS;
  cfg->irq_timeout_ms = DEFAULT_IRQ_TIMEOUT_MS;
  cfg->restore = false;
  cfg->verbose = false;
  cfg->interactive = false;
}

/****************************************************************************
 * Name: parse_args
 *
 * Description:
 *   Initialize the default configuration and override it from the
 *   command-line arguments, then validate the selected case and timing.
 *
 * Input Parameters:
 *   argc - Argument count.
 *   argv - Argument vector.
 *   cfg  - Configuration structure to fill in.
 *
 * Returned Value:
 *   Zero on success; 1 if help was requested and printed; a negated errno
 *   value on an invalid argument.
 *
 ****************************************************************************/

static int parse_args(int argc, char *argv[], struct app_config_s *cfg)
{
  int i;
  int ret;

  init_default_config(cfg);

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
          print_usage(argv[0]);
          return 1;
        }

      if (strcmp(argv[i], "-v") == 0)
        {
          cfg->verbose = true;
          continue;
        }

      if (strcmp(argv[i], "-r") == 0)
        {
          cfg->restore = true;
          continue;
        }

      if (strcmp(argv[i], "--interactive") == 0)
        {
          cfg->interactive = true;
          continue;
        }

      if (i + 1 >= argc)
        {
          printf("ERROR: missing value for %s\n", argv[i]);
          return -EINVAL;
        }

      if (strcmp(argv[i], "-c") == 0)
        {
          cfg->case_id = argv[++i];
        }
      else if (strcmp(argv[i], "--key0") == 0)
        {
          cfg->key0_path = argv[++i];
        }
      else if (strcmp(argv[i], "--key1") == 0)
        {
          cfg->key1_path = argv[++i];
        }
      else if (strcmp(argv[i], "--led") == 0)
        {
          cfg->led_path = argv[++i];
        }
      else if (strcmp(argv[i], "--out") == 0)
        {
          cfg->out_path = argv[++i];
        }
      else if (strcmp(argv[i], "--in") == 0)
        {
          cfg->in_path = argv[++i];
        }
      else if (strcmp(argv[i], "-p") == 0)
        {
          ret = parse_u32_arg("period", argv[++i], &cfg->period_us);
          if (ret < 0)
            {
              return ret;
            }
        }
      else if (strcmp(argv[i], "-n") == 0)
        {
          ret = parse_u32_arg("count", argv[++i], &cfg->count);
          if (ret < 0)
            {
              return ret;
            }
        }
      else if (strcmp(argv[i], "-H") == 0)
        {
          ret = parse_u32_arg("hold time", argv[++i], &cfg->hold_ms);
          if (ret < 0)
            {
              return ret;
            }
        }
      else if (strcmp(argv[i], "-A") == 0)
        {
          ret = parse_u32_arg("arm time", argv[++i], &cfg->arm_ms);
          if (ret < 0)
            {
              return ret;
            }
        }
      else if (strcmp(argv[i], "-T") == 0)
        {
          ret = parse_u32_arg("timeout", argv[++i],
                              &cfg->irq_timeout_ms);
          if (ret < 0)
            {
              return ret;
            }
        }
      else
        {
          printf("ERROR: unsupported option %s\n", argv[i]);
          return -EINVAL;
        }
    }

  if (!case_is_supported(cfg->case_id))
    {
      printf("ERROR: unsupported case %s\n", cfg->case_id);
      return -EINVAL;
    }

  if (cfg->count == 0 || cfg->period_us == 0)
    {
      printf("ERROR: period and count must be non-zero\n");
      return -EINVAL;
    }

  return 0;
}

/****************************************************************************
 * Name: pintype_name
 *
 * Description:
 *   Return a constant string name for a GPIO pin type, for logging.
 *
 * Input Parameters:
 *   type - GPIO pin type enumerator.
 *
 * Returned Value:
 *   Constant string naming the pin type, or "GPIO_UNKNOWN_PIN" if not
 *   recognized.
 *
 ****************************************************************************/

static const char *pintype_name(enum gpio_pintype_e type)
{
  switch (type)
    {
      case GPIO_INPUT_PIN:
        return "GPIO_INPUT_PIN";
      case GPIO_INPUT_PIN_PULLUP:
        return "GPIO_INPUT_PIN_PULLUP";
      case GPIO_INPUT_PIN_PULLDOWN:
        return "GPIO_INPUT_PIN_PULLDOWN";
      case GPIO_OUTPUT_PIN:
        return "GPIO_OUTPUT_PIN";
      case GPIO_OUTPUT_PIN_OPENDRAIN:
        return "GPIO_OUTPUT_PIN_OPENDRAIN";
      case GPIO_INTERRUPT_PIN:
        return "GPIO_INTERRUPT_PIN";
      case GPIO_INTERRUPT_HIGH_PIN:
        return "GPIO_INTERRUPT_HIGH_PIN";
      case GPIO_INTERRUPT_LOW_PIN:
        return "GPIO_INTERRUPT_LOW_PIN";
      case GPIO_INTERRUPT_RISING_PIN:
        return "GPIO_INTERRUPT_RISING_PIN";
      case GPIO_INTERRUPT_FALLING_PIN:
        return "GPIO_INTERRUPT_FALLING_PIN";
      case GPIO_INTERRUPT_BOTH_PIN:
        return "GPIO_INTERRUPT_BOTH_PIN";
      case GPIO_INTERRUPT_PIN_WAKEUP:
        return "GPIO_INTERRUPT_PIN_WAKEUP";
      case GPIO_INTERRUPT_HIGH_PIN_WAKEUP:
        return "GPIO_INTERRUPT_HIGH_PIN_WAKEUP";
      case GPIO_INTERRUPT_LOW_PIN_WAKEUP:
        return "GPIO_INTERRUPT_LOW_PIN_WAKEUP";
      case GPIO_INTERRUPT_RISING_PIN_WAKEUP:
        return "GPIO_INTERRUPT_RISING_PIN_WAKEUP";
      case GPIO_INTERRUPT_FALLING_PIN_WAKEUP:
        return "GPIO_INTERRUPT_FALLING_PIN_WAKEUP";
      case GPIO_INTERRUPT_BOTH_PIN_WAKEUP:
        return "GPIO_INTERRUPT_BOTH_PIN_WAKEUP";
      default:
        return "GPIO_UNKNOWN_PIN";
    }
}

/****************************************************************************
 * Name: gpio_open_handle
 *
 * Description:
 *   Open a GPIO character device and cache its current pin type so it can
 *   optionally be restored when the handle is closed.
 *
 * Input Parameters:
 *   gpio     - Handle structure to initialize.
 *   path     - Device path to open (for example "/dev/gpio8").
 *   optional - If true, suppress the error message when the open fails.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int gpio_open_handle(struct gpio_handle_s *gpio, const char *path,
                            bool optional)
{
  int ret;

  gpio->path = path;
  gpio->fd = open(path, O_RDWR);
  gpio->original_type_valid = false;

  if (gpio->fd < 0)
    {
      ret = -errno;
      if (!optional)
        {
          printf("ERROR: failed to open %s: errno=%d\n", path, errno);
        }

      return ret;
    }

  ret = gpio_get_type(gpio, &gpio->original_type);
  if (ret < 0)
    {
      printf("ERROR: %s is not a usable GPIO device: ret=%d\n", path, ret);
      close(gpio->fd);
      gpio->fd = -1;
      return ret;
    }

  gpio->original_type_valid = true;
  return 0;
}

/****************************************************************************
 * Name: gpio_close_handle
 *
 * Description:
 *   Optionally restore the cached original pin type and close the GPIO
 *   device. Safe to call on an unopened or already-closed handle.
 *
 * Input Parameters:
 *   gpio             - Handle to close.
 *   restore_original - If true, restore the pin type captured at open.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void gpio_close_handle(struct gpio_handle_s *gpio,
                              bool restore_original)
{
  int ret;

  if (gpio == NULL || gpio->fd < 0)
    {
      return;
    }

  if (restore_original && gpio->original_type_valid)
    {
      ret = gpio_set_type(gpio, gpio->original_type);
      if (ret < 0)
        {
          printf("WARNING: failed to restore %s to %s: ret=%d\n",
                 gpio->path, pintype_name(gpio->original_type), ret);
        }
    }

  close(gpio->fd);
  gpio->fd = -1;
}

/****************************************************************************
 * Name: gpio_get_type
 *
 * Description:
 *   Query the current pin type of an opened GPIO via GPIOC_PINTYPE.
 *
 * Input Parameters:
 *   gpio - Opened GPIO handle.
 *   type - Receives the current pin type.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int gpio_get_type(struct gpio_handle_s *gpio,
                         enum gpio_pintype_e *type)
{
  int ret;

  ret = ioctl(gpio->fd, GPIOC_PINTYPE,
              (unsigned long)((uintptr_t)type));
  if (ret < 0)
    {
      return -errno;
    }

  return 0;
}

/****************************************************************************
 * Name: gpio_set_type
 *
 * Description:
 *   Set the pin type of an opened GPIO via GPIOC_SETPINTYPE.
 *
 * Input Parameters:
 *   gpio - Opened GPIO handle.
 *   type - Pin type to apply.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int gpio_set_type(struct gpio_handle_s *gpio,
                         enum gpio_pintype_e type)
{
  int ret;

  ret = ioctl(gpio->fd, GPIOC_SETPINTYPE, (unsigned long)type);
  if (ret < 0)
    {
      ret = -errno;
      printf("ERROR: %s set type %s failed: ret=%d\n",
             gpio->path, pintype_name(type), ret);
      return ret;
    }

  return 0;
}

/****************************************************************************
 * Name: gpio_write_value
 *
 * Description:
 *   Drive the output level of an opened GPIO via GPIOC_WRITE.
 *
 * Input Parameters:
 *   gpio  - Opened GPIO handle.
 *   value - Level to drive (true for high, false for low).
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int gpio_write_value(struct gpio_handle_s *gpio, bool value)
{
  int ret;

  ret = ioctl(gpio->fd, GPIOC_WRITE, (unsigned long)value);
  if (ret < 0)
    {
      ret = -errno;
      printf("ERROR: %s write %u failed: ret=%d\n",
             gpio->path, value ? 1 : 0, ret);
      return ret;
    }

  return 0;
}

/****************************************************************************
 * Name: gpio_read_value
 *
 * Description:
 *   Read the current input level of an opened GPIO via GPIOC_READ.
 *
 * Input Parameters:
 *   gpio  - Opened GPIO handle.
 *   value - Receives the level read (true for high, false for low).
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int gpio_read_value(struct gpio_handle_s *gpio, bool *value)
{
  int ret;

  ret = ioctl(gpio->fd, GPIOC_READ,
              (unsigned long)((uintptr_t)value));
  if (ret < 0)
    {
      ret = -errno;
      printf("ERROR: %s read failed: ret=%d\n", gpio->path, ret);
      return ret;
    }

  return 0;
}

/****************************************************************************
 * Name: stable_read
 *
 * Description:
 *   Sample a GPIO STABLE_READ_SAMPLES times and confirm the level is
 *   stable across all samples, rejecting glitches on a software-only read.
 *
 * Input Parameters:
 *   gpio  - Opened GPIO handle to read.
 *   value - Receives the stable level on success.
 *
 * Returned Value:
 *   Zero on success; -EAGAIN if the level was not stable; a negated errno
 *   value on a read failure.
 *
 ****************************************************************************/

static int stable_read(struct gpio_handle_s *gpio, bool *value)
{
  bool first;
  bool current;
  int i;
  int ret;

  ret = gpio_read_value(gpio, &first);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 1; i < STABLE_READ_SAMPLES; i++)
    {
      usleep(STABLE_READ_DELAY_US);
      ret = gpio_read_value(gpio, &current);
      if (ret < 0)
        {
          return ret;
        }

      if (current != first)
        {
          printf("ERROR: %s unstable read: first=%u current=%u sample=%d\n",
                 gpio->path, first ? 1 : 0, current ? 1 : 0, i);
          return -EAGAIN;
        }
    }

  *value = first;
  return 0;
}

/****************************************************************************
 * Name: led_set_active_low
 *
 * Description:
 *   Drive an active-low LED, writing the inverted level so that "on"
 *   produces a low output.
 *
 * Input Parameters:
 *   led - Opened LED GPIO handle.
 *   on  - True to light the LED, false to turn it off.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int led_set_active_low(struct gpio_handle_s *led, bool on)
{
  return gpio_write_value(led, on ? false : true);
}

/****************************************************************************
 * Name: led_blink
 *
 * Description:
 *   Configure the LED as a push-pull output and blink it the requested
 *   number of times.
 *
 * Input Parameters:
 *   led   - Opened LED GPIO handle.
 *   count - Number of on/off blink cycles.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int led_blink(struct gpio_handle_s *led, unsigned int count)
{
  unsigned int i;
  int ret;

  ret = gpio_set_type(led, GPIO_OUTPUT_PIN);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < count; i++)
    {
      ret = led_set_active_low(led, true);
      if (ret < 0)
        {
          return ret;
        }

      usleep(LED_BLINK_US);

      ret = led_set_active_low(led, false);
      if (ret < 0)
        {
          return ret;
        }

      usleep(LED_BLINK_US);
    }

  return 0;
}

/****************************************************************************
 * Name: wait_for_key_state
 *
 * Description:
 *   Poll a key GPIO until it reaches the expected level or the timeout
 *   elapses, printing the prompt before polling begins.
 *
 * Input Parameters:
 *   key        - Opened key GPIO handle.
 *   expected   - Level to wait for.
 *   timeout_ms - Maximum time to wait, in milliseconds.
 *   prompt     - Message printed before polling.
 *
 * Returned Value:
 *   Zero once the expected level is seen; -ETIMEDOUT on timeout; a negated
 *   errno value on a read failure.
 *
 ****************************************************************************/

static int wait_for_key_state(struct gpio_handle_s *key, bool expected,
                              uint32_t timeout_ms, const char *prompt)
{
  uint32_t elapsed_ms = 0;
  bool value;
  int ret;

  printf("%s\n", prompt);
  while (elapsed_ms < timeout_ms)
    {
      ret = gpio_read_value(key, &value);
      if (ret < 0)
        {
          return ret;
        }

      if (value == expected)
        {
          return 0;
        }

      usleep(KEY_POLL_INTERVAL_US);
      elapsed_ms += KEY_POLL_INTERVAL_US / 1000;
    }

  printf("ERROR: timeout waiting for %s to become %u\n",
         key->path, expected ? 1 : 0);
  return -ETIMEDOUT;
}

/****************************************************************************
 * Name: path_equal
 *
 * Description:
 *   Compare two device path strings for equality, treating NULL as not
 *   equal.
 *
 * Input Parameters:
 *   lhs - First path (may be NULL).
 *   rhs - Second path (may be NULL).
 *
 * Returned Value:
 *   True if both are non-NULL and equal, false otherwise.
 *
 ****************************************************************************/

static bool path_equal(const char *lhs, const char *rhs)
{
  return lhs != NULL && rhs != NULL && strcmp(lhs, rhs) == 0;
}

/****************************************************************************
 * Name: case_uses_pair
 *
 * Description:
 *   Test whether the selected case drives the out/in GPIO pair (and so
 *   requires those two paths to differ).
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   True if the case uses the out/in pair, false otherwise.
 *
 ****************************************************************************/

static bool case_uses_pair(const struct app_config_s *cfg)
{
  return strcmp(cfg->case_id, CASE_ALL) == 0 ||
         strcmp(cfg->case_id, CASE_PAIR) == 0 ||
         strcmp(cfg->case_id, CASE_001) == 0 ||
         strcmp(cfg->case_id, CASE_002) == 0 ||
         strcmp(cfg->case_id, CASE_003) == 0 ||
         strcmp(cfg->case_id, CASE_004) == 0 ||
         strcmp(cfg->case_id, CASE_005) == 0;
}

/****************************************************************************
 * Name: validate_config_paths
 *
 * Description:
 *   Validate the configured device paths: the out/in pair must differ for
 *   pair cases, and the recorded paths must all be unique for the record
 *   cases (all/010).
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero if the paths are valid; -EINVAL otherwise.
 *
 ****************************************************************************/

static int validate_config_paths(const struct app_config_s *cfg)
{
  const char *const record_labels[] =
  {
    "key0",
    "key1",
    "led",
    "out",
    "in",
  };

  const char *const record_paths[] =
  {
    cfg->key0_path,
    cfg->key1_path,
    cfg->led_path,
    cfg->out_path,
    cfg->in_path,
  };

  size_t i;
  int ret;

  if (case_uses_pair(cfg) && path_equal(cfg->out_path, cfg->in_path))
    {
      printf("ERROR: --out and --in must be different for case %s\n",
             cfg->case_id);
      return -EINVAL;
    }

  if (strcmp(cfg->case_id, CASE_ALL) == 0 ||
      strcmp(cfg->case_id, CASE_010) == 0)
    {
      for (i = 0; i < sizeof(record_paths) / sizeof(record_paths[0]); i++)
        {
          ret = validate_record_path_unique(record_labels[i],
                                            record_paths[i],
                                            record_labels,
                                            record_paths,
                                            i);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  return 0;
}

/****************************************************************************
 * Name: validate_record_path_unique
 *
 * Description:
 *   Check that a path does not collide with any of the earlier entries in
 *   the record path list.
 *
 * Input Parameters:
 *   label  - Label of the path being checked, for error messages.
 *   path   - Path being checked.
 *   labels - Array of all record labels.
 *   paths  - Array of all record paths.
 *   count  - Number of earlier entries to compare against.
 *
 * Returned Value:
 *   Zero if unique; -EINVAL if a duplicate is found.
 *
 ****************************************************************************/

static int validate_record_path_unique(const char *label,
                                       const char *path,
                                       const char *const *labels,
                                       const char *const *paths,
                                       size_t count)
{
  size_t i;

  for (i = 0; i < count; i++)
    {
      if (path_equal(path, paths[i]))
        {
          printf("ERROR: GPIO-010 record paths must be unique: "
                 "%s and %s both use %s\n",
                 labels[i], label, path);
          return -EINVAL;
        }
    }

  return 0;
}

/****************************************************************************
 * Name: wait_gpio_signal
 *
 * Description:
 *   Wait for a GPIO interrupt delivered as a signal, up to the given
 *   timeout, using sigtimedwait.
 *
 * Input Parameters:
 *   signo      - Signal number to wait for.
 *   timeout_ms - Maximum time to wait, in milliseconds.
 *
 * Returned Value:
 *   Zero when the signal arrives; a negated errno value on timeout or
 *   failure (-EAGAIN on timeout).
 *
 ****************************************************************************/

static int wait_gpio_signal(int signo, uint32_t timeout_ms)
{
  struct timespec timeout;
  sigset_t set;
  siginfo_t info;
  int ret;

  sigemptyset(&set);
  sigaddset(&set, signo);

  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_nsec = (timeout_ms % 1000) * 1000000;

  ret = sigtimedwait(&set, &info, &timeout);
  if (ret < 0)
    {
      ret = -errno;
      printf("ERROR: timeout or failure waiting for GPIO signal: ret=%d\n",
             ret);
      return ret;
    }

  return 0;
}

/****************************************************************************
 * Name: register_gpio_signal
 *
 * Description:
 *   Register a signal event to be raised on a GPIO interrupt via
 *   GPIOC_REGISTER.
 *
 * Input Parameters:
 *   gpio  - Opened GPIO handle configured as an interrupt pin.
 *   event - Signal event describing how to notify.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int register_gpio_signal(struct gpio_handle_s *gpio,
                                struct sigevent *event)
{
  int ret;

  ret = ioctl(gpio->fd, GPIOC_REGISTER,
              (unsigned long)((uintptr_t)event));
  if (ret < 0)
    {
      ret = -errno;
      printf("ERROR: %s GPIOC_REGISTER failed: ret=%d\n", gpio->path, ret);
      return ret;
    }

  return 0;
}

/****************************************************************************
 * Name: unregister_gpio_signal
 *
 * Description:
 *   Remove a previously registered GPIO interrupt notification via
 *   GPIOC_UNREGISTER.
 *
 * Input Parameters:
 *   gpio - Opened GPIO handle previously registered.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int unregister_gpio_signal(struct gpio_handle_s *gpio)
{
  int ret;

  ret = ioctl(gpio->fd, GPIOC_UNREGISTER, 0);
  if (ret < 0)
    {
      ret = -errno;
      printf("ERROR: %s GPIOC_UNREGISTER failed: ret=%d\n",
             gpio->path, ret);
      return ret;
    }

  return 0;
}

/****************************************************************************
 * Name: run_key_irq_case
 *
 * Description:
 *   Configure a key GPIO as an interrupt pin, wait for one edge delivered
 *   as a signal, then blink the LED. Used by the interactive board checks.
 *
 * Input Parameters:
 *   key         - Opened key GPIO handle.
 *   led         - Opened LED GPIO handle for feedback.
 *   irq_type    - Interrupt pin type (rising/falling edge).
 *   timeout_ms  - Maximum time to wait for the edge.
 *   prompt      - Message printed before waiting (e.g. "Press KEY0").
 *   case_id     - Identifier used in log lines.
 *   blink_count - Number of LED blinks on success.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure or timeout.
 *
 ****************************************************************************/

static int run_key_irq_case(struct gpio_handle_s *key,
                            struct gpio_handle_s *led,
                            enum gpio_pintype_e irq_type,
                            uint32_t timeout_ms,
                            const char *prompt,
                            const char *case_id,
                            unsigned int blink_count)
{
  struct sigevent event;
  sigset_t set;
  sigset_t oldset;
  int signo = SIGUSR1;
  bool mask_changed = false;
  bool registered = false;
  int ret;

  printf("[%s] START key=%s irq=%s\n",
         case_id, key->path, pintype_name(irq_type));

  sigemptyset(&set);
  sigaddset(&set, signo);
  ret = sigprocmask(SIG_BLOCK, &set, &oldset);
  if (ret < 0)
    {
      return -errno;
    }

  mask_changed = true;

  ret = gpio_set_type(key, irq_type);
  if (ret < 0)
    {
      goto out;
    }

  memset(&event, 0, sizeof(event));
  event.sigev_notify = SIGEV_SIGNAL;
  event.sigev_signo = signo;

  ret = register_gpio_signal(key, &event);
  if (ret < 0)
    {
      goto out;
    }

  registered = true;
  printf("%s\n", prompt);

  ret = wait_gpio_signal(signo, timeout_ms);
  if (ret < 0)
    {
      goto out;
    }

  ret = unregister_gpio_signal(key);
  if (ret < 0)
    {
      goto out;
    }

  registered = false;

  ret = led_blink(led, blink_count);

out:
  if (registered)
    {
      unregister_gpio_signal(key);
    }

  if (mask_changed)
    {
      sigprocmask(SIG_SETMASK, &oldset, NULL);
    }

  return ret;
}

/****************************************************************************
 * Name: run_board_case
 *
 * Description:
 *   Board self-check: blink the LED push-pull, verify both keys read high
 *   when idle, optionally run interactive key interrupt checks, and verify
 *   the LED in open-drain mode.
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero if all checks pass; a negated errno value on the first failure.
 *
 ****************************************************************************/

static int run_board_case(const struct app_config_s *cfg)
{
  struct gpio_handle_s key0;
  struct gpio_handle_s key1;
  struct gpio_handle_s led;
  bool value;
  int ret;

  printf("[GPIO-board] START key0=%s key1=%s led=%s\n",
         cfg->key0_path, cfg->key1_path, cfg->led_path);

  ret = gpio_open_handle(&key0, cfg->key0_path, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = gpio_open_handle(&key1, cfg->key1_path, false);
  if (ret < 0)
    {
      gpio_close_handle(&key0, cfg->restore);
      return ret;
    }

  ret = gpio_open_handle(&led, cfg->led_path, false);
  if (ret < 0)
    {
      gpio_close_handle(&key1, cfg->restore);
      gpio_close_handle(&key0, cfg->restore);
      return ret;
    }

  ret = gpio_set_type(&led, GPIO_OUTPUT_PIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  printf("[GPIO-board] LED push-pull active-low blink\n");
  ret = led_blink(&led, 3);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = gpio_set_type(&key0, GPIO_INPUT_PIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = gpio_set_type(&key1, GPIO_INPUT_PIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = stable_read(&key0, &value);
  if (ret < 0)
    {
      goto out_fail;
    }

  printf("[GPIO-board] KEY0 active-low raw idle value=%u\n",
         value ? 1 : 0);
  if (!value)
    {
      printf("[GPIO-board] FAIL KEY0 idle expected=1 actual=0\n");
      ret = -EIO;
      goto out_fail;
    }

  ret = stable_read(&key1, &value);
  if (ret < 0)
    {
      goto out_fail;
    }

  printf("[GPIO-board] KEY1 active-low raw idle value=%u\n",
         value ? 1 : 0);
  if (!value)
    {
      printf("[GPIO-board] FAIL KEY1 idle expected=1 actual=0\n");
      ret = -EIO;
      goto out_fail;
    }

  if (cfg->interactive)
    {
      ret = wait_for_key_state(&key0, true, cfg->irq_timeout_ms,
                               "[GPIO-board] Release KEY0");
      if (ret < 0)
        {
          goto out_fail;
        }

      ret = run_key_irq_case(&key0, &led, GPIO_INTERRUPT_FALLING_PIN,
                             cfg->irq_timeout_ms,
                             "[GPIO-board] Press KEY0",
                             "GPIO-board-KEY0-FALLING", 1);
      if (ret < 0)
        {
          goto out_fail;
        }

      ret = run_key_irq_case(&key0, &led, GPIO_INTERRUPT_RISING_PIN,
                             cfg->irq_timeout_ms,
                             "[GPIO-board] Release KEY0",
                             "GPIO-board-KEY0-RISING", 2);
      if (ret < 0)
        {
          goto out_fail;
        }

      ret = wait_for_key_state(&key1, true, cfg->irq_timeout_ms,
                               "[GPIO-board] Release KEY1");
      if (ret < 0)
        {
          goto out_fail;
        }

      ret = run_key_irq_case(&key1, &led, GPIO_INTERRUPT_FALLING_PIN,
                             cfg->irq_timeout_ms,
                             "[GPIO-board] Press KEY1",
                             "GPIO-board-KEY1-FALLING", 1);
      if (ret < 0)
        {
          goto out_fail;
        }

      ret = run_key_irq_case(&key1, &led, GPIO_INTERRUPT_RISING_PIN,
                             cfg->irq_timeout_ms,
                             "[GPIO-board] Release KEY1",
                             "GPIO-board-KEY1-RISING", 2);
      if (ret < 0)
        {
          goto out_fail;
        }
    }
  else
    {
      printf("[GPIO-board] SKIP manual key interrupt checks; "
             "use --interactive\n");
    }

  printf("[GPIO-board] LED open-drain active-low check\n");
  ret = gpio_set_type(&led, GPIO_OUTPUT_PIN_OPENDRAIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = led_set_active_low(&led, true);
  if (ret < 0)
    {
      goto out_fail;
    }

  usleep(LED_BLINK_US * 3);

  ret = led_set_active_low(&led, false);
  if (ret < 0)
    {
      goto out_fail;
    }

  printf("[GPIO-board] PASS board GPIO checks completed\n");
  gpio_close_handle(&led, cfg->restore);
  gpio_close_handle(&key1, cfg->restore);
  gpio_close_handle(&key0, cfg->restore);
  return 0;

out_fail:
  gpio_close_handle(&led, cfg->restore);
  gpio_close_handle(&key1, cfg->restore);
  gpio_close_handle(&key0, cfg->restore);
  return ret;
}

/****************************************************************************
 * Name: run_case_001_pair
 *
 * Description:
 *   GPIO-001: drive the output as push-pull and verify the wired input
 *   follows the alternating level for the configured number of samples.
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero on success; -EIO on a level mismatch; a negated errno value on a
 *   GPIO failure.
 *
 ****************************************************************************/

static int run_case_001_pair(const struct app_config_s *cfg)
{
  struct gpio_handle_s out;
  struct gpio_handle_s in;
  uint32_t i;
  bool expected;
  bool actual;
  int ret;

  printf("[GPIO-001] START out=%s in=%s period_us=%lu count=%lu\n",
         cfg->out_path, cfg->in_path, (unsigned long)cfg->period_us,
         (unsigned long)cfg->count);

  ret = gpio_open_handle(&out, cfg->out_path, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = gpio_open_handle(&in, cfg->in_path, false);
  if (ret < 0)
    {
      gpio_close_handle(&out, cfg->restore);
      return ret;
    }

  ret = gpio_set_type(&out, GPIO_OUTPUT_PIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = gpio_set_type(&in, GPIO_INPUT_PIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  for (i = 0; i < cfg->count; i++)
    {
      expected = (i & 1) != 0;
      ret = gpio_write_value(&out, expected);
      if (ret < 0)
        {
          goto out_fail;
        }

      usleep(cfg->period_us / 2);

      ret = gpio_read_value(&in, &actual);
      if (ret < 0)
        {
          goto out_fail;
        }

      if (actual != expected)
        {
          printf("[GPIO-001] FAIL sample=%lu expected=%u actual=%u\n",
                 (unsigned long)i, expected ? 1 : 0, actual ? 1 : 0);
          ret = -EIO;
          goto out_fail;
        }

      if (cfg->verbose)
        {
          printf("[GPIO-001] sample=%lu value=%u\n",
                 (unsigned long)i, actual ? 1 : 0);
        }
    }

  printf("[GPIO-001] PASS input followed push-pull output\n");
  gpio_close_handle(&in, cfg->restore);
  gpio_close_handle(&out, cfg->restore);
  return 0;

out_fail:
  gpio_close_handle(&in, cfg->restore);
  gpio_close_handle(&out, cfg->restore);
  return ret;
}

/****************************************************************************
 * Name: run_case_002_pair
 *
 * Description:
 *   GPIO-002: with the output open-drain and the input pulled up, verify
 *   driving low pulls the line low and releasing lets the pull-up raise it.
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero on success; -EIO on an unexpected level; a negated errno value on
 *   a GPIO failure.
 *
 ****************************************************************************/

static int run_case_002_pair(const struct app_config_s *cfg)
{
  struct gpio_handle_s out;
  struct gpio_handle_s in;
  bool value = false;
  int ret;

  printf("[GPIO-002] START out=%s in=%s\n", cfg->out_path, cfg->in_path);

  ret = gpio_open_handle(&out, cfg->out_path, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = gpio_open_handle(&in, cfg->in_path, false);
  if (ret < 0)
    {
      gpio_close_handle(&out, cfg->restore);
      return ret;
    }

  ret = gpio_set_type(&out, GPIO_OUTPUT_PIN_OPENDRAIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = gpio_set_type(&in, GPIO_INPUT_PIN_PULLUP);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = gpio_write_value(&out, false);
  if (ret < 0)
    {
      goto out_fail;
    }

  usleep(STABLE_READ_DELAY_US);
  ret = gpio_read_value(&in, &value);
  if (ret < 0 || value)
    {
      printf("[GPIO-002] FAIL drive-low expected=0 actual=%u\n",
             value ? 1 : 0);
      ret = ret < 0 ? ret : -EIO;
      goto out_fail;
    }

  ret = gpio_write_value(&out, true);
  if (ret < 0)
    {
      goto out_fail;
    }

  usleep(STABLE_READ_DELAY_US);
  ret = gpio_read_value(&in, &value);
  if (ret < 0 || !value)
    {
      printf("[GPIO-002] FAIL release-high expected=1 actual=%u\n",
             value ? 1 : 0);
      ret = ret < 0 ? ret : -EIO;
      goto out_fail;
    }

  printf("[GPIO-002] PASS open-drain low/release checks completed\n");
  gpio_close_handle(&in, cfg->restore);
  gpio_close_handle(&out, cfg->restore);
  return 0;

out_fail:
  gpio_close_handle(&in, cfg->restore);
  gpio_close_handle(&out, cfg->restore);
  return ret;
}

/****************************************************************************
 * Name: run_case_003_pair
 *
 * Description:
 *   GPIO-003: verify the input internal pull-up reads high and pull-down
 *   reads low, then verify an external drive on the output overrides the
 *   internal pull in both directions.
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero on success; -EIO on an unexpected level; a negated errno value on
 *   a GPIO failure.
 *
 ****************************************************************************/

static int run_case_003_pair(const struct app_config_s *cfg)
{
  struct gpio_handle_s out;
  struct gpio_handle_s in;
  bool value = false;
  int ret;

  printf("[GPIO-003] START out=%s in=%s\n", cfg->out_path, cfg->in_path);

  ret = gpio_open_handle(&out, cfg->out_path, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = gpio_open_handle(&in, cfg->in_path, false);
  if (ret < 0)
    {
      gpio_close_handle(&out, cfg->restore);
      return ret;
    }

  ret = gpio_set_type(&out, GPIO_INPUT_PIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = gpio_set_type(&in, GPIO_INPUT_PIN_PULLUP);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = stable_read(&in, &value);
  if (ret < 0 || !value)
    {
      printf("[GPIO-003] FAIL pull-up expected=1 actual=%u\n",
             value ? 1 : 0);
      ret = ret < 0 ? ret : -EIO;
      goto out_fail;
    }

  ret = gpio_set_type(&in, GPIO_INPUT_PIN_PULLDOWN);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = stable_read(&in, &value);
  if (ret < 0 || value)
    {
      printf("[GPIO-003] FAIL pull-down expected=0 actual=%u\n",
             value ? 1 : 0);
      ret = ret < 0 ? ret : -EIO;
      goto out_fail;
    }

  ret = gpio_set_type(&out, GPIO_OUTPUT_PIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = gpio_set_type(&in, GPIO_INPUT_PIN_PULLDOWN);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = gpio_write_value(&out, true);
  if (ret < 0)
    {
      goto out_fail;
    }

  usleep(STABLE_READ_DELAY_US);
  ret = gpio_read_value(&in, &value);
  if (ret < 0 || !value)
    {
      printf("[GPIO-003] FAIL external high override expected=1 actual=%u\n",
             value ? 1 : 0);
      ret = ret < 0 ? ret : -EIO;
      goto out_fail;
    }

  ret = gpio_set_type(&in, GPIO_INPUT_PIN_PULLUP);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = gpio_write_value(&out, false);
  if (ret < 0)
    {
      goto out_fail;
    }

  usleep(STABLE_READ_DELAY_US);
  ret = gpio_read_value(&in, &value);
  if (ret < 0 || value)
    {
      printf("[GPIO-003] FAIL external low override expected=0 actual=%u\n",
             value ? 1 : 0);
      ret = ret < 0 ? ret : -EIO;
      goto out_fail;
    }

  printf("[GPIO-003] PASS input pull-up/down checks completed\n");
  gpio_close_handle(&in, cfg->restore);
  gpio_close_handle(&out, cfg->restore);
  return 0;

out_fail:
  gpio_close_handle(&in, cfg->restore);
  gpio_close_handle(&out, cfg->restore);
  return ret;
}

/****************************************************************************
 * Name: run_irq_pair_case
 *
 * Description:
 *   Shared GPIO interrupt loop: drive the output to the initial level, arm
 *   the input as an interrupt pin, then repeatedly toggle to the trigger
 *   level and confirm an interrupt signal is received each time.
 *
 * Input Parameters:
 *   cfg           - Active configuration.
 *   irq_type      - Interrupt pin type (rising/falling edge).
 *   initial_level - Idle output level before each trigger.
 *   trigger_level - Output level that produces the active edge.
 *   case_id       - Identifier used in log lines.
 *
 * Returned Value:
 *   Zero if all expected interrupts arrive; a negated errno value on
 *   failure or timeout.
 *
 ****************************************************************************/

static int run_irq_pair_case(const struct app_config_s *cfg,
                             enum gpio_pintype_e irq_type,
                             bool initial_level, bool trigger_level,
                             const char *case_id)
{
  struct gpio_handle_s out;
  struct gpio_handle_s in;
  struct sigevent event;
  sigset_t set;
  sigset_t oldset;
  int signo = SIGUSR1;
  bool mask_changed = false;
  bool registered = false;
  int ret;
  uint32_t i;

  printf("[%s] START out=%s in=%s irq=%s\n",
         case_id, cfg->out_path, cfg->in_path, pintype_name(irq_type));

  ret = gpio_open_handle(&out, cfg->out_path, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = gpio_open_handle(&in, cfg->in_path, false);
  if (ret < 0)
    {
      gpio_close_handle(&out, cfg->restore);
      return ret;
    }

  sigemptyset(&set);
  sigaddset(&set, signo);
  ret = sigprocmask(SIG_BLOCK, &set, &oldset);
  if (ret < 0)
    {
      ret = -errno;
      goto out_fail;
    }

  mask_changed = true;

  ret = gpio_set_type(&out, GPIO_OUTPUT_PIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = gpio_write_value(&out, initial_level);
  if (ret < 0)
    {
      goto out_fail;
    }

  usleep(STABLE_READ_DELAY_US);

  ret = gpio_set_type(&in, irq_type);
  if (ret < 0)
    {
      goto out_fail;
    }

  memset(&event, 0, sizeof(event));
  event.sigev_notify = SIGEV_SIGNAL;
  event.sigev_signo = signo;

  ret = register_gpio_signal(&in, &event);
  if (ret < 0)
    {
      goto out_fail;
    }

  registered = true;

  /* Let the IRQ finish arming before the first edge, else it is missed. */

  usleep(IRQ_ARM_SETTLE_US);

  for (i = 0; i < cfg->count; i++)
    {
      ret = gpio_write_value(&out, trigger_level);
      if (ret < 0)
        {
          goto out_fail;
        }

      ret = wait_gpio_signal(signo, cfg->irq_timeout_ms);
      if (ret < 0)
        {
          goto out_fail;
        }

      ret = gpio_write_value(&out, initial_level);
      if (ret < 0)
        {
          goto out_fail;
        }

      usleep(STABLE_READ_DELAY_US);

      if (cfg->verbose)
        {
          printf("[%s] irq sample=%lu\n", case_id, (unsigned long)i);
        }
    }

  ret = unregister_gpio_signal(&in);
  if (ret < 0)
    {
      goto out_fail;
    }

  registered = false;

  if (mask_changed)
    {
      sigprocmask(SIG_SETMASK, &oldset, NULL);
      mask_changed = false;
    }

  printf("[%s] PASS %lu interrupts received\n",
         case_id, (unsigned long)cfg->count);
  gpio_close_handle(&in, cfg->restore);
  gpio_close_handle(&out, cfg->restore);
  return 0;

out_fail:
  if (registered)
    {
      unregister_gpio_signal(&in);
    }

  if (mask_changed)
    {
      sigprocmask(SIG_SETMASK, &oldset, NULL);
    }

  gpio_close_handle(&in, cfg->restore);
  gpio_close_handle(&out, cfg->restore);
  return ret;
}

/****************************************************************************
 * Name: run_case_004_pair
 *
 * Description:
 *   GPIO-004: rising-edge interrupt test (output idles low, triggers high).
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure or timeout.
 *
 ****************************************************************************/

static int run_case_004_pair(const struct app_config_s *cfg)
{
  return run_irq_pair_case(cfg, GPIO_INTERRUPT_RISING_PIN,
                           false, true, "GPIO-004");
}

/****************************************************************************
 * Name: run_case_005_pair
 *
 * Description:
 *   GPIO-005: falling-edge interrupt test (output idles high, triggers
 *   low).
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure or timeout.
 *
 ****************************************************************************/

static int run_case_005_pair(const struct app_config_s *cfg)
{
  return run_irq_pair_case(cfg, GPIO_INTERRUPT_FALLING_PIN,
                           true, false, "GPIO-005");
}

/****************************************************************************
 * Name: run_pair_case
 *
 * Description:
 *   Run the full out/in pair suite in sequence: 001, 003, 004, 005, 002.
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero if every case passes; the first negated errno value otherwise.
 *
 ****************************************************************************/

static int run_pair_case(const struct app_config_s *cfg)
{
  int ret;

  ret = run_case_001_pair(cfg);
  if (ret < 0)
    {
      return ret;
    }

  ret = run_case_003_pair(cfg);
  if (ret < 0)
    {
      return ret;
    }

  ret = run_case_004_pair(cfg);
  if (ret < 0)
    {
      return ret;
    }

  ret = run_case_005_pair(cfg);
  if (ret < 0)
    {
      return ret;
    }

  return run_case_002_pair(cfg);
}

/****************************************************************************
 * Name: run_case_007_measure
 *
 * Description:
 *   GPIO-007: hold the output high for hold_ms so drive strength can be
 *   measured externally with a known load. No software pass/fail.
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on a GPIO failure.
 *
 ****************************************************************************/

static int run_case_007_measure(const struct app_config_s *cfg)
{
  struct gpio_handle_s out;
  int ret;

  printf("[GPIO-007] START out=%s hold_ms=%lu\n",
         cfg->out_path, (unsigned long)cfg->hold_ms);
  printf("[GPIO-007] MEASURE connect known load and measure "
         "voltage/current\n");

  ret = gpio_open_handle(&out, cfg->out_path, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = gpio_set_type(&out, GPIO_OUTPUT_PIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = gpio_write_value(&out, true);
  if (ret < 0)
    {
      goto out_fail;
    }

  usleep(cfg->hold_ms * 1000);

  printf("[GPIO-007] MEASURE output held high; "
         "external measurement required\n");
  gpio_close_handle(&out, cfg->restore);
  return 0;

out_fail:
  gpio_close_handle(&out, cfg->restore);
  return ret;
}

/****************************************************************************
 * Name: run_case_008_measure
 *
 * Description:
 *   GPIO-008: toggle the output for the configured count so edge quality,
 *   overshoot and ringing can be observed externally. No software
 *   pass/fail.
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on a GPIO failure.
 *
 ****************************************************************************/

static int run_case_008_measure(const struct app_config_s *cfg)
{
  struct gpio_handle_s out;
  uint32_t i;
  int ret;

  printf("[GPIO-008] START out=%s period_us=%lu count=%lu\n",
         cfg->out_path, (unsigned long)cfg->period_us,
         (unsigned long)cfg->count);
  printf("[GPIO-008] MEASURE observe edge, overshoot "
         "and ringing externally\n");

  ret = gpio_open_handle(&out, cfg->out_path, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = gpio_set_type(&out, GPIO_OUTPUT_PIN);
  if (ret < 0)
    {
      goto out_fail;
    }

  for (i = 0; i < cfg->count; i++)
    {
      ret = gpio_write_value(&out, true);
      if (ret < 0)
        {
          goto out_fail;
        }

      usleep(cfg->period_us / 2);

      ret = gpio_write_value(&out, false);
      if (ret < 0)
        {
          goto out_fail;
        }

      usleep(cfg->period_us / 2);
    }

  printf("[GPIO-008] MEASURE fast toggle completed\n");
  gpio_close_handle(&out, cfg->restore);
  return 0;

out_fail:
  gpio_close_handle(&out, cfg->restore);
  return ret;
}

/****************************************************************************
 * Name: record_one_gpio
 *
 * Description:
 *   Open a single GPIO, print its current pin type and level, then close it
 *   without restoring (used to snapshot app-start state).
 *
 * Input Parameters:
 *   path - Device path to record.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int record_one_gpio(const char *path)
{
  struct gpio_handle_s gpio;
  enum gpio_pintype_e type;
  bool value;
  int ret;

  ret = gpio_open_handle(&gpio, path, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = gpio_get_type(&gpio, &type);
  if (ret < 0)
    {
      gpio_close_handle(&gpio, false);
      return ret;
    }

  ret = gpio_read_value(&gpio, &value);
  if (ret < 0)
    {
      gpio_close_handle(&gpio, false);
      return ret;
    }

  printf("[GPIO-010] RECORD %s pintype=%s value=%u\n",
         path, pintype_name(type), value ? 1 : 0);
  gpio_close_handle(&gpio, false);
  return 0;
}

/****************************************************************************
 * Name: run_case_010_record
 *
 * Description:
 *   GPIO-010: record the app-start pin type and level of every configured
 *   GPIO. The cold-reset transient itself still needs a logic analyzer.
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on the first failure.
 *
 ****************************************************************************/

static int run_case_010_record(const struct app_config_s *cfg)
{
  int ret;

  printf("[GPIO-010] RECORD app-start GPIO state; "
         "cold-reset transient requires LA\n");

  ret = record_one_gpio(cfg->key0_path);
  if (ret < 0)
    {
      return ret;
    }

  ret = record_one_gpio(cfg->key1_path);
  if (ret < 0)
    {
      return ret;
    }

  ret = record_one_gpio(cfg->led_path);
  if (ret < 0)
    {
      return ret;
    }

  ret = record_one_gpio(cfg->out_path);
  if (ret < 0)
    {
      return ret;
    }

  ret = record_one_gpio(cfg->in_path);
  if (ret < 0)
    {
      return ret;
    }

  return 0;
}

/****************************************************************************
 * Name: run_selected_case
 *
 * Description:
 *   Dispatch to the handler for the configured case. For "all", run the
 *   record, board and pair suites and skip the instrument-only measurement
 *   cases.
 *
 * Input Parameters:
 *   cfg - Active configuration.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure; -EINVAL for an
 *   unsupported case.
 *
 ****************************************************************************/

static int run_selected_case(const struct app_config_s *cfg)
{
  int ret;

  if (strcmp(cfg->case_id, CASE_BOARD) == 0)
    {
      return run_board_case(cfg);
    }

  if (strcmp(cfg->case_id, CASE_PAIR) == 0)
    {
      return run_pair_case(cfg);
    }

  if (strcmp(cfg->case_id, CASE_001) == 0)
    {
      return run_case_001_pair(cfg);
    }

  if (strcmp(cfg->case_id, CASE_002) == 0)
    {
      return run_case_002_pair(cfg);
    }

  if (strcmp(cfg->case_id, CASE_003) == 0)
    {
      return run_case_003_pair(cfg);
    }

  if (strcmp(cfg->case_id, CASE_004) == 0)
    {
      return run_case_004_pair(cfg);
    }

  if (strcmp(cfg->case_id, CASE_005) == 0)
    {
      return run_case_005_pair(cfg);
    }

  if (strcmp(cfg->case_id, CASE_007) == 0)
    {
      return run_case_007_measure(cfg);
    }

  if (strcmp(cfg->case_id, CASE_008) == 0)
    {
      return run_case_008_measure(cfg);
    }

  if (strcmp(cfg->case_id, CASE_010) == 0)
    {
      return run_case_010_record(cfg);
    }

  if (strcmp(cfg->case_id, CASE_ALL) == 0)
    {
      ret = run_case_010_record(cfg);
      if (ret < 0)
        {
          return ret;
        }

      ret = run_board_case(cfg);
      if (ret < 0)
        {
          return ret;
        }

      ret = run_pair_case(cfg);
      if (ret < 0)
        {
          return ret;
        }

      printf("[GPIO-all] SKIP GPIO-007/GPIO-008 "
             "external measurement cases\n");
      printf("[GPIO-all] Run -c 007 or -c 008 explicitly "
             "with instruments\n");
      return 0;
    }

  printf("ERROR: unsupported case %s\n", cfg->case_id);
  return -EINVAL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 *
 * Description:
 *   Entry point: parse arguments, validate paths, print the configuration,
 *   wait the arm delay, then run the selected case.
 *
 * Input Parameters:
 *   argc - Argument count.
 *   argv - Argument vector.
 *
 * Returned Value:
 *   Zero on success; 1 on argument error or case failure.
 *
 ****************************************************************************/

int main(int argc, char *argv[])
{
  struct app_config_s cfg;
  int ret;

  ret = parse_args(argc, argv, &cfg);
  if (ret > 0)
    {
      return 0;
    }

  if (ret < 0)
    {
      return 1;
    }

  ret = validate_config_paths(&cfg);
  if (ret < 0)
    {
      return 1;
    }

  printf("MCU Peripheral GPIO Tests\n");
  printf("Case: %s\n", cfg.case_id);
  printf("KEY0=%s KEY1=%s LED=%s OUT=%s IN=%s\n",
         cfg.key0_path, cfg.key1_path, cfg.led_path,
         cfg.out_path, cfg.in_path);

  if (cfg.arm_ms > 0)
    {
      printf("Arm external instruments now, waiting %lu ms\n",
             (unsigned long)cfg.arm_ms);
      usleep(cfg.arm_ms * 1000);
    }

  ret = run_selected_case(&cfg);

  if (ret < 0)
    {
      printf("GPIO case execution failed: ret=%d\n", ret);
      return 1;
    }

  printf("GPIO case execution completed\n");
  return 0;
}
