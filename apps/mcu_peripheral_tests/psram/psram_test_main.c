/****************************************************************************
 * vendor/bouffalolab/apps/mcu_peripheral_tests/psram/psram_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <inttypes.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <nuttx/cache.h>
#include <arch/barriers.h>
#include <arch/chip/bl616cl_psram.h>

/* Keep room for NSH and allocator metadata. A near-capacity allocation also
 * exercises address lines beyond the small buffers that fit in the cache.
 */

#define PSRAM_RESERVE (64 * 1024)

static uint32_t psram_pattern(size_t index, unsigned int pass)
{
  static const uint32_t seeds[] =
  {
    0x00000000, 0xffffffff, 0xaa55aa55, 0x55aa55aa
  };

  return ((uint32_t)index * 0x9e3779b9u) ^ seeds[pass];
}

int main(int argc, FAR char *argv[])
{
  volatile uint32_t *cached;
  volatile uint32_t *uncached;
  uintptr_t start;
  uintptr_t end;
  size_t capacity = bl616cl_psram_size_get();
  size_t bytes;
  size_t words;
  size_t i;
  unsigned int pass;
  uint32_t expected;
  uint32_t actual;
  int ret = EXIT_FAILURE;

  if (capacity <= PSRAM_RESERVE)
    {
      printf("PSRAM FAIL: unavailable, capacity=%zu\n", capacity);
      return ret;
    }

  bytes = capacity - PSRAM_RESERVE;
  cached = memalign(32, bytes);
  start = (uintptr_t)cached;
  end = start + bytes;
  if (cached == NULL || start < BL616CL_PSRAM_BASE ||
      end > BL616CL_PSRAM_BASE + capacity)
    {
      printf("PSRAM FAIL: allocation=%p bytes=%zu capacity=%zu\n",
             (void *)cached, bytes, capacity);
      free((void *)cached);
      return ret;
    }

  uncached = (volatile uint32_t *)(start - BL616CL_PSRAM_BASE +
                                  BL616CL_PSRAM_NOCACHE_BASE);
  words = bytes / sizeof(uint32_t);
  printf("PSRAM allocation: capacity=%zu start=0x%08" PRIxPTR
         " bytes=%zu\n", capacity, start, bytes);

  for (pass = 0; pass < 4; pass++)
    {
      for (i = 0; i < words; i++)
        {
          cached[i] = psram_pattern(i, pass);
        }

      /* Reading the alias proves writes reached the device, even for the
       * last cache lines which have not been evicted by the large sweep.
       */

      up_clean_dcache(start, end);
      UP_DSB();
      for (i = 0; i < words; i++)
        {
          expected = psram_pattern(i, pass);
          actual = uncached[i];
          if (actual != expected)
            {
              printf("PSRAM FAIL: clean pass=%u offset=%zu "
                     "expected=%08" PRIx32 " actual=%08" PRIx32 "\n",
                     pass, i * sizeof(uint32_t), expected, actual);
              goto out;
            }
        }

      /* Fill cache lines, change physical memory, then invalidate. Missing
       * PSRAM support in the range API must fail this small-buffer check.
       */

      for (i = 0; i < 256; i++)
        {
          actual = cached[i];
          (void)actual;
          uncached[i] = ~psram_pattern(i, pass);
        }

      UP_DSB();
      actual = cached[0];
      if (actual != psram_pattern(0, pass))
        {
          printf("PSRAM FAIL: stale cache control pass=%u\n", pass);
          goto out;
        }

      up_invalidate_dcache(start, start + 256 * sizeof(uint32_t));
      for (i = 0; i < 256; i++)
        {
          expected = ~psram_pattern(i, pass);
          actual = cached[i];
          if (actual != expected)
            {
              printf("PSRAM FAIL: invalidate pass=%u offset=%zu "
                     "expected=%08" PRIx32 " actual=%08" PRIx32 "\n",
                     pass, i * sizeof(uint32_t), expected, actual);
              goto out;
            }
        }

      printf("PSRAM pass=%u bytes=%zu clean/invalidate PASS\n", pass, bytes);
    }

  ret = EXIT_SUCCESS;
out:
  free((void *)cached);
  printf("PSRAM %s\n", ret == EXIT_SUCCESS ? "PASS" : "FAIL");
  return ret;
}
