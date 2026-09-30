/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_ble_port.c
 *
 * NuttX port of the BL616CL BLE controller library.
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

/* The prebuilt controller (libbtblecontroller_bl616cl_m2s1.a) is the
 * FreeRTOS build: btble_controller_init() creates one task that loops on
 * a message queue, and the BLE interrupt and the HCI host posts wake it.
 * The library calls the OS through weak btblecontroller_* functions; the
 * FreeRTOS set lives in its own archive member.  This file defines every
 * function of that member, so the member is never linked, and runs the
 * task as a kernel thread.
 *
 * The interrupt, MAC and timer hooks keep the library defaults: they call
 * bflb_irq_*, mfg_media_read_macaddr_with_lock() and
 * bflb_mtimer_get_time_us(), which this chip provides.  Only the console
 * output is redirected to syslog, because kernel threads have no stdout.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/kthread.h>
#include <nuttx/sched.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>

/* btble_lib_api.h has legacy non-prototype declarations. */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "btble_lib_api.h"
#pragma GCC diagnostic pop

#include "bl616cl_ble.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* FreeRTOS return values and portMAX_DELAY.  The library was built with a
 * 1 kHz FreeRTOS tick, so its timeouts are in milliseconds.
 */

#define BLE_PORT_PASS          1
#define BLE_PORT_FAIL          0
#define BLE_PORT_WAIT_FOREVER  UINT32_MAX

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef void (*ble_task_entry_t)(void *arg);

/* Fixed-size message queue.  Senders in interrupt context never block;
 * only the controller thread receives.
 */

struct ble_queue_s
{
  sem_t      items;  /* Counts the queued messages */
  spinlock_t lock;   /* Protects head and count */
  uint16_t   depth;  /* Number of message slots */
  uint16_t   size;   /* Bytes per message */
  uint16_t   head;   /* Slot of the oldest message */
  uint16_t   count;  /* Number of queued messages */
  uint8_t    buf[];  /* depth * size bytes */
};

/* The library creates a single task, the controller main loop */

