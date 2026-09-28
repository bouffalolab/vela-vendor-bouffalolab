/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_wifi_adapter.c
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

#include <assert.h>
#include <debug.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <arch/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/wqueue.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>
#include <nuttx/signal.h>
#include <nuttx/wdog.h>
#include <nuttx/wireless/wireless.h>
#include <nuttx/wireless/ieee80211/ieee80211.h>

#include "bl616cl_wifi_glb.h"

#include "bl616cl_wifi_adapter.h"
#include "bl616cl_wlan.h"

#include "wl80211.h"
#include "macsw.h"

#include "supplicant.h"
#include "bl_wpa.h"
#include "wifi_mgmr.h"
#include "macsw_plat.h"
#include "wl80211_platform.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CONFIG_BL616CL_FW_TASK_NAME       "wifi_fw"

#ifndef CONFIG_BL616CL_SCAN_DURANTION
  #define CONFIG_BL616CL_SCAN_DURANTION   (100)
#endif /* CONFIG_BL616CL_SCAN_DURANTION */

#ifndef CONFIG_BL616CL_WLAN_CONNECT_TIMEOUT
  #define CONFIG_BL616CL_WLAN_CONNECT_TIMEOUT (20) /* 60s */
#endif /* CONFIG_BL616CL_WLAN_CONNECT_TIMEOUT */

#ifndef CONFIG_BL616CL_WLAN_PS_ACTIVETIME
  #define CONFIG_BL616CL_WLAN_PS_ACTIVETIME (50)
#endif

 #define WIFI_TASK_STACK_SIZE           (6 * 1024)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Wi-Fi interrupt private data */

/* Wi-Fi cfg */

static struct
{
  int mode;
  uint8_t bssid[MAC_LEN];
  /* sta */

  char ssid[SSID_MAX_LEN];
  int ssid_len;
  int essid_flag;
  char pwd[PWD_MAX_LEN + 1];
  int pwd_len;
  uint16_t channel;
  uint16_t freq;
  uint8_t pta;

  bool pmk_valid;
  char pmk[PWD_MAX_LEN + 1];

  /* lower power */
  uint8_t dtim;
  bool powersave;

  /* ap not supported */
} g_wifi_cfg;

/* wifi mgmr handle */


/* Wi-Fi event private data */

static mutex_t g_wifiexcl_lock = NXMUTEX_INITIALIZER;

/* Wi-Fi adapter reference */


/* Semaphore for task notification synchronization */
static sem_t g_wifi_notify_sem = SEM_INITIALIZER(0);

/* Main wifi stack entry point */
extern void wifi_main(void *param);

/* If Wi-Fi sta connected */

static bool g_sta_connected;

/* If Wi-Fi sta connect blocking */

static volatile bool g_sta_block = false;

/* Wi-Fi station TX done callback function */

static wifi_txdone_cb_t g_sta_txdone_cb;

static sem_t g_wifi_scan_sem = SEM_INITIALIZER(1);
static sem_t g_wifi_wait_connect_sem = SEM_INITIALIZER(0);


/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* rf config */

extern int32_t rfparam_init(uint32_t base_addr,
                            void *rf_para,
                            uint32_t apply_flag);
extern int rfparam_set_country_code(const char *country);
extern void rfparam_update_ant_gain(int8_t ant_gain);
extern int8_t rfparam_get_ant_gain(void);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nibble2hex
 *
 * Description:
 *  Convert a binary nibble to a hexadecimal character.
 *
 ****************************************************************************/

static char nibble2hex(unsigned char nibble)
{
  if (nibble < 10)
    {
      return '0' + nibble;
    }
  else
    {
      return 'A' + nibble - 10;
    }
}

/****************************************************************************
 * Name: bin2hex
 ****************************************************************************/

static size_t bin2hex(const uint8_t *buf,
                      size_t buflen,
                      char *hex,
                      size_t hexlen)
{
  size_t i;

  if (buflen > hexlen)
    {
      buflen = hexlen;
    }

  for (i = 0; i < buflen; i++)
    {
      hex[2 * i]     = nibble2hex(buf[i] >> 4);
      hex[2 * i + 1] = nibble2hex(buf[i] & 0xf);
    }

  return buflen;
}

/****************************************************************************
 * Name: adapter_wifi_lock
 *
 * Description:
 *   Lock or unlock the event process
 *
 * Input Parameters:
 *   lock - true: Lock event process, false: unlock event process
 *
 * Returned Value:
 *   The result of lock or unlock the event process
 *
 ****************************************************************************/

static int adapter_wifi_lock(bool lock)
{
  int ret;

  if (lock)
    {
      ret = nxmutex_lock(&g_wifiexcl_lock);
      if (ret < 0)
        {
          wlinfo("INFO: Failed to lock Wi-Fi ret=%d\n", ret);
        }
    }
  else
    {
      ret = nxmutex_unlock(&g_wifiexcl_lock);
      if (ret < 0)
        {
          wlinfo("INFO: Failed to unlock Wi-Fi ret=%d\n", ret);
        }
    }

  return ret;
}

#if 0
/****************************************************************************
 * Name: bl616_wifi_auth_trans
 *
 * Description:
 *   Converts a auth type values to WEXT auth type values.
 *
 * Input Parameters:
 *   wifi_auth - bl616 auth type
 *
 * Returned Value:
 *     auth type
 *
 ****************************************************************************/

static int bl616_wifi_auth_trans(uint32_t wifi_auth)
{
  int auth_mode = IW_AUTH_WPA_VERSION_DISABLED;

  switch (wifi_auth)
    {
      case WIFI_AUTH_OPEN:
        auth_mode = IW_AUTH_WPA_VERSION_DISABLED;
        break;

      case WIFI_AUTH_WPA_PSK:
        auth_mode = IW_AUTH_WPA_VERSION_WPA;
        break;

      case WIFI_AUTH_WPA2_PSK:
      case WIFI_AUTH_WPA_WPA2_PSK:
        auth_mode = IW_AUTH_WPA_VERSION_WPA2;
        break;

      default:
        wlerr("ERROR: Failed to transfer wireless authmode: %d",
              (int)wifi_auth);
        break;
    }

  return auth_mode;
}

/****************************************************************************
 * Name: bl616_wifi_cipher_trans
 *
 * Description:
 *   Converts a cipher type values to WEXT cipher type values.
 *
 * Input Parameters:
 *   wifi_cipher - bl616 cipher type
 *
 * Returned Value:
 *     cipher type
 *
 ****************************************************************************/

