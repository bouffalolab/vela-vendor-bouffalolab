/****************************************************************************
 * apps/vendor/bouffalolab/apps/perf_tools/perfmon/perfmon_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef CONFIG_ALLSYMS
#  include <nuttx/allsyms.h>
#  include <nuttx/symtab.h>
#endif

#include <arch/irq.h>
#include <arch/chip/bl616cl_perfmon.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PERFMON_DEFAULT_SECONDS 10
#define PERFMON_DEFAULT_SHIFT   5
#define PERFMON_DEFAULT_TOP     20
#define PERFMON_IRQ_TOP         8

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct perfmon_irqname_s
{
  int irq;
  FAR const char *name;
};

struct perfmon_window_s
{
  struct bl616cl_perfmon_counters_s counters;
  FAR struct bl616cl_perfmon_irq_s *irqs;
  struct timespec ts;
};

#ifdef CONFIG_BL616CL_PERFMON_PCSAMPLE
struct perfmon_func_s
{
  uintptr_t addr;
  FAR const char *name;
  uint32_t count;
};
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct perfmon_irqname_s g_perfmon_irqnames[] =
{
  { RISCV_IRQ_ECALLM, "ecall" },
  { RISCV_IRQ_MSOFT, "msoft" },
  { RISCV_IRQ_MTIMER, "mtimer" },
  { BL616CL_IRQ_NUM_BMX_MCU_BUS_ERR, "bmx_mcu_bus_err" },
  { BL616CL_IRQ_NUM_BMX_MCU_TO, "bmx_mcu_to" },
  { BL616CL_IRQ_NUM_DBI, "dbi" },
  { BL616CL_IRQ_NUM_SDU_SOFT_RST, "sdu_soft_rst" },
  { BL616CL_IRQ_NUM_AUDAC, "audac" },
  { BL616CL_IRQ_NUM_RF_TOP_INT0, "rf_top_int0" },
  { BL616CL_IRQ_NUM_RF_TOP_INT1, "rf_top_int1" },
  { BL616CL_IRQ_NUM_SDIO, "sdio" },
  { BL616CL_IRQ_NUM_WIFI_TBTT_SLEEP, "wifi_tbtt_sleep" },
  { BL616CL_IRQ_NUM_SEC_ENG_ID1_CDET, "sec_eng_id1_cdet" },
  { BL616CL_IRQ_NUM_SEC_ENG_ID0_CDET, "sec_eng_id0_cdet" },
  { BL616CL_IRQ_NUM_SF_CTRL_ID1, "sf_ctrl_id1" },
  { BL616CL_IRQ_NUM_SF_CTRL_ID0, "sf_ctrl_id0" },
  { BL616CL_IRQ_NUM_DMA0_ALL, "dma0_all" },
  { BL616CL_IRQ_NUM_DVP2BUS_INT0, "dvp2bus_int0" },
  { BL616CL_IRQ_NUM_SDH, "sdh" },
  { BL616CL_IRQ_NUM_DVP2BUS_INT1, "dvp2bus_int1" },
  { BL616CL_IRQ_NUM_WIFI_TBTT_WAKEUP, "wifi_tbtt_wakeup" },
  { BL616CL_IRQ_NUM_TOUCH_V2, "touch_v2" },
  { BL616CL_IRQ_NUM_USB, "usb" },
  { BL616CL_IRQ_NUM_AUADC, "auadc" },
  { BL616CL_IRQ_NUM_MJPEG, "mjpeg" },
  { BL616CL_IRQ_NUM_EMAC, "emac" },
  { BL616CL_IRQ_NUM_GPADC_DMA, "gpadc_dma" },
  { BL616CL_IRQ_NUM_EFUSE, "efuse" },
  { BL616CL_IRQ_NUM_SPI0, "spi0" },
  { BL616CL_IRQ_NUM_UART0, "uart0" },
  { BL616CL_IRQ_NUM_UART1, "uart1" },
  { BL616CL_IRQ_NUM_GPIO_DMA, "gpio_dma" },
  { BL616CL_IRQ_NUM_I2C0, "i2c0" },
  { BL616CL_IRQ_NUM_PWM, "pwm" },
  { BL616CL_IRQ_NUM_PEC_INT0, "pec_int0" },
  { BL616CL_IRQ_NUM_PEC_INT1, "pec_int1" },
  { BL616CL_IRQ_NUM_TIMER0, "timer0" },
  { BL616CL_IRQ_NUM_TIMER1, "timer1" },
  { BL616CL_IRQ_NUM_WDG, "wdg" },
  { BL616CL_IRQ_NUM_I2C1, "i2c1" },
  { BL616CL_IRQ_NUM_I2S, "i2s" },
  { BL616CL_IRQ_NUM_ANA_OCP_OUT_TO_CPU_0, "ana_ocp_out_to_cpu_0" },
  { BL616CL_IRQ_NUM_ANA_OCP_OUT_TO_CPU_1, "ana_ocp_out_to_cpu_1" },
  { BL616CL_IRQ_NUM_XTAL_RDY_SCAN, "xtal_rdy_scan" },
  { BL616CL_IRQ_NUM_GPIO_INT0, "gpio_int0" },
  { BL616CL_IRQ_NUM_DM, "dm" },
  { BL616CL_IRQ_NUM_BT, "bt" },
  { BL616CL_IRQ_NUM_UART2, "uart2" },
  { BL616CL_IRQ_NUM_SPI1, "spi1" },
  { BL616CL_IRQ_NUM_MJDEC, "mjdec" },
  { BL616CL_IRQ_NUM_PDS_WAKEUP, "pds_wakeup" },
  { BL616CL_IRQ_NUM_HBN_OUT0, "hbn_out0" },
  { BL616CL_IRQ_NUM_HBN_OUT1, "hbn_out1" },
  { BL616CL_IRQ_NUM_BOD, "bod" },
  { BL616CL_IRQ_NUM_WIFI, "wifi" },
  { BL616CL_IRQ_NUM_BZ_PHY_INT, "bz_phy_int" },
  { BL616CL_IRQ_NUM_BLE, "ble" },
  { BL616CL_IRQ_NUM_MAC_INT_TIMER, "mac_int_timer" },
  { BL616CL_IRQ_NUM_MAC_INT_MISC, "mac_int_misc" },
  { BL616CL_IRQ_NUM_MAC_INT_RX_TRIGGER, "mac_int_rx_trigger" },
  { BL616CL_IRQ_NUM_MAC_INT_TX_TRIGGER, "mac_int_tx_trigger" },
  { BL616CL_IRQ_NUM_MAC_INT_GEN, "mac_int_gen" },
  { BL616CL_IRQ_NUM_MAC_INT_PROT_TRIGGER, "mac_int_prot_trigger" },
  { BL616CL_IRQ_NUM_WIFI_IPC, "wifi_ipc" },
  { BL616CL_IRQ_NUM_EXP_PERI, "exp_peri" },
  { BL616CL_IRQ_NUM_LTMR_GPIO_LAT, "ltmr_gpio_lat" },
  { BL616CL_IRQ_NUM_WDT1, "wdt1" },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void perfmon_usage(FAR const char *progname)
{
  printf("Usage: %s stat [-d delay] [-t seconds]\n"
         "       %s prof [-d delay] [-t seconds] [-s shift] [-n top] [-r]\n"
         "  stat  E907 counters and time per IRQ over the window\n"
         "  prof  tick PC samples: hottest functions, or with -r the raw\n"
         "        histogram (2^shift byte buckets) for host tools\n"
         "  -d    wait before the window starts, e.g. past TCP ramp-up\n"
         "Start it in the background next to the workload, for example\n"
         "  %s stat -d 2 -t 15 &\n",
         progname, progname, progname);
}

static FAR const char *perfmon_irqname(int irq)
{
  size_t i;

  for (i = 0; i < sizeof(g_perfmon_irqnames) /
                  sizeof(g_perfmon_irqnames[0]); i++)
    {
      if (g_perfmon_irqnames[i].irq == irq)
        {
          return g_perfmon_irqnames[i].name;
        }
    }

  return irq < RISCV_IRQ_ASYNC ? "exception" : "-";
}

static int perfmon_snapshot(FAR struct perfmon_window_s *w)
{
  w->irqs = calloc(NR_IRQS, sizeof(*w->irqs));
  if (w->irqs == NULL)
    {
      return -ENOMEM;
    }

  clock_gettime(CLOCK_MONOTONIC, &w->ts);
  bl616cl_perfmon_read(&w->counters);
  bl616cl_perfmon_irq_read(w->irqs, NR_IRQS);
  return OK;
}

static double perfmon_ratio(uint64_t part, uint64_t whole)
{
  return whole != 0 ? (double)part * 100.0 / (double)whole : 0.0;
}

static void perfmon_print_counters(FAR const struct perfmon_window_s *b,
                                   FAR const struct perfmon_window_s *e)
{
  uint64_t d[BL616CL_PERFMON_NEVENTS];
  uint64_t cycles = e->counters.cycle - b->counters.cycle;
  uint64_t instret = e->counters.instret - b->counters.instret;
  double seconds;
  int i;

  for (i = 0; i < BL616CL_PERFMON_NEVENTS; i++)
    {
      d[i] = e->counters.event[i] - b->counters.event[i];
    }

  seconds = (double)(e->ts.tv_sec - b->ts.tv_sec) +
            (double)(e->ts.tv_nsec - b->ts.tv_nsec) / 1e9;

  /* mcycle keeps counting in WFI, so idle time lowers the IPC. */

  printf("perfmon: %.2f s, %" PRIu64 " cycles, %" PRIu64
         " instructions, IPC %.3f (idle included)\n",
         seconds, cycles, instret,
         cycles != 0 ? (double)instret / (double)cycles : 0.0);
  printf("  I-cache    %12" PRIu64 " access %10" PRIu64 " miss %6.2f%%\n",
         d[BL616CL_PERFMON_ICACHE_ACCESS], d[BL616CL_PERFMON_ICACHE_MISS],
         perfmon_ratio(d[BL616CL_PERFMON_ICACHE_MISS],
                       d[BL616CL_PERFMON_ICACHE_ACCESS]));
  printf("  D-cache rd %12" PRIu64 " access %10" PRIu64 " miss %6.2f%%\n",
         d[BL616CL_PERFMON_DCACHE_READ], d[BL616CL_PERFMON_DCACHE_READ_MISS],
         perfmon_ratio(d[BL616CL_PERFMON_DCACHE_READ_MISS],
                       d[BL616CL_PERFMON_DCACHE_READ]));
  printf("  D-cache wr %12" PRIu64 " access %10" PRIu64 " miss %6.2f%%\n",
         d[BL616CL_PERFMON_DCACHE_WRITE],
         d[BL616CL_PERFMON_DCACHE_WRITE_MISS],
         perfmon_ratio(d[BL616CL_PERFMON_DCACHE_WRITE_MISS],
                       d[BL616CL_PERFMON_DCACHE_WRITE]));
  printf("  branch     %12" PRIu64 " cond.  %10" PRIu64 " miss %6.2f%%\n",
         d[BL616CL_PERFMON_BRANCH], d[BL616CL_PERFMON_BRANCH_MISPREDICT],
         perfmon_ratio(d[BL616CL_PERFMON_BRANCH_MISPREDICT],
                       d[BL616CL_PERFMON_BRANCH]));
}