struct ble_task_s
{
  ble_task_entry_t entry;
  void            *arg;
  pid_t            pid;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct ble_task_s g_ble_task;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ble_task_main
 *
 * Description:
 *   Kernel thread entry that runs the library task function.
 *
 ****************************************************************************/

static int ble_task_main(int argc, FAR char *argv[])
{
  g_ble_task.entry(g_ble_task.arg);
  return 0;
}

/****************************************************************************
 * Name: ble_queue_put
 *
 * Description:
 *   Copy one message into the queue if a slot is free.  Safe in interrupt
 *   context.
 *
 * Returned Value:
 *   true if the message was queued, false if the queue is full.
 *
 ****************************************************************************/

static bool ble_queue_put(FAR struct ble_queue_s *q, FAR const void *msg)
{
  irqstate_t flags;
  bool queued = false;

  flags = spin_lock_irqsave(&q->lock);
  if (q->count < q->depth)
    {
      uint16_t slot = (q->head + q->count) % q->depth;

      memcpy(&q->buf[slot * q->size], msg, q->size);
      q->count++;
      queued = true;
    }

  spin_unlock_irqrestore(&q->lock, flags);

  if (queued)
    {
      nxsem_post(&q->items);
    }

  return queued;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_ble_controller_start
 *
 * Description:
 *   Start the controller library: its thread, queue and interrupts.
 *
 ****************************************************************************/

int bl616cl_ble_controller_start(void)
{
  wlinfo("BLE controller %s\n", btble_controller_get_lib_ver());

  /* The task priority argument is FreeRTOS numbering and is not used;
   * btblecontroller_task_new() applies CONFIG_BL_COMPONENT_BLE_PRIORITY.
   */

  btble_controller_init(0);
  if (g_ble_task.entry == NULL)
    {
      /* No deinit: it would delete task handle 0, that is the caller */

      wlerr("ERROR: BLE controller thread was not created\n");
      return -ENOMEM;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_ble_controller_stop
 *
 * Description:
 *   Stop the controller library and release what start created.
 *
 ****************************************************************************/

void bl616cl_ble_controller_stop(void)
{
  btble_controller_deinit();
}

/* The functions below override weak definitions in the controller library.
 * The prototypes follow btblecontroller_port_os.h and
 * btblecontroller_port.h of the Bouffalo SDK; queue handles are opaque.
 */

int btblecontroller_task_new(ble_task_entry_t entry, const char *name,
                             int stack_size, void *arg, int prio,
                             void *handle)
{
  pid_t pid;

  if (g_ble_task.entry != NULL)
    {
      wlerr("ERROR: BLE controller task already exists\n");
      return BLE_PORT_FAIL;
    }

  /* stack_size (FreeRTOS words) and prio (FreeRTOS numbering) come from
   * the library and do not apply here.
   */

  wlinfo("BLE task %s: library stack %d prio %d\n", name, stack_size,
         prio);

  g_ble_task.entry = entry;
  g_ble_task.arg   = arg;

  pid = kthread_create(name, CONFIG_BL_COMPONENT_BLE_PRIORITY,
                       CONFIG_BL_COMPONENT_BLE_STACKSIZE, ble_task_main,
                       NULL);
  if (pid < 0)
    {
      wlerr("ERROR: kthread_create failed: %d\n", pid);
      g_ble_task.entry = NULL;
      return BLE_PORT_FAIL;
    }

  g_ble_task.pid = pid;
  if (handle != NULL)
    {
      *(FAR void **)handle = (FAR void *)(uintptr_t)pid;
    }

  return BLE_PORT_PASS;
}

void btblecontroller_task_delete(uint32_t handle)
{
  /* As in FreeRTOS, handle 0 deletes the calling task */

  pid_t pid = handle != 0 ? (pid_t)handle : nxsched_gettid();

  if (pid == g_ble_task.pid)
    {
      g_ble_task.entry = NULL;
      g_ble_task.pid   = 0;
    }

  kthread_delete(pid);
}

void btblecontroller_task_delay(uint32_t ms)
{
  nxsig_usleep(ms * USEC_PER_MSEC);
}

void *btblecontroller_task_get_current_task_handle(void)
{
  return (FAR void *)(uintptr_t)nxsched_gettid();
}

int btblecontroller_xport_is_inside_interrupt(void)
{
  return up_interrupt_context();
}

int btblecontroller_queue_new(uint32_t depth, uint32_t size, void **queue)
{
  FAR struct ble_queue_s *q;

  q = kmm_zalloc(sizeof(*q) + depth * size);
  if (q == NULL)
    {
      return -1;
    }

  nxsem_init(&q->items, 0, 0);
  spin_lock_init(&q->lock);
  q->depth = depth;
  q->size  = size;

  *queue = q;
  return 0;
}

void btblecontroller_queue_free(void *queue)
{
  FAR struct ble_queue_s *q = queue;

  nxsem_destroy(&q->items);
  kmm_free(q);
}

int btblecontroller_queue_send(void *queue, void *msg, uint32_t size,
                               uint32_t timeout)
{
  FAR struct ble_queue_s *q = queue;
  clock_t start = clock_systime_ticks();

  /* A full queue is rare (the controller thread outranks every sender
   * except interrupts), so poll for a free slot instead of tracking
   * blocked senders.
   */

  while (!ble_queue_put(q, msg))
    {
      if (timeout != BLE_PORT_WAIT_FOREVER &&
          clock_systime_ticks() - start >= MSEC2TICK(timeout))
        {
          return BLE_PORT_FAIL;
        }

      nxsig_usleep(USEC_PER_TICK);
    }

  return BLE_PORT_PASS;
}

int btblecontroller_queue_send_from_isr(void *queue, void *msg,
                                        uint32_t size)
{
  return ble_queue_put(queue, msg) ? BLE_PORT_PASS : BLE_PORT_FAIL;
}

int btblecontroller_queue_recv(void *queue, void *msg, uint32_t timeout)
{
  FAR struct ble_queue_s *q = queue;
  irqstate_t flags;
  int ret;

  if (timeout == BLE_PORT_WAIT_FOREVER)
    {
      ret = nxsem_wait_uninterruptible(&q->items);
    }
  else if (timeout == 0)
    {
      ret = nxsem_trywait(&q->items);
    }
  else
    {
      ret = nxsem_tickwait_uninterruptible(&q->items, MSEC2TICK(timeout));
    }

  if (ret < 0)
    {
      return BLE_PORT_FAIL;
    }

  flags = spin_lock_irqsave(&q->lock);
  memcpy(msg, &q->buf[q->head * q->size], q->size);
  q->head = (q->head + 1) % q->depth;
  q->count--;
  spin_unlock_irqrestore(&q->lock, flags);

  return BLE_PORT_PASS;
}

void *btblecontroller_malloc(size_t size)
{
  return kmm_malloc(size);
}

void btblecontroller_free(void *buf)
{
  kmm_free(buf);
}

int btblecontroller_printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);

  return 0;
}

void btblecontroller_puts(const char *str)
{
  syslog(LOG_INFO, "%s\n", str);
}