static int bl616_wifi_cipher_trans(uint32_t wifi_cipher)
{
  int cipher_mode = IW_AUTH_CIPHER_NONE;

  switch (wifi_cipher)
    {
      case WIFI_CIPHER_TYPE_NONE:
        cipher_mode = IW_AUTH_CIPHER_NONE;
        break;

      case WIFI_CIPHER_TYPE_WEP40:
        cipher_mode = IW_AUTH_CIPHER_WEP40;
        break;

      case WIFI_CIPHER_TYPE_WEP104:
        cipher_mode = IW_AUTH_CIPHER_WEP104;
        break;

      case WIFI_CIPHER_TYPE_TKIP:
        cipher_mode = IW_AUTH_CIPHER_TKIP;
        break;

      case WIFI_CIPHER_TYPE_CCMP:
      case WIFI_CIPHER_TYPE_TKIP_CCMP:
        cipher_mode = IW_AUTH_CIPHER_CCMP;
        break;

      case WIFI_CIPHER_TYPE_AES_CMAC128:
        cipher_mode = IW_AUTH_CIPHER_AES_CMAC;
        break;

      default:
        wlerr("ERROR: Failed to transfer wireless authmode: %d",
               (int)wifi_cipher);
        break;
    }

  return cipher_mode;
}
#endif

/****************************************************************************
 * Name: bl616_freq_to_channel
 *
 * Description:
 *   convert freq to channel
 *
 * Input Parameters:
 *   freq
 *
 * Returned Value:
 *   channel id
 *
 ****************************************************************************/

static int bl616_freq_to_channel(int freq)
{
  int channel = freq;

  if (channel <= 14)
    {
      return channel;
    }

  if (freq >= 2412 && freq <= 2484)
    {
      if (freq == 2484)
        {
          channel = 14;
        }
      else
        {
          channel = freq - 2407;
          if (channel % 5)
            {
              return 0;
            }

          channel /= 5;
        }

      return channel;
    }

  if (freq >= 5005 && freq < 5900)
    {
      if (freq % 5)
        {
          return 0;
        }

      channel = (freq - 5000) / 5;
      return channel;
    }

  if (freq >= 4905 && freq < 5000)
    {
      if (freq % 5)
        {
          return 0;
        }

      channel = (freq - 4000) / 5;
      return channel;
    }

  return 0;
}

/****************************************************************************
 * Name: bl616_channel_to_freq
 *
 * Description:
 *  convert channel to freq
 *
 * Input Parameters:
 *   channel
 *
 * Returned Value:
 *   freq
 *
 ****************************************************************************/

static inline uint16_t bl616_channel_to_freq(int channel)
{
  if ((channel >= 1) && (channel <= 14))
    {
      if (channel == 14)
        return 2484;
      else
        return 2407 + channel * 5;
    }

  return 0;
}

/****************************************************************************
 * Function: format_scan_result_to_wapi
 *
 * Description:
 *   scan result from wifiMgmr to wapi
 *
 * Input Parameters:
 *   req - Reference to iwreq
 *
 * Returned Value:
 *   OK or negative value.
 *
 ****************************************************************************/

static int rssi_compare(const void *arg1, const void *arg2)
{
  uintptr_t ptr1 = *(uintptr_t *)arg1;
  uintptr_t ptr2 = *(uintptr_t *)arg2;

  struct wl80211_scan_result_item *item1 =
    (struct wl80211_scan_result_item *)ptr1;
  struct wl80211_scan_result_item *item2 =
    (struct wl80211_scan_result_item *)ptr2;

  return item1->rssi - item2->rssi;
}