static void perfmon_print_irqs(FAR const struct perfmon_window_s *b,
                               FAR const struct perfmon_window_s *e)
{
  uint64_t window = e->counters.cycle - b->counters.cycle;
  uint64_t total = 0;
  bool shown[NR_IRQS];
  int n;
  int i;

  memset(shown, 0, sizeof(shown));
  for (i = 0; i < NR_IRQS; i++)
    {
      total += e->irqs[i].cycles - b->irqs[i].cycles;
    }

  printf("  interrupts and exceptions: %.1f%% of the cycles\n",
         perfmon_ratio(total, window));
  printf("   IRQ name                   calls       cycles  share"
         "  cyc/call\n");

  /* Print the busiest entries first. */

  for (n = 0; n < PERFMON_IRQ_TOP; n++)
    {
      uint64_t best = 0;
      uint32_t calls;
      int irq = -1;

      for (i = 0; i < NR_IRQS; i++)
        {
          uint64_t c = e->irqs[i].cycles - b->irqs[i].cycles;

          if (!shown[i] && c > best)
            {
              best = c;
              irq = i;
            }
        }

      if (irq < 0)
        {
          break;
        }

      shown[irq] = true;
      calls = e->irqs[irq].count - b->irqs[irq].count;
      printf("  %4d %-20s %8" PRIu32 " %12" PRIu64 " %5.1f%% %9" PRIu64
             "\n", irq, perfmon_irqname(irq), calls, best,
             perfmon_ratio(best, window),
             calls != 0 ? best / calls : 0);
    }
}

