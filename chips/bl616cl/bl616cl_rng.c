/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_rng.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/drivers/drivers.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>

#include "bl616cl_lhal.h"
#include "bflb_clock.h"
#include "bflb_core.h"
#include "hardware/sec_eng_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_TRNG_BLOCK_SIZE 32
#define BL616CL_TRNG_TIMEOUT_MS 100

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static ssize_t bl616cl_rng_read(FAR struct file *filep, FAR char *buffer,
                                size_t buflen);
static int bl616cl_rng_poll(FAR struct file *filep, FAR struct pollfd *fds,
                            bool setup);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_bl616cl_rng_lock = NXMUTEX_INITIALIZER;
static FAR struct bflb_device_s *g_bl616cl_trng;

static const struct file_operations g_bl616cl_rng_fops =
{
  .read = bl616cl_rng_read,
  .poll = bl616cl_rng_poll,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_rng_cleanup
 *
 * Description:
 *   Stop the TRNG and leave it idle: clear the trigger bit, clear the
 *   output register, disable the engine and clear its interrupt. Does
 *   nothing if the device has not been obtained.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_rng_cleanup(void)
{
  uintptr_t ctrladdr;
  uint32_t regval;

  if (g_bl616cl_trng == NULL)
    {
      return;
    }

  ctrladdr = g_bl616cl_trng->reg_base + SEC_ENG_SE_TRNG_0_CTRL_0_OFFSET;
  regval = getreg32(ctrladdr);
  regval &= ~SEC_ENG_SE_TRNG_0_TRIG_1T;
  putreg32(regval, ctrladdr);

  regval = getreg32(ctrladdr);
  regval |= SEC_ENG_SE_TRNG_0_DOUT_CLR_1T;
  putreg32(regval, ctrladdr);

  regval = getreg32(ctrladdr);
  regval &= ~SEC_ENG_SE_TRNG_0_DOUT_CLR_1T;
  putreg32(regval, ctrladdr);

  regval = getreg32(ctrladdr);
  regval &= ~SEC_ENG_SE_TRNG_0_EN;
  putreg32(regval, ctrladdr);

  regval = getreg32(ctrladdr);
  regval |= SEC_ENG_SE_TRNG_0_INT_CLR_1T;
  putreg32(regval, ctrladdr);
}

/****************************************************************************
 * Name: bl616cl_rng_wait_idle
 *
 * Description:
 *   Poll the TRNG busy bit until it clears, or until
 *   BL616CL_TRNG_TIMEOUT_MS milliseconds have elapsed.
 *
 * Input Parameters:
 *   ctrladdr - Address of the TRNG control register.
 *
 * Returned Value:
 *   OK when the TRNG is idle; -ETIMEDOUT on timeout.
 *
 ****************************************************************************/

static int bl616cl_rng_wait_idle(uintptr_t ctrladdr)
{
  uint64_t start = bflb_mtimer_get_time_ms();

  while ((getreg32(ctrladdr) & SEC_ENG_SE_TRNG_0_BUSY) != 0)
    {
      if (bflb_mtimer_get_time_ms() - start > BL616CL_TRNG_TIMEOUT_MS)
        {
          return -ETIMEDOUT;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_rng_store_le32
 *
 * Description:
 *   Store a 32-bit value into a byte buffer in little-endian order.
 *
 * Input Parameters:
 *   dest - Destination buffer of at least four bytes.
 *   value - Value to store.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_rng_store_le32(FAR uint8_t *dest, uint32_t value)
{
  dest[0] = value & UINT32_C(0xff);
  dest[1] = (value >> 8) & UINT32_C(0xff);
  dest[2] = (value >> 16) & UINT32_C(0xff);
  dest[3] = (value >> 24) & UINT32_C(0xff);
}

/****************************************************************************
 * Name: bl616cl_rng_read_block
 *
 * Description:
 *   Generate one 32-byte TRNG block. The sequence follows lhal
 *   bflb_trng_read(), which is not used directly: it ignores
 *   SEC_ENG_SE_TRNG_0_HT_ERROR, so a block that failed the hardware health
 *   test would be returned as random data, and it leaves the TRNG enabled
 *   when it times out.
 *
 * Input Parameters:
 *   block - Buffer that receives the block; zeroed on failure.
 *
 * Returned Value:
 *   OK on success; -ETIMEDOUT if the TRNG stays busy, or -EIO if the
 *   health test fails.
 *
 ****************************************************************************/

static int bl616cl_rng_read_block(uint8_t block[BL616CL_TRNG_BLOCK_SIZE])
{
  uintptr_t ctrladdr =
    g_bl616cl_trng->reg_base + SEC_ENG_SE_TRNG_0_CTRL_0_OFFSET;
  uintptr_t dataaddr =
    g_bl616cl_trng->reg_base + SEC_ENG_SE_TRNG_0_DOUT_0_OFFSET;
  uint32_t regval;
  size_t i;
  int ret;

  regval = getreg32(ctrladdr);
  regval |= SEC_ENG_SE_TRNG_0_EN;
  putreg32(regval, ctrladdr);

  regval = getreg32(ctrladdr);
  regval |= SEC_ENG_SE_TRNG_0_INT_CLR_1T;
  putreg32(regval, ctrladdr);

  __asm__ __volatile__("nop");
  __asm__ __volatile__("nop");
  __asm__ __volatile__("nop");
  __asm__ __volatile__("nop");

  ret = bl616cl_rng_wait_idle(ctrladdr);
  if (ret < 0)
    {
      goto fail;
    }

  if ((getreg32(ctrladdr) & SEC_ENG_SE_TRNG_0_HT_ERROR) != 0)
    {
      ret = -EIO;
      goto fail;
    }

  regval = getreg32(ctrladdr);
  regval |= SEC_ENG_SE_TRNG_0_INT_CLR_1T;
  putreg32(regval, ctrladdr);

  regval = getreg32(ctrladdr);
  regval |= SEC_ENG_SE_TRNG_0_TRIG_1T;
  putreg32(regval, ctrladdr);

  __asm__ __volatile__("nop");
  __asm__ __volatile__("nop");
  __asm__ __volatile__("nop");
  __asm__ __volatile__("nop");

  ret = bl616cl_rng_wait_idle(ctrladdr);
  if (ret < 0)
    {
      goto fail;
    }

  if ((getreg32(ctrladdr) & SEC_ENG_SE_TRNG_0_HT_ERROR) != 0)
    {
      ret = -EIO;
      goto fail;
    }

  for (i = 0; i < BL616CL_TRNG_BLOCK_SIZE / sizeof(uint32_t); i++)
    {
      bl616cl_rng_store_le32(&block[i * sizeof(uint32_t)],
                             getreg32(dataaddr + i * sizeof(uint32_t)));
    }

  bl616cl_rng_cleanup();
  return OK;

fail:
  bl616cl_rng_cleanup();
  memset(block, 0, BL616CL_TRNG_BLOCK_SIZE);
  return ret;
}

/****************************************************************************
 * Name: bl616cl_rng_read
 *
 * Description:
 *   Implement the file_operations read method of /dev/random and
 *   /dev/urandom. Serialized by a mutex, it fills the buffer with 32-byte
 *   TRNG blocks and wipes its stack copy afterward. A short read is
 *   returned if a later block fails.
 *
 * Input Parameters:
 *   filep - File structure instance (unused).
 *   buffer - Buffer that receives the random bytes.
 *   buflen - Number of bytes requested.
 *
 * Returned Value:
 *   The number of bytes read; 0 if buflen is zero; -ENODEV if the TRNG is
 *   not initialized; a negated errno value if no data could be generated
 *   (mutex error, -ETIMEDOUT or -EIO).
 *
 ****************************************************************************/

static ssize_t bl616cl_rng_read(FAR struct file *filep, FAR char *buffer,
                                size_t buflen)
{
  uint8_t block[BL616CL_TRNG_BLOCK_SIZE];
  size_t generated = 0;
  int ret;

  UNUSED(filep);

  if (buflen == 0)
    {
      return 0;
    }

  if (g_bl616cl_trng == NULL)
    {
      return -ENODEV;
    }

  ret = nxmutex_lock(&g_bl616cl_rng_lock);
  if (ret < 0)
    {
      return ret;
    }

  while (generated < buflen)
    {
      size_t copylen = buflen - generated;

      ret = bl616cl_rng_read_block(block);
      if (ret < 0)
        {
          break;
        }

      if (copylen > sizeof(block))
        {
          copylen = sizeof(block);
        }

      memcpy(buffer + generated, block, copylen);
      memset(block, 0, sizeof(block));
      generated += copylen;
    }

  nxmutex_unlock(&g_bl616cl_rng_lock);
  memset(block, 0, sizeof(block));

  return generated > 0 ? (ssize_t)generated : ret;
}

/****************************************************************************
 * Name: bl616cl_rng_poll
 *
 * Description:
 *   Implement the file_operations poll method. The device is always
 *   readable, so POLLIN is notified immediately on setup.
 *
 * Input Parameters:
 *   filep - File structure instance (unused).
 *   fds - The poll structure to notify.
 *   setup - True to set up the poll; false to tear it down.
 *
 * Returned Value:
 *   OK, always.
 *
 ****************************************************************************/

static int bl616cl_rng_poll(FAR struct file *filep, FAR struct pollfd *fds,
                            bool setup)
{
  UNUSED(filep);

  if (setup)
    {
      poll_notify(&fds, 1, POLLIN);
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_rng_initialize
 *
 * Description:
 *   Obtain the SEC TRNG device once, enable the SEC peripheral clock and
 *   put the TRNG in its idle state. Later calls return OK immediately.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the TRNG device is unavailable.
 *
 ****************************************************************************/

static int bl616cl_rng_initialize(void)
{
  if (g_bl616cl_trng != NULL)
    {
      return OK;
    }

  PERIPHERAL_CLOCK_SEC_ENABLE();
  g_bl616cl_trng = bflb_device_get_by_name(BFLB_NAME_SEC_TRNG);
  if (g_bl616cl_trng == NULL)
    {
      syslog(LOG_ERR, "BL616CL TRNG device is unavailable\n");
      return -ENODEV;
    }

  bl616cl_rng_cleanup();
  return OK;
}

/****************************************************************************
 * Name: bl616cl_rng_register
 *
 * Description:
 *   Initialize the TRNG and register it as a read-only character driver
 *   using g_bl616cl_rng_fops. Failure is logged and not returned.
 *
 * Input Parameters:
 *   path - Device node path to register.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_rng_register(FAR const char *path)
{
  int ret;

  ret = bl616cl_rng_initialize();
  if (ret >= 0)
    {
      ret = register_driver(path, &g_bl616cl_rng_fops, 0444, NULL);
    }

  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to register %s: %d\n", path, ret);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_DEV_RANDOM
/****************************************************************************
 * Name: devrandom_register
 *
 * Description:
 *   Register /dev/random. Called by the NuttX core during initialization;
 *   backed by the BL616CL TRNG.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void devrandom_register(void)
{
  bl616cl_rng_register("/dev/random");
}
#endif

#ifdef CONFIG_DEV_URANDOM_ARCH
/****************************************************************************
 * Name: devurandom_register
 *
 * Description:
 *   Register /dev/urandom. Called by the NuttX core during
 *   initialization; backed by the BL616CL TRNG.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void devurandom_register(void)
{
  bl616cl_rng_register("/dev/urandom");
}
#endif