static int format_scan_result_to_wapi(struct iwreq *req)
{
  int i = 0;
  int j = 0;
  int result_cnt = 0;
  int event_buff_len = 0;
  uint8_t *curr_pos = NULL;

  uintptr_t *rssi_list = NULL; /* for sort */

  struct wl80211_scan_result_item *n, *tmp;

  /* Count wl80211 scan results.  The scan-result lock serializes with the
   * WiFi task producer; nothing may allocate, free or sleep under it.
   */

  wl80211_scan_result_lock();
  RB_FOREACH_SAFE(n, _scan_result_tree, &wl80211_scan_result, tmp)
  {
    result_cnt++;
  }

  wl80211_scan_result_unlock();

  if (result_cnt == 0)
    {
      return -ENOENT;
    }

  /* More compact arrangement */

  event_buff_len =
    result_cnt * (offsetof(struct iw_event, u) * 4 + sizeof(struct sockaddr) +
                  sizeof(struct iw_freq) + sizeof(struct iw_quality) +
                  sizeof(struct iw_point) + IW_ESSID_MAX_SIZE);

  if (req->u.data.length == 0 || req->u.data.length < event_buff_len)
    {
      return -E2BIG;
    }

  rssi_list = kmm_malloc(result_cnt * sizeof(uintptr_t));
  if (rssi_list == NULL)
    {
      return -ENOMEM;
    }

  /* Unlink at most result_cnt items from the tree under the lock; the
   * unlinked items are owned by this call, so sorting, formatting and
   * freeing them run without the lock.
   */

  j = 0;

  wl80211_scan_result_lock();
  RB_FOREACH_SAFE(n, _scan_result_tree, &wl80211_scan_result, tmp)
  {
    if (j == result_cnt)
      {
        break; /* The tree grew after counting */
      }

    RB_REMOVE(_scan_result_tree, &wl80211_scan_result, n);
    rssi_list[j++] = (uintptr_t)n;
  }

  wl80211_scan_result_unlock();

  /* Another reader may have drained items after counting */

  result_cnt = j;
  if (result_cnt == 0)
    {
      kmm_free(rssi_list);
      return -ENOENT;
    }

  /* Sort the valid list according the rssi using custom comparator */

  qsort(rssi_list, result_cnt, sizeof(uintptr_t), rssi_compare);

  /* Construct iw event buffer */

  curr_pos = req->u.data.pointer;
  DEBUGASSERT(curr_pos != NULL);
  DEBUGASSERT(((uintptr_t)curr_pos & 0x3) == 0);

  for (i = 0; i < result_cnt; i++)
    {
      struct wl80211_scan_result_item *scan =
        (struct wl80211_scan_result_item *)(uintptr_t)rssi_list[i];

      struct iw_event *iwe;
      uint8_t *essid;

      iwe = (struct iw_event *)curr_pos;
      DEBUGASSERT(((uintptr_t)iwe & 0x3) == 0);
      iwe->len = offsetof(struct iw_event, u) + sizeof(struct sockaddr);
      iwe->cmd = SIOCGIWAP;
      memcpy(iwe->u.ap_addr.sa_data, scan->bssid, sizeof(struct ether_addr));

      iwe = (struct iw_event *)((uintptr_t)iwe + iwe->len);
      DEBUGASSERT(((uintptr_t)iwe & 0x3) == 0);
      iwe->len = offsetof(struct iw_event, u) + sizeof(struct iw_freq);
      iwe->cmd = SIOCGIWFREQ;
      iwe->u.freq.e = 0;
      iwe->u.freq.m = scan->channel;

      iwe = (struct iw_event *)((uintptr_t)iwe + iwe->len);
      DEBUGASSERT(((uintptr_t)iwe & 0x3) == 0);
      iwe->len = offsetof(struct iw_event, u) + sizeof(struct iw_quality);
      iwe->cmd = IWEVQUAL;
      iwe->u.qual.level = scan->rssi;
      iwe->u.qual.updated = IW_QUAL_DBM;

      iwe = (struct iw_event *)((uintptr_t)iwe + iwe->len);
      DEBUGASSERT(((uintptr_t)iwe & 0x3) == 0);
      iwe->len = offsetof(struct iw_event, u) + sizeof(struct iw_point) +
                 IW_ESSID_MAX_SIZE;
      iwe->cmd = SIOCGIWESSID;
      iwe->u.essid.length =
        scan->ssid ? strnlen(scan->ssid, IW_ESSID_MAX_SIZE) : 0;
      iwe->u.essid.flags = 1;
      /* refer:wapi wireless.c:272 */
      iwe->u.essid.pointer = (void *)(uintptr_t)sizeof(struct iw_point);
      essid = (uint8_t *)iwe + offsetof(struct iw_event, u) +
              sizeof(struct iw_point);
      memset(essid, 0x0, IW_ESSID_MAX_SIZE);
      if (scan->ssid != NULL)
        {
          memcpy(essid, scan->ssid, iwe->u.essid.length);
        }

      curr_pos = (uint8_t *)(uintptr_t)iwe + iwe->len;
    }

  req->u.data.length = curr_pos - (uint8_t *)req->u.data.pointer;

  /* Free memory for all nodes removed from tree earlier
   * Nodes were already removed from the tree during the first traversal,
   * now we just need to free the associated memory */

  for (i = 0; i < result_cnt; i++)
    {
      struct wl80211_scan_result_item *scan_item =
        (struct wl80211_scan_result_item *)(uintptr_t)rssi_list[i];
      if (scan_item->ssid != NULL)
        {
          kmm_free((void *)scan_item->ssid);
        }
      kmm_free(scan_item);
    }

  kmm_free(rssi_list);

  return OK;

}

/****************************************************************************
 * Name: bl616_wifi_sta_clear_info
 *
 * Description:
 *   Clear Wi-Fi station info
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616_wifi_sta_clear_info(void)
{
  memset(g_wifi_cfg.pwd, 0x0, sizeof(g_wifi_cfg.pwd));
  g_wifi_cfg.pwd_len = 0;

  memset(g_wifi_cfg.bssid, 0x0, sizeof(g_wifi_cfg.bssid));
  memset(g_wifi_cfg.ssid, 0x0, sizeof(g_wifi_cfg.ssid));
  memset(g_wifi_cfg.pmk, 0x0, sizeof(g_wifi_cfg.pmk));
  g_wifi_cfg.pmk_valid = false;
  g_wifi_cfg.ssid_len = 0;
  g_wifi_cfg.channel = 0;
  g_wifi_cfg.freq = 0;
}

/****************************************************************************
 * Name: bl616_wifi_sta_set_quick_connect
 *
 * Description:
 *   Set quick connect flag
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void bl616_wifi_sta_set_quick_connect(bool quick_connect)
{
}

/****************************************************************************
 * Name: bl616_wifi_sta_set_lowreate_connect
 *
 * Description:
 *   Set low rate connect flag
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void bl616_wifi_sta_set_lowrate_connect(bool lowrate_connect)
{
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __assert_func
 *
 * Description:
 *   Delete timer and free resource
 *
 * Input Parameters:
 *   file  - assert file
 *   line  - assert line
 *   func  - assert function
 *   expr  - assert condition
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void __assert_func(const char *file, int line,
                   const char *func, const char *expr)
{
  wlerr("ERROR: Assert failed in %s, %s:%d (%s)",
        func, file, line, expr);

  PANIC();
}


/**
 ************************************************************************
 * Name: wifi_task_suspend
 *
 * Description:
 *  Suspend the WiFi task
 *
 * Input Parameters:
 *  None
 *
 * Returned Value:
 * None
 * ***********************************************************************
 */

void wifi_task_suspend(void)
{
  extern bool coex_coord_on_wifi_suspend_enter(void);
  extern void coex_coord_on_wifi_wake(bool slept_committed);

  bool slept_committed = coex_coord_on_wifi_suspend_enter();

  /* Wait for notification using semaphore */

  nxsem_wait(&g_wifi_notify_sem);

  coex_coord_on_wifi_wake(slept_committed);
}

/**
 ************************************************************************
 * Name: wifi_task_resume
 *
 * Description:
 *  Resume the WiFi task
 *
 * Input Parameters:
 *  isr - Whether called from interrupt context
 *
 * Returned Value:
 * None
 * ***********************************************************************
 */

void wifi_task_resume(bool isr)
{
  int ret;

  /* NuttX doesn't distinguish between ISR and task context for semaphores */
  ret = nxsem_post(&g_wifi_notify_sem);

  if (ret != 0)
    {
      wlerr("failed to resume WiFi task: %d\n", ret);
    }
}

/****************************************************************************
 * Name: wifi_sys_now_ms
 *
 * Description:
 *   Get system time in milliseconds
 *
 * Input Parameters:
 *   isr - Whether called from interrupt context
 *
 * Returned Value:
 *   System time in milliseconds
 *
 ****************************************************************************/