static int perfmon_stat(int seconds)
{
  struct perfmon_window_s b;
  struct perfmon_window_s e;
  int ret;

  ret = perfmon_snapshot(&b);
  if (ret < 0)
    {
      return ret;
    }

  sleep(seconds);
  ret = perfmon_snapshot(&e);
  if (ret == OK)
    {
      perfmon_print_counters(&b, &e);
      perfmon_print_irqs(&b, &e);
      free(e.irqs);
    }

  free(b.irqs);
  return ret;
}

#ifdef CONFIG_BL616CL_PERFMON_PCSAMPLE
static int perfmon_cmp_func(FAR const void *a, FAR const void *b)
{
  FAR const struct perfmon_func_s *fa = a;
  FAR const struct perfmon_func_s *fb = b;

  return fa->count < fb->count ? 1 : (fa->count > fb->count ? -1 : 0);
}

static void perfmon_print_top(FAR const uint16_t *hist, uintptr_t base,
                              size_t nbuckets, unsigned int shift,
                              uint32_t samples, int top)
{
  FAR struct perfmon_func_s *funcs;
  size_t nfuncs = 0;
  size_t used = 0;
  size_t i;
#ifdef CONFIG_ALLSYMS
  uintptr_t symend = 0;
#endif

  for (i = 0; i < nbuckets; i++)
    {
      used += hist[i] != 0;
    }

  funcs = calloc(used != 0 ? used : 1, sizeof(*funcs));
  if (funcs == NULL)
    {
      printf("perfmon: no memory for %zu entries\n", used);
      return;
    }

  /* Buckets are in address order, so the buckets of one function are
   * adjacent.  A bucket counts for the function it starts in.
   */

  for (i = 0; i < nbuckets; i++)
    {
      uintptr_t addr = base + (i << shift);
      FAR const char *name = NULL;

      if (hist[i] == 0)
        {
          continue;
        }

#ifdef CONFIG_ALLSYMS
      if (nfuncs == 0 || addr >= symend)
        {
          FAR const struct symtab_s *sym;
          size_t size;

          sym = allsyms_findbyvalue((FAR void *)addr, &size);
          if (sym != NULL)
            {
              name = sym->sym_name;
              addr = (uintptr_t)sym->sym_value;
              symend = size != 0 ? addr + size : addr + 1;
            }
        }
      else
        {
          name = funcs[nfuncs - 1].name;
          addr = funcs[nfuncs - 1].addr;
        }
#endif

      if (nfuncs != 0 && funcs[nfuncs - 1].addr == addr &&
          funcs[nfuncs - 1].name == name)
        {
          funcs[nfuncs - 1].count += hist[i];
        }
      else
        {
          funcs[nfuncs].addr = addr;
          funcs[nfuncs].name = name;
          funcs[nfuncs].count = hist[i];
          nfuncs++;
        }
    }

  qsort(funcs, nfuncs, sizeof(*funcs), perfmon_cmp_func);
  printf("      %%  samples  address     function\n");
  for (i = 0; i < nfuncs && i < (size_t)top; i++)
    {
      printf("  %5.1f %8" PRIu32 "  0x%08" PRIxPTR "  %s\n",
             perfmon_ratio(funcs[i].count, samples), funcs[i].count,
             funcs[i].addr, funcs[i].name != NULL ? funcs[i].name : "?");
    }

  free(funcs);
}