uint32_t wifi_sys_now_ms(bool isr)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/****************************************************************************
 * Name: bl616_wifi_adapter_init
 *
 * Description:
 *   Initialize Wi-Fi adapter
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

int bl616_wifi_adapter_init(void)
{
  int semcount;

  wlinfo("Starting wifi ...\r\n");

  /* enable wifi clock (bit-mask argument of GLB_PER_Clock_UnGate) */

  GLB_PER_Clock_UnGate(GLB_AHB_CLOCK_WIFI_PHY |
                       GLB_AHB_CLOCK_WIFI_MAC_PHY |
                       GLB_AHB_CLOCK_WIFI_PLATFORM);
  /* Load and apply RF calibration parameters (TLV in flash) BEFORE the
   * WiFi task starts: wl_init()/phy chain reads wl_cfg power tables from
   * these parameters during mm_init policy-table setup.
   */

  rfparam_init(0, NULL, 1);

  GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_WIFI);

  /* Enable wifi irq */

  extern void interrupt0_handler(void);
  extern void wifi_main(void *arg);

  if (irq_attach(BL616CL_IRQ_NUM_WIFI, (xcpt_t)interrupt0_handler, NULL) == OK)
    {
      up_enable_irq(BL616CL_IRQ_NUM_WIFI);
    }

  task_create(CONFIG_BL616CL_FW_TASK_NAME,
              CONFIG_BL616CL_FW_TASK_PRIORITY,
              WIFI_TASK_STACK_SIZE,
              (main_t)wifi_main,
              NULL);

  /* macswl_init() in the new task resets the kernel message queue and
   * memory, so send nothing before the task is done with it, that is, until
   * it first waits in wifi_task_suspend().  A Wi-Fi task below the priority
   * of this thread used to lose the first request and hang boot.  Polling
   * keeps wifi_task_suspend(), which is on the hot path, unchanged.
   */

  while (nxsem_get_value(&g_wifi_notify_sem, &semcount) == OK &&
         semcount >= 0)
    {
      nxsig_usleep(1000);
    }

  uint8_t eth_mac[6];
  platform_get_mac(WL80211_VIF_STA, eth_mac);

  wl80211_init();

  /* The scan path requires a country channel plan; nothing in the NuttX
   * boot flow sets one, so default to CN (channels 1-13).  Users can
   * override via SIOCSIWCOUNTRY. */

  if (wifi_mgmr_set_country_code("CN") != 0)
    {
      wlerr("ERROR: Failed to set default country code\n");
    }

  bl_wifi_sta_ps_active_ms(CONFIG_BL616CL_WLAN_PS_ACTIVETIME);

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_start
 *
 * Description:
 *   Start Wi-Fi station.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_start(void)
{
  g_wifi_cfg.mode = IW_MODE_INFRA;
  g_wifi_cfg.pta = IW_PTA_PRIORITY_BALANCED;

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_stop
 *
 * Description:
 *   Stop Wi-Fi station.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_stop(void)
{
  /* Clear sta info */

  g_wifi_cfg.pta = IW_PTA_PRIORITY_BALANCED;

  bl616_wifi_sta_clear_info();

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_register_txdone_cb
 *
 * Description:
 *   Register the station TX done callback function.
 *
 * Input Parameters:
 *   cb - The callback function
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616_wifi_sta_register_txdone_cb(wifi_txdone_cb_t cb)
{
  extern void internal_register_txdone_cb(void (*cb)(void));

  g_sta_txdone_cb = cb;
  internal_register_txdone_cb(bl616_wifi_sta_txdone);
}

void bl616_wifi_sta_txdone(void)
{
  if (g_sta_txdone_cb != NULL)
    {
      g_sta_txdone_cb(NULL);
    }
}


/****************************************************************************
 * Name: bl616_wifi_sta_send_data
 *
 * Description:
 *   Send data to Wi-Fi station.
 *
 * Input Parameters:
 *   iob - The IOB containing the packet to be sent
 *   llhdrlen - The length of the link layer header
 *   offset - The offset of the data in the IOB
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_send_data(struct iob_s *iob,
                             uint16_t llhdrlen,
                             uint16_t offset)
{
  int ret = OK;

  extern int wl80211_output(struct iob_s *buf);
  ret = wl80211_output(iob);

  return ret;
}

/****************************************************************************
 * Name: bl616_wifi_sta_register_recv_cb
 *
 * Description:
 *   Register Wi-Fi station receive packet callback function
 *
 * Input Parameters:
 *   recv_cb - Receive callback function
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_register_recv_cb(int (*recv_cb)(void *net,
                                                   void *buffer,
                                                   uint16_t len,
                                                   void *eb))
{
  typedef void (*rx_cb_type)(void *buf, void *addr, uint16_t len, void *free_fn);
  extern void internal_register_recv_cb(rx_cb_type cb);

  internal_register_recv_cb((rx_cb_type)recv_cb);

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_read_mac
 *
 * Description:
 *   Read MAC address from efuse
 *
 * Input Parameters:
 *   mac  - MAC address buffer pointer
 *   type - MAC address type
 *
 * Returned Value:
 *   0 if success or -1 if fail
 *
 ****************************************************************************/

int bl616_wifi_sta_read_mac(uint8_t *mac)
{
  return wifi_mgmr_sta_mac_get(mac);
}

/****************************************************************************
 * Name: bl616_wifi_set_password
 *
 * Description:
 *   Set/Get Wi-Fi station password
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set   - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_password(struct iwreq *iwr, bool set)
{
  struct iw_encode_ext *ext = iwr->u.encoding.pointer;
  uint8_t *pdata = ext->key;
  int ret;

  /* Serialize with bl616_wifi_sta_connect(), which reads pwd */

  ret = adapter_wifi_lock(true);
  if (ret < 0)
    {
      return ret;
    }

  if (set)
    {
      uint8_t len = ext->key_len;

      switch (ext->alg)
        {
          case IW_ENCODE_ALG_NONE:
            {
              memset(g_wifi_cfg.pwd, 0x0, sizeof(g_wifi_cfg.pwd));
              g_wifi_cfg.pwd_len = 0;
              break;
            }

          default:
            {
              if (len > PWD_MAX_LEN)
                {
                  ret = -EINVAL;
                  break;
                }

              memset(g_wifi_cfg.pwd, 0x0, sizeof(g_wifi_cfg.pwd));
              memcpy(g_wifi_cfg.pwd, pdata, len);
              g_wifi_cfg.pwd_len = len;
              break;
            }
        }
    }
  else
    {
      if (g_wifi_cfg.pwd_len == 0)
        {
          ext->alg = IW_ENCODE_ALG_NONE;
          ext->key_len = 0;
        }
      else
        {
          ext->alg = IW_ENCODE_ALG_CCMP;
          memcpy(pdata, g_wifi_cfg.pwd, g_wifi_cfg.pwd_len);
          ext->key_len = g_wifi_cfg.pwd_len;
        }
    }

  adapter_wifi_lock(false);
  return ret;
}

/****************************************************************************
 * Name: bl616_wifi_sta_essid
 *
 * Description:
 *   Set/Get Wi-Fi station ESSID
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_essid(struct iwreq *iwr, bool set)
{
  uint8_t *pdata = iwr->u.essid.pointer;

  if (set)
    {
      uint8_t len = iwr->u.essid.length;

      if (len > SSID_MAX_LEN)
        {
          return -EINVAL;
        }

      /* save ssid */

      memset(g_wifi_cfg.ssid, 0x0, SSID_MAX_LEN);
      memcpy(g_wifi_cfg.ssid, pdata, len);

      g_wifi_cfg.ssid_len = len;
      g_wifi_cfg.essid_flag = iwr->u.essid.flags;
    }
  else
    {
      memcpy(pdata, g_wifi_cfg.ssid, g_wifi_cfg.ssid_len);
      iwr->u.essid.flags = g_wifi_cfg.essid_flag;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_bssid
 *
 * Description:
 *   Set/Get Wi-Fi station BSSID
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set   - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_bssid(struct iwreq *iwr, bool set)
{
  struct iwreq *req = (struct iwreq *)iwr;
  int ret = OK;

  if (set)
    {
      memcpy(g_wifi_cfg.bssid, req->u.ap_addr.sa_data, MAC_LEN);
    }
  else
    {
      if (g_wifi_cfg.mode == IW_MODE_INFRA)
        {
          /* Get bssid */

          ret = wifi_mgmr_sta_get_bssid((uint8_t *)req->u.ap_addr.sa_data);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: is_ascii_hex_char
 *
 * Description:
 *   Is ascii character
 *
 * Input Parameters:
 *   c   - character
 *
 * Returned Value:
 *   0 if is character or -1 if is not character
 *
 ****************************************************************************/

static int is_ascii_hex_char(char c)
{
	if (c >= '0' && c <= '9')
		return 0;
	if (c >= 'a' && c <= 'f')
		return 0;
	if (c >= 'A' && c <= 'F')
		return 0;
	return -1;
}

/****************************************************************************
 * Name: bl616_wifi_sta_connect
 *
 * Description:
 *   Trigger Wi-Fi station connection action
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_connect(void)
{
  int ret;
  int retry;
  uint16_t status;
  uint16_t freq = 0;
  uint8_t bssid[18] = {0};

  ret = adapter_wifi_lock(true);
  if (ret < 0)
    {
      return ret;
    }

  if (g_sta_connected)
    {
      wlinfo("Wi-Fi has connected AP\n");
      adapter_wifi_lock(false);
      return OK;
    }

  snprintf((char *)bssid,
           sizeof(bssid),
           "%02x:%02x:%02x:%02x:%02x:%02x",
           g_wifi_cfg.bssid[0],
           g_wifi_cfg.bssid[1],
           g_wifi_cfg.bssid[2],
           g_wifi_cfg.bssid[3],
           g_wifi_cfg.bssid[4],
           g_wifi_cfg.bssid[5]);
  bssid[17] = '\0';

  wlinfo("connect ssid:%s\n", g_wifi_cfg.ssid);
  // wlinfo("connect pwd:%s\n", g_wifi_cfg.pwd);
  wlinfo("connect bssid:%s\n", bssid);
  wlinfo("connect channel:%d(%d)\n", g_wifi_cfg.channel, g_wifi_cfg.freq);

  if (g_wifi_cfg.pwd_len == PWD_MAX_LEN)
    {
      char *key = g_wifi_cfg.pwd;

      for (int i = 0; i < PWD_MAX_LEN; ++i)
        {
          if (is_ascii_hex_char(key[i]) < 0)
            {
              wlwarn("WARN: password is not hex string\n");

              adapter_wifi_lock(false);
              return -16;
            }
        }
    }

  /* Set channel */

  freq = g_wifi_cfg.freq;

  bl616_wifi_sta_set_quick_connect(true);

  /* An AP may drop a PMF station in its driver only (e.g. Broadcom
   * "wl deauthenticate") while hostapd keeps the old association.  The
   * next association then succeeds, but the AP sends SA Queries instead
   * of EAPOL M1 until our 4-way handshake timeout deauths and clears the
   * stale entry.  Try once more in that case: the status is PSK_TIMEOUT
   * only when no EAPOL frame came at all, a wrong key reports
   * WLAN_FW_AUTHENTICATION_FAIILURE instead.
   */

  for (retry = 1; ; retry--)
    {
      /* Set connect block */

      g_sta_block = true;

      {
        wifi_mgmr_sta_connect_params_t params;
        char bssid_str[MGMR_BSSID_LEN + 1] = { 0 };

        /* Convert BSSID array to string format */

        snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 g_wifi_cfg.bssid[0], g_wifi_cfg.bssid[1], g_wifi_cfg.bssid[2],
                 g_wifi_cfg.bssid[3], g_wifi_cfg.bssid[4], g_wifi_cfg.bssid[5]);

        memset(&params, 0, sizeof(params));
        memcpy(params.ssid, g_wifi_cfg.ssid, g_wifi_cfg.ssid_len);
        memcpy(params.key,
               g_wifi_cfg.pmk_valid ? g_wifi_cfg.pmk : g_wifi_cfg.pwd,
               g_wifi_cfg.pmk_valid ? MGMR_KEY_LEN : g_wifi_cfg.pwd_len);
        memcpy(params.bssid_str, bssid_str, sizeof(bssid_str));
        params.freq1 = freq;
        params.pmf_cfg = 1;

        ret = wifi_mgmr_sta_connect(&params);
      }

      if (ret < 0)
        {
          wlerr("ERROR: Failed to connect Wi-Fi ret=%d\n", ret);

          g_sta_block = false;

          adapter_wifi_lock(false);
          return ret;
        }

      ret = nxsem_tickwait_uninterruptible(
        &g_wifi_wait_connect_sem,
        SEC2TICK(CONFIG_BL616CL_WLAN_CONNECT_TIMEOUT));

      g_sta_block = false;

      /* check connect state */

      status = wifi_mgmr_sta_info_status_code_get();

      if (retry == 0 || ret < 0 ||
          status != WLAN_FW_4WAY_HANDSHAKE_ERROR_PSK_TIMEOUT_FAILURE)
        {
          break;
        }

      wlwarn("WARN: no 4-way handshake from AP, retry\n");
    }

  if (ret < 0 || status != WLAN_FW_SUCCESSFUL)
    {
      wlerr("ERROR: connect Wi-Fi ret=%d status=%u\n", ret, status);

      if (ret == 0)
        {
          switch (status)
            {
              case WLAN_FW_4WAY_HANDSHAKE_ERROR_PSK_TIMEOUT_FAILURE:
              case WLAN_FW_DEAUTH_BY_AP_WHEN_NOT_CONNECTION:
              case WLAN_FW_AUTHENTICATION_FAIILURE:
              case WLAN_FW_DEAUTH_BY_AP_WHEN_CONNECTION:
                ret = -WLAN_STATUS_AUTH_TIMEOUT;
                break;

              default:
                ret = -EIO;
                break;
            }
        }

      /* Clear sta info */

      bl616_wifi_sta_clear_info();

    }

  adapter_wifi_lock(false);

  return ret;
}

/****************************************************************************
 * Name: bl616_wifi_sta_disconnect
 *
 * Description:
 *   Trigger Wi-Fi station disconnection action
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_disconnect(void)
{
  int ret;

  ret = adapter_wifi_lock(true);

  wlinfo("disconnect Wi-Fi\n");

  if (ret < 0)
    {
      wlwarn("WARN: Failed to lock Wi-Fi ret=%d\n", ret);
      return ret;
    }

  ret = wifi_mgmr_sta_disconnect();

  adapter_wifi_lock(false);
  return ret;
}

/****************************************************************************
 * Name: bl616_wifi_sta_mode
 *
 * Description:
 *   Set/Get Wi-Fi Station mode code.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_mode(struct iwreq *iwr, bool set)
{
  if (set)
    g_wifi_cfg.mode = iwr->u.mode;
  else
    iwr->u.mode = g_wifi_cfg.mode;

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_auth
 *
 * Description:
 *   Set/Get station authentication mode params.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_auth(struct iwreq *iwr, bool set)
{
  int cmd;

  if (set)
    {
      return OK;
    }
  else
    {
      if (!g_sta_connected)
        {
          return -ENOTCONN;
        }

#if 0
      ret = bl616_wifi_sta_find_ap_info(&ap_info);
      if (ret)
        {
          wlerr("ERROR: Failed to get AP record ret=%d\n", ret);
          return -ENOSYS;
        }
#endif

      cmd = iwr->u.param.flags & IW_AUTH_INDEX;
      switch (cmd)
        {
          case IW_AUTH_WPA_VERSION:
            iwr->u.param.value = IW_AUTH_WPA_VERSION_WPA2;
            break;

          case IW_AUTH_CIPHER_PAIRWISE:
            iwr->u.param.value = IW_AUTH_CIPHER_CCMP;
            break;

          case IW_AUTH_CIPHER_GROUP:
            iwr->u.param.value = IW_AUTH_CIPHER_CCMP;
            break;

          case IW_AUTH_KEY_MGMT:
          case IW_AUTH_TKIP_COUNTERMEASURES:
          case IW_AUTH_DROP_UNENCRYPTED:
          case IW_AUTH_80211_AUTH_ALG:
          case IW_AUTH_WPA_ENABLED:
          case IW_AUTH_RX_UNENCRYPTED_EAPOL:
          case IW_AUTH_ROAMING_CONTROL:
          case IW_AUTH_PRIVACY_INVOKED:
          default:
            wlerr("ERROR: Unknown cmd %d\n", cmd);
            return -ENOSYS;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_freq
 *
 * Description:
 *   Set/Get station frequency.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_freq(struct iwreq *iwr, bool set)
{
  struct iwreq *req = (struct iwreq *)iwr;

  /* FIXME: wl80211 use uint8_t, wifi_mgmr use int */
  uint8_t channel;

  if (set && (iwr->u.freq.flags == IW_FREQ_FIXED))
    {
      if (req->u.freq.e != 0)
        {
          return -EINVAL;
        }

      g_wifi_cfg.channel = bl616_freq_to_channel(req->u.freq.m);
      g_wifi_cfg.freq = bl616_channel_to_freq(g_wifi_cfg.channel);

      if (g_wifi_cfg.channel == 0)
        return -EINVAL;
      else
        {
          wlinfo("set channel: %d, freq: %d\n",
                 g_wifi_cfg.channel,
                 g_wifi_cfg.freq);
          return OK;
        }
    }
  else
    {
      if (wifi_mgmr_sta_channel_get(&channel) < 0)
        {
          iwr->u.freq.flags = IW_FREQ_AUTO;
          iwr->u.freq.e = 0;
          iwr->u.freq.m = 2412;
        }
      else
        {
          iwr->u.freq.flags = IW_FREQ_FIXED;
          req->u.freq.e = 0;
          iwr->u.freq.m = 2407 + 5 * channel;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_bitrate
 *
 * Description:
 *   Get station default bit rate (Mbps).
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_bitrate(struct iwreq *iwr, bool set)
{
  if (set)
    {
      return -ENOSYS;
    }
  else
    {
      iwr->u.bitrate.fixed = IW_FREQ_AUTO;
      iwr->u.bitrate.value = 0;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_txpower
 *
 * Description:
 *   Get station transmit power (dBm).
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_txpower(struct iwreq *iwr, bool set)
{
  int8_t power;

  if (set)
    {
      struct iwreq *req = (struct iwreq *)iwr;
      uint8_t power_type = req->u.txpower.flags & IW_TXPOW_TYPE;
      power = (int8_t)req->u.txpower.value;

      wlinfo("set ant gain:%d dbm, type %d\n", power, power_type);

      if (power_type == IW_TXPOW_DBM)
        {
          rfparam_update_ant_gain(power);

          rfparam_init(0, NULL, 1);
        }
      else
        {
          wlwarn("WARNING: SIOCSIWTXPOW not implemented\n");
          return -ENOSYS;
        }
    }
  else
    {
      struct iwreq *req = (struct iwreq *)iwr;
      int8_t ant_gain = rfparam_get_ant_gain();

      req->u.txpower.value    = ant_gain;
      req->u.txpower.disabled = 0;
      req->u.txpower.flags    = IW_TXPOW_DBM;

      wlinfo("get ant gain:%d dbm\n", ant_gain);
    }

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_pta
 *
 * Description:
 *   Get station PTA priority parameters.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_pta(struct iwreq *iwr, bool set)
{
  if (set)
    {
      return -ENOSYS;
    }
  else
    {
      iwr->u.param.value = IW_PTA_PRIORITY_BALANCED;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_channel
 *
 * Description:
 *   Get station range of channel parameters.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_channel(struct iwreq *iwr, bool set)
{
  int ret;
  int k;
  char country_code[3];
  struct iw_range *range;

  if (set)
    return -ENOSYS;
  else
    {
      ret = wifi_mgmr_get_country_code(country_code);
      if (ret < 0)
        {
          return -EINVAL;
        }

      range = (struct iw_range *)iwr->u.data.pointer;
      wifi_mgmr_get_channel_nums(country_code,
                                 &range->num_frequency,
                                 NULL);

      for (k = 1; k <= range->num_frequency; k++)
        {
          range->freq[k - 1].i = k;
          range->freq[k - 1].e = 0;
          range->freq[k - 1].m = 2407 + 5 * k;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: bl616_wifi_sta_country
 *
 * Description:
 *   Configure country info.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_country(struct iwreq *iwr, bool set)
{
  int ret;

  if (set)
    {
      if (wifi_mgmr_set_country_code(iwr->u.data.pointer) == 0)
        {
          if (rfparam_set_country_code(iwr->u.data.pointer) == 0)
            {
              rfparam_init(0, NULL, 1);
            }

          ret = OK;
        }
      else
        {
          wlerr("ERROR: Failed to set country code\n");
          ret = -EINVAL;
        }
    }
  else
    ret = wifi_mgmr_get_country_code(iwr->u.data.pointer);

  if (ret != 0)
    {
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_rssi
 *
 * Description:
 *   Get Wi-Fi sensitivity (dBm).
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_rssi(struct iwreq *iwr, bool set)
{
  int rssi = 0;

  if (set)
    {
      return -ENOSYS;
    }

  wifi_mgmr_sta_rssi_get(&rssi);

  if (rssi == -1)
    {
      return -EIO;
    }

  iwr->u.sens.value = -rssi;

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_scan
 *
 * Description:
 *   Scan APs.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_scan(struct iwreq *iwr)
{
  struct iw_scan_req *req;
  char ssid[SSID_MAX_LEN + 1] = { 0 };
  int ret;
  wifi_mgmr_scan_params_t config;

  /* Wait for scan to complete */

  ret = nxsem_trywait(&g_wifi_scan_sem);
  if (ret < 0)
    {
      wlerr("ERROR: start scan failed\n");
      return -EBUSY;
    }

  memset(&config, 0, sizeof(wifi_mgmr_scan_params_t));

  if (iwr->u.data.pointer && iwr->u.data.length >= sizeof(struct iw_scan_req))
    {
      req = (struct iw_scan_req *)iwr->u.data.pointer;

      if (iwr->u.data.flags & IW_SCAN_THIS_ESSID &&
          req->essid_len < (SSID_MAX_LEN + 1))
        {
          /* Scan specific ESSID */

          memcpy(config.ssid_array, req->essid, req->essid_len);
          config.ssid_length =
            req->essid_len > SSID_MAX_LEN ? SSID_MAX_LEN : req->essid_len;

          memcpy(ssid, config.ssid_array, config.ssid_length);
        }
      else
        {
          wlwarn("Params error, Scan all ESSID\n");
        }

      if (req->num_channels > 0)
        {
          /* Scan specific channels */

          DEBUGASSERT(req->num_channels <= MAX_FIXED_CHANNELS_LIMIT);

          for (; config.channels_cnt < req->num_channels &&
                 config.channels_cnt < MAX_FIXED_CHANNELS_LIMIT;
               config.channels_cnt++)
            {
              config.channels[config.channels_cnt] =
                req->channel_list[config.channels_cnt].m;
            }
        }

      if (memcmp(req->bssid.sa_data, config.bssid, MAC_LEN) != 0)
        {
          /* Scan specific bssid */

          config.bssid_set_flag = 1;
          memcpy(config.bssid, req->bssid.sa_data, MAC_LEN);
        }
    }

  config.duration = CONFIG_BL616CL_SCAN_DURANTION;

  /* TODO: passive scan */
  /* TODO: probt cnt */



  wlinfo("Start scan ssid:%s, bssid:%X:%X:%X:%X:%X:%X, channel:%d\n",
         ssid,
         config.bssid[0],
         config.bssid[1],
         config.bssid[2],
         config.bssid[3],
         config.bssid[4],
         config.bssid[5],
         config.channels[0]);

  /* Start scan */

  if (OK != wifi_mgmr_sta_scan(&config))
    {
      sem_post(&g_wifi_scan_sem);
      wlerr("ERROR: exec scan failed\n");
      return -EIO;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_dtim
 *
 * Description:
 *   Set/Get DTIM interval.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_dtim(struct iwreq *iwr, bool set)
{
  struct iwreq *req = (struct iwreq *)iwr;
  DEBUGASSERT(req != NULL);

  if (set)
    {
      uint8_t dtim = req->u.param.value;
      g_wifi_cfg.dtim = dtim;
      wlinfo("set dtim: %d\n", dtim);
    }
  else
    {
      req->u.param.value = g_wifi_cfg.dtim;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_powersave
 *
 * Description:
 *   Set/Get power save mode.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_powersave(struct iwreq *iwr, bool set)
{
  struct iwreq *req = (struct iwreq *)iwr;

  DEBUGASSERT(req != NULL);

  if (set)
    {
      bool on = req->u.power.flags;
      if (on)
        {
          wifi_mgmr_sta_ps_enter();
        }
      else
        {
          wifi_mgmr_sta_ps_exit();
        }

      /* Only save power save mode when set successfully */

      g_wifi_cfg.powersave = on;
    }
  else
    {
      req->u.power.flags = g_wifi_cfg.powersave;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_pmksa
 *
 * Description
 *   Set/Get PMKSA cache.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_pmksa(struct iwreq *iwr, bool set)
{
  uint8_t *pmk = iwr->u.data.pointer;
  uint16_t len = iwr->u.data.length;
  uint8_t result[32] = { 0 };
  int ret = OK;

  if (len != PWD_MAX_LEN)
    {
      wlerr("ERROR: Invalid PMK length %d\n", len);
      return -EINVAL;
    }

  if (set)
    {
      uint32_t flag = up_irq_save();
      memcpy(g_wifi_cfg.pmk, pmk, len);
      g_wifi_cfg.pmk[PWD_MAX_LEN] = 0;
      g_wifi_cfg.pmk_valid = true;
      up_irq_restore(flag);
    }
  else
    {
      /* Check if password exists */

      if (g_wifi_cfg.pwd_len > 0)
        {
          /* Only support WPA2-PSK and WPA-PSK */

          wifi_mgmr_connect_ind_stat_info_t conn_info;
          wifi_mgmr_sta_connect_ind_stat_get(&conn_info);
          if (conn_info.security == WIFI_EVENT_BEACON_IND_AUTH_WPA_PSK ||
              conn_info.security == WIFI_EVENT_BEACON_IND_AUTH_WPA2_PSK ||
              conn_info.security == WIFI_EVENT_BEACON_IND_AUTH_WPA_WPA2_PSK)

            {
              ret = pbkdf2_sha1(g_wifi_cfg.pwd,
                                (uint8_t *)g_wifi_cfg.ssid,
                                g_wifi_cfg.ssid_len,
                                4096,
                                result,
                                32);
              if (ret != 0)
                {
                  wlerr("ERROR: Failed to get PMKSA cache\n");
                  return -EIO;
                }

              bin2hex(result, 32, (char *)pmk, PWD_MAX_LEN);
            }
          else
            {
              wlerr("ERROR: Unsupported security mode %d\n",
                    conn_info.security);
              return -ENOSYS;
            }
        }
      else
        {
          wlerr("ERROR: Password is not set\n");
          return -EIO;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: bl616_wifi_sta_scan_result
 *
 * Description:
 *   Get scan result.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int bl616_wifi_sta_scan_result(struct iwreq *iwr)
{
  int ret;

  ret = sem_trywait(&g_wifi_scan_sem);
  if (ret < 0)
    {
      return -EAGAIN;
    }

  /* Format scan results */

  ret = format_scan_result_to_wapi(iwr);

  sem_post(&g_wifi_scan_sem);
  return ret;
}

/****************************************************************************
 * Name: bl616_wifi_event_handler
 *
 * Description:
 *   handle wifi event from firmware
 *
 * Input Parameters:
 *   dev - Reference to the NuttX driver state structure
 *
 * Returned Value:
 *   OK on success; a negated errno on failure
 *
 ****************************************************************************/

void bl616_wifi_event_handler(void *arg1, uint32_t arg2)
{
  irqstate_t flags;

  uint32_t code1 = (uint32_t)arg1;

  switch (code1)
    {
      case WL80211_EVT_STA_CONNECTED:
        {
          /* Notify the connecting thread */
          if (g_sta_block)
            {
              if (OK != sem_post(&g_wifi_wait_connect_sem))
                wlerr("ERROR: Failed to post semaphore\n");
            }

          /* TODO: support lowpower */
          // PLATFORM_HOOK(prevent_sleep, PSM_EVENT_CONNECT, 0);

          /* FIXME: not support now */
#if 0
          wifi_mgmr_connect_ind_stat_info_t conn_info;

          if (wifi_mgmr_sta_connect_ind_stat_get(&conn_info) == 0)
            {
              char *auth_str = wifi_mgmr_auth_to_str(conn_info.auth_mode);
              char *cipher_str = wifi_mgmr_cipher_to_str(conn_info.cipher_type);
              wlinfo("认证模式: %s\n", auth_str);   // "WPA2-PSK", "WPA3-SAE" 等
              wlinfo("加密方式: %s\n", cipher_str); // "AES", "TKIP" 等
            }
#endif

          if (!g_sta_connected)
            bl616_wlan_sta_set_linkstatus(true);

          flags = up_irq_save();
          g_sta_connected = true;
          up_irq_restore(flags);

          wlinfo("[APP] [EVT] CODE_WIFI_ON_CONNECTED\n");
          break;
        }
      case WL80211_EVT_SCAN_DONE:
        {
          /* Scan done, wake up the waiting thread */

          sem_post(&g_wifi_scan_sem);
          break;
        }
      case WL80211_EVT_STA_DISCONNECTED:
        {
          uint16_t status_code = (arg2 >> 16) & 0xFF;
          uint16_t reason_code = arg2 & 0xFF;

          /* Notify the connecting thread */

          if (g_sta_block)
            {
              if (OK != sem_post(&g_wifi_wait_connect_sem))
                wlerr("ERROR: Failed to post semaphore\n");
            }

          if (g_sta_connected)
            {
              /* Disconnect event */

              bl616_wlan_sta_set_linkstatus(false);

              /* Clear sta info */

              bl616_wifi_sta_clear_info();
            }

          flags = up_irq_save();
          g_sta_connected = false;
          up_irq_restore(flags);

#define wlprint(x, ...) syslog(LOG_INFO, x, ##__VA_ARGS__)
          wlprint("[APP] [EVT] CODE_WIFI_ON_DISCONNECT\n");
          wlprint("=================================================================\n");
          wlprint("[AT][RX] Connection Status\n");
          wlprint("[AT][RX]   status_code %u\n", status_code);
          wlprint("[AT][RX]   reason_code %u\n", reason_code);
          wlprint("[AT][RX]   status detail: %s\n", wifi_mgmr_get_sm_status_code_str(status_code));
          wlprint("=================================================================\n");
#undef wlprint

          wlinfo("[APP] [EVT] CODE_WIFI_ON_SCAN_DONE\n");
          break;
        }

      case WL80211_EVT_AP_STARTED:
      case WL80211_EVT_AP_STOPPED:
      case WL80211_EVT_AP_STA_ADDED:
      case WL80211_EVT_AP_STA_DEL:
        /* Not supported now */
        break;
    }
}