static int perfmon_prof(int seconds, unsigned int shift, int top, bool raw)
{
  struct perfmon_window_s b;
  struct perfmon_window_s e;
  FAR uint16_t *hist;
  uintptr_t base;
  size_t size;
  size_t nbuckets;
  struct bl616cl_perfmon_pcstat_s stat;
  size_t i;
  int ret;

  bl616cl_perfmon_text_range(&base, &size);
  nbuckets = (size + (1u << shift) - 1) >> shift;
  hist = calloc(nbuckets, sizeof(*hist));
  if (hist == NULL)
    {
      printf("perfmon: no memory for %zu buckets; use a larger -s\n",
             nbuckets);
      return -ENOMEM;
    }

  ret = perfmon_snapshot(&b);
  if (ret < 0)
    {
      free(hist);
      return ret;
    }

  ret = bl616cl_perfmon_pc_start(hist, base, nbuckets, shift);
  if (ret < 0)
    {
      printf("perfmon: sampling not started: %d\n", ret);
      free(b.irqs);
      free(hist);
      return ret;
    }

  sleep(seconds);
  bl616cl_perfmon_pc_stop(&stat);
  ret = perfmon_snapshot(&e);

  if (ret == OK)
    {
      perfmon_print_counters(&b, &e);
      perfmon_print_irqs(&b, &e);
      printf("  %" PRIu32 " ticks: idle %.1f%%, busy outside .text"
             " (RAM code) %.1f%%\n", stat.samples,
             perfmon_ratio(stat.idle, stat.samples),
             perfmon_ratio(stat.outside, stat.samples));
      if (raw)
        {
          printf("perfmon-raw base=0x%08" PRIxPTR " shift=%u buckets=%zu"
                 " samples=%" PRIu32 " idle=%" PRIu32 " outside=%" PRIu32
                 "\n", base, shift, nbuckets, stat.samples, stat.idle,
                 stat.outside);
          for (i = 0; i < nbuckets; i++)
            {
              if (hist[i] != 0)
                {
                  printf("%zx %u\n", i, hist[i]);
                }
            }

          printf("perfmon-raw end\n");
        }
      else
        {
          perfmon_print_top(hist, base, nbuckets, shift, stat.samples,
                            top);
        }

      free(e.irqs);
    }

  free(b.irqs);
  free(hist);
  return ret;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  int seconds = PERFMON_DEFAULT_SECONDS;
  int delay = 0;
  unsigned int shift = PERFMON_DEFAULT_SHIFT;
  int top = PERFMON_DEFAULT_TOP;
  bool raw = false;
  int opt;
  int ret;

  if (argc < 2)
    {
      perfmon_usage(argv[0]);
      return EXIT_FAILURE;
    }

  /* Options follow the subcommand. */

  while ((opt = getopt(argc - 1, argv + 1, "d:t:s:n:r")) != -1)
    {
      switch (opt)
        {
          case 'd':
            delay = atoi(optarg);
            break;

          case 't':
            seconds = atoi(optarg);
            break;

          case 's':
            shift = atoi(optarg);
            break;

          case 'n':
            top = atoi(optarg);
            break;

          case 'r':
            raw = true;
            break;

          default:
            perfmon_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

  if (delay < 0 || seconds <= 0 || shift < 2 || shift > 12 || top <= 0)
    {
      perfmon_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (delay > 0)
    {
      sleep(delay);
    }

#ifndef CONFIG_BL616CL_PERFMON_PCSAMPLE
  UNUSED(raw); /* Only prof takes -r */
#endif

  if (strcmp(argv[1], "stat") == 0)
    {
      ret = perfmon_stat(seconds);
    }
#ifdef CONFIG_BL616CL_PERFMON_PCSAMPLE
  else if (strcmp(argv[1], "prof") == 0)
    {
      ret = perfmon_prof(seconds, shift, top, raw);
    }
#endif
  else
    {
      perfmon_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (ret < 0)
    {
      printf("perfmon: failed: %d\n", ret);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
