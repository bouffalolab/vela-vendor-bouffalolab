/****************************************************************************

 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_wlan.c
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


#include <arpa/inet.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <stdio.h>
#include <sys/endian.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <arch/irq.h>
#include <nuttx/net/netdev.h>
#include <nuttx/nuttx.h>
#include <nuttx/queue.h>
#include <nuttx/wdog.h>
#include <nuttx/wqueue.h>
#include <nuttx/kthread.h>

#if defined(CONFIG_NET_PKT)
#include <nuttx/net/pkt.h>
#endif

#ifdef CONFIG_PM
#include <nuttx/power/pm.h>
#ifdef CONFIG_BL616CL_LOWPOWER
#include "bl616_lp.h"
#endif
#endif

#include "bl616cl_wifi_adapter.h"
#include "bl616cl_wlan.h"
#include "wl80211_mac.h"
#include "wifi_mgmr_ext.h"

#ifdef CONFIG_BL616CL_WLAN_SDIO
#include "sdiowifi_mgmr.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* TX timeout = 1 minute */

#define WLAN_TXTOUT  (60 * CLK_TCK)

/* Low-priority work queue processes RX/TX */

#define WLAN_WORK    LPWORK

#define SSID_MAX_LEN (32)
#define PWD_MAX_LEN  (64)

#ifdef CONFIG_BL616CL_WLAN_WORK_THREAD
#define NETDEV_THREAD_NAME_FMT "netdev-%s"
#define NETDEV_TX_CONTINUE 1 /* Return value for devif_poll */
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* WLAN operations */

/* Ethernet frame:
 *     Resource address   :   6 bytes
 *     Destination address:   6 bytes
 *     Type               :   2 bytes
 *     Payload            :   MAX 1500
 *     Checksum           :   Ignore
 *
 *     Total size         :   1514
 */

#define WLAN_BUF_SIZE \
  (CONFIG_NET_ETH_PKTSIZE + CONFIG_NET_LL_GUARDSIZE + CONFIG_NET_GUARDSIZE)

struct wlan_priv_s
{
  /* This holds the information visible to the NuttX network */

  struct net_driver_s dev;

  int ref; /* Reference count */

  bool ifup; /* true:ifup false:ifdown */

  struct wdog_s txtimeout; /* TX timeout timer */

  struct work_s rxwork;   /* Send packet work */
  struct work_s txwork;   /* Receive packet work */
  struct work_s toutwork; /* Send packet timeout work */
#ifdef CONFIG_BL616CL_WLAN_PROBE
  struct work_s probework; /* Probe work */
#endif

#ifdef CONFIG_PM
  struct work_s pmwork; /* PM work */
  bool pm_enabled;
  struct pm_wakelock_s wakelock;
#endif

  const struct wlan_ops_s *ops; /* WLAN operations */

  /* RX packet queue */

  struct iob_queue_s rxb;

  /* TX ready packet queue */

  struct iob_queue_s txb;
  struct iob_s *tx_pending;
#ifdef CONFIG_BL616CL_WLAN_WORK_THREAD
  pid_t tid;
  sem_t sem;
#endif
};

struct wlan_ops_s
{
  int (*start)(void);
  int (*send)(struct iob_s *iob, uint16_t llhdrlen, uint16_t offset);
  int (*essid)(struct iwreq *iwr, bool set);
  int (*bssid)(struct iwreq *iwr, bool set);
  int (*passwd)(struct iwreq *iwr, bool set);
  int (*mode)(struct iwreq *iwr, bool set);
  int (*auth)(struct iwreq *iwr, bool set);
  int (*freq)(struct iwreq *iwr, bool set);
  int (*bitrate)(struct iwreq *iwr, bool set);
  int (*txpower)(struct iwreq *iwr, bool set);
  int (*channel)(struct iwreq *iwr, bool set);
  int (*country)(struct iwreq *iwr, bool set);
  int (*rssi)(struct iwreq *iwr, bool set);
  int (*connect)(void);
  int (*disconnect)(void);
  int (*scan)(struct iwreq *iwr);
  int (*scan_result)(struct iwreq *iwr);
  int (*event)(pid_t pid, struct sigevent *event);
  int (*pta)(struct iwreq *iwr, bool set);
  int (*stop)(void);
  int (*dtim)(struct iwreq *iwr, bool set);
  int (*powersave)(struct iwreq *iwr, bool set);
  int (*pmksa)(struct iwreq *iwr, bool set);
};

/* The wlan_priv_s encapsulates all state information for a single
 * hardware interface
 */

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Reference count of register Wi-Fi handler */

static uint8_t g_callback_register_ref = 0;
static struct wlan_priv_s g_wlan_priv[BL616_WLAN_DEVS];

#ifdef BL616_WLAN_HAS_STA
static const struct wlan_ops_s g_sta_ops =
{
  .start       = bl616_wifi_sta_start,
  .send        = bl616_wifi_sta_send_data,
  .essid       = bl616_wifi_sta_essid,
  .bssid       = bl616_wifi_sta_bssid,
  .passwd      = bl616_wifi_sta_password,
  .mode        = bl616_wifi_sta_mode,
  .auth        = bl616_wifi_sta_auth,
  .freq        = bl616_wifi_sta_freq,
  .bitrate     = bl616_wifi_sta_bitrate,
  .txpower     = bl616_wifi_sta_txpower,
  .channel     = bl616_wifi_sta_channel,
  .country     = bl616_wifi_sta_country,
  .rssi        = bl616_wifi_sta_rssi,
  .connect     = bl616_wifi_sta_connect,
  .scan        = bl616_wifi_sta_scan,
  .scan_result = bl616_wifi_sta_scan_result,
  .disconnect  = bl616_wifi_sta_disconnect,
  .pta         = bl616_wifi_sta_pta,
  .stop        = bl616_wifi_sta_stop,
  .dtim        = bl616_wifi_sta_dtim,
  .powersave   = bl616_wifi_sta_powersave,
  .pmksa       = bl616_wifi_sta_pmksa,
};
#endif

#ifdef CONFIG_PM
struct bl616_wlan_pm_config_s
{
  struct pm_callback_s pm_cb;
};

static void up_pm_notify(struct pm_callback_s *cb,
                         int domain,
                         enum pm_state_e pmstate);

static struct bl616_wlan_pm_config_s g_wlan_pm =
{
  .pm_cb.notify  = up_pm_notify,
};

#endif /* CONFIG_PM */

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Common TX logic */

static void wlan_transmit(struct wlan_priv_s *priv);
static void wlan_rxpoll(void *arg);
static int wlan_txpoll(struct net_driver_s *dev);
static void wlan_dopoll(struct wlan_priv_s *priv);

/* Watchdog timer expirations */

static void wlan_txtimeout_work(void *arg);
static void wlan_txtimeout_expiry(wdparm_t arg);

/* NuttX callback functions */

static int wlan_ifup(struct net_driver_s *dev);
static int wlan_ifdown(struct net_driver_s *dev);

static void wlan_txavail_work(void *arg);
static int wlan_txavail(struct net_driver_s *dev);
static void wlan_sta_tx_done(void *arg);

#if defined(CONFIG_NET_MCASTGROUP) || defined(CONFIG_NET_ICMPv6)
static int wlan_addmac(struct net_driver_s *dev, const uint8_t *mac);
#endif

#ifdef CONFIG_NET_MCASTGROUP
static int wlan_rmmac(struct net_driver_s *dev, const uint8_t *mac);
#endif

#ifdef CONFIG_NETDEV_IOCTL
static int wlan_ioctl(struct net_driver_s *dev, int cmd, unsigned long arg);
#endif

#ifdef CONFIG_NET_ICMPv6
static void wlan_ipv6multicast(struct wlan_priv_s *priv);
#endif

#ifdef CONFIG_PM
static int bl616_wlan_pm_init(void);
static void up_pm_notify(struct pm_callback_s *cb,
                         int domain,
                         enum pm_state_e pmstate);
#endif

#ifdef CONFIG_BL616CL_WLAN_PROBE
static void wlan_sta_probe_status(struct wlan_priv_s *dev);
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Note:
 *     All TX done/RX done/Error trigger functions are not called from
 *     interrupts, this is much different from ethernet driver, including:
 *       * wlan_rx_done
 *       * wlan_tx_done
 *
 *     These functions are called in a Wi-Fi private thread. So we just use
 *     mutex/semaphore instead of disable interrupt, if necessary.
 */

// #define CONFIG_BL616CL_NET_DEBUG
#ifdef CONFIG_BL616CL_NET_DEBUG
static inline void dump_ethhdr(const char *msg,
                               unsigned char *buf,
                               int buflen)
{
  syslog(LOG_INFO, "WLAN: %s %d bytes\n", msg, buflen);
  syslog(LOG_INFO, "      %02x:%02x:%02x:%02x:%02x:%02x "
         "%02x:%02x:%02x:%02x:%02x:%02x %02x%02x\n",
         buf[0], buf[1], buf[2], buf[3], buf[4],  buf[5],
         buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
         buf[12], buf[13]
        );
}
#else
#define dump_ethhdr(m, b, l)
#endif

/****************************************************************************
 * Function: wlan_cache_txpkt_tail
 *
 * Description:
 *   Cache packet from dev->d_buf into tail of TX ready queue.
 *
 * Input Parameters:
 *   priv - Reference to the driver state structure
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void wlan_cache_txpkt_tail(struct wlan_priv_s *priv)
{
  int ret = iob_tryadd_queue(priv->dev.d_iob, &priv->txb);

  if (ret < 0)
    {
      wlerr("TX queue insertion failed: %d\n", ret);
      netdev_iob_release(&priv->dev);
      return;
    }

  netdev_iob_clear(&priv->dev);
}

/****************************************************************************
 * Name: wlan_upper_queue_work
 *
 * Description:
 *   Called when there is any work to do.
 *
 * Input Parameters:
 *   dev - Reference to the NuttX driver state structure
 *
 ****************************************************************************/

#ifdef CONFIG_BL616CL_WLAN_WORK_THREAD
static inline void wlan_upper_queue_work(struct wlan_priv_s *priv)
{
  int semcount;
  if (nxsem_get_value(&priv->sem, &semcount) == OK && semcount <= 0)
    {
      nxsem_post(&priv->sem);
    }
}

/****************************************************************************
 * Name: netdev_upper_txavail_work
 *
 * Description:
 *   Perform an out-of-cycle poll on a dedicated thread or the worker thread.
 *
 * Input Parameters:
 *   arg - Reference to the upper half driver structure (cast to void *)
 *
 ****************************************************************************/

static void wlan_upper_work(FAR void *arg)
{
  struct wlan_priv_s *priv = (struct wlan_priv_s *)arg;

  /* RX may release quota and driver buffer, so do RX first. */

  net_lock();
  wlan_rxpoll(priv);
  wlan_txavail_work(priv);
  net_unlock();
}

/****************************************************************************
 * Name: wlan_loop
 *
 * Description:
 *   The loop for dedicated thread.
 *
 ****************************************************************************/

static int wlan_loop(int argc, FAR char *argv[])
{
  struct wlan_priv_s *priv =
    (struct wlan_priv_s *)((uintptr_t)strtoul(argv[1], NULL, 16));

  while (nxsem_wait(&priv->sem) == OK && priv->tid != INVALID_PROCESS_ID)
    {
      wlan_upper_work(priv);
    }

  wlwarn("WARNING: Netdev work thread quitting.");
  return 0;
}
#endif

/****************************************************************************
 * Function: wlan_recvframe
 *
 * Description:
 *   Try to receive RX packet from RX done packet queue.
 *
 * Input Parameters:
 *   priv - Reference to the driver state structure
 *
 * Returned Value:
 *   RX packet if success or NULl if no packet in queue.
 *
 ****************************************************************************/

static struct iob_s *wlan_recvframe(struct wlan_priv_s *priv)
{
  struct iob_s *iob;
  irqstate_t flags;

  flags = up_irq_save();

  iob = iob_remove_queue(&priv->rxb);

  up_irq_restore(flags);

  return iob;
}

/****************************************************************************
 * Name: wlan_transmit
 *
 * Description:
 *   Try to send all TX packets in TX ready queue to Wi-Fi driver. If this
 *    sending fails, then breaks loop and returns.
 *
 * Input Parameters:
 *   priv - Reference to the driver state structure
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void wlan_transmit(struct wlan_priv_s *priv)
{
  uint16_t llhdrlen = NET_LL_HDRLEN(&priv->dev);
  unsigned int offset = CONFIG_NET_LL_GUARDSIZE - llhdrlen;
  struct iob_s *iob;
  int ret;

  while (wl80211_mac_tx_ready())
    {
      if (priv->tx_pending != NULL)
        {
          iob = priv->tx_pending;
          priv->tx_pending = NULL;
        }
      else
        {
          iob = iob_remove_queue(&priv->txb);
          if (iob == NULL)
            {
              break;
            }
        }
#ifdef CONFIG_BL616CL_NET_DEBUG
      wlinfo("iob=%p\n", iob);
#endif
      dump_ethhdr("TX", IOB_DATA(iob) - llhdrlen, iob->io_pktlen + llhdrlen);

      /* Pool exhaustion leaves ownership with this driver. The completion
       * callback frees a slot and schedules another transmit pass. */
      ret = priv->ops->send(iob, llhdrlen, offset);
      if (ret == -EAGAIN)
        {
          priv->tx_pending = iob;
          break;
        }
      if (ret < 0)
        {
          wlerr("Wi-Fi TX failed: %d\n", ret);
        }
    }
}

/****************************************************************************
 * Name: wlan_tx_done
 *
 * Description:
 *   Wi-Fi TX done callback function. If this is called, it means sending
 *   next packet.
 *
 * Input Parameters:
 *   priv   - Reference to the driver state structure
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void wlan_tx_done(struct wlan_priv_s *priv)
{
  wd_cancel(&priv->txtimeout);

  wlan_txavail(&priv->dev);
}

/****************************************************************************
 * Function: wlan_rx_done
 *
 * Description:
 *   Wi-Fi RX done callback function. If this is called, it means receiving
 *   packet.
 *
 * Input Parameters:
 *   priv   - Reference to the driver state structure
 *   buffer - Wi-Fi received packet buffer
 *   len    - Length of received packet
 *   eb     - Wi-Fi receive callback input eb pointer
 *
 * Returned Value:
 *   0 on success or a negated errno on failure
 *
 ****************************************************************************/

static int wlan_rx_done(struct wlan_priv_s *priv,
                        void *buffer,
                        uint16_t len,
                        void *net,
                        void *free)
{
  struct net_driver_s *dev = &priv->dev;
  struct iob_s *iob = NULL;
  irqstate_t flags;
  int ret = 0;

  void (*free_cb)(void *data) = free;

  if (!priv->ifup)
    {
      goto out;
    }

  dump_ethhdr("RX", buffer, len);

#ifdef CONFIG_BL616CL_WLAN_PROBE
  /* Upon receipt of a data packet, the active WLAN connection
   * is deemed stable, resulting in the refresh of the probe timer.
   */

  wlan_sta_probe_status(priv);
#endif

  /* If the free callback is empty, it indicates that the input
   * buffer is already a pre-constructed IOB, requiring no
   * additional memory allocation
   */

  if (free_cb == NULL)
    {
      iob = (struct iob_s *)net;
      goto recv_frame;
    }

  if (len > WLAN_BUF_SIZE)
    {
      wlwarn("ERROR: Wlan receive %d larger than %d\n",
             len,
             WLAN_BUF_SIZE);
      ret = -EINVAL;
      goto out;
    }

  if (len > iob_navail(false) * CONFIG_IOB_BUFSIZE)
    {
      // wlwarn("ERROR: No enough iob to receive pkt, len: %d\n", len);
      ret = -ENOBUFS;
      goto out;
    }

  iob = iob_tryalloc(true);
  if (iob == NULL)
    {
      // wlwarn("ERROR: Failed to alloc iob\n");
      ret = -ENOBUFS;
      goto out;
    }

  iob_reserve(iob, CONFIG_NET_LL_GUARDSIZE);

  ret = iob_trycopyin(iob, buffer, len, 0 - NET_LL_HDRLEN(dev), false);
  if (ret != len)
    {
      wlwarn("ERROR: Failed to copyin iob, ret: %d\n", ret);
      ret = -ENOBUFS;
      goto out;
    }

#ifdef CONFIG_BL616CL_NET_DEBUG
  wlinfo("RX: net %p, buff %p, len: %d, iob %p\n", net, buffer, len, iob);
#endif

  /*  Release the occupied WLAN RX buf as soon as
   *  possible after copying the data to the IOB buffer
   */

  free_cb(net);

  /* wlinfo("free rx buf\n"); */
recv_frame:

  flags = up_irq_save();
  ret = iob_tryadd_queue(iob, &priv->rxb);
  up_irq_restore(flags);

  if (ret < 0)
    {
      wlwarn("ERROR: Failed to add iob to rxb, ret: %d\n", ret);
      ret = -ENOBUFS;
      goto out;
    }

#ifdef CONFIG_BL616CL_WLAN_WORK_THREAD
  wlan_upper_queue_work(priv);
#else
  if (work_available(&priv->rxwork))
    {
      work_queue(WLAN_WORK, &priv->rxwork, wlan_rxpoll, priv, 0);
    }
#endif

  /* wlinfo("rx done, return\n"); */

  return OK;

out:

  /* clear wifi buffer */

  if (free_cb != NULL)
    free_cb(net);

  if (iob != NULL)
    {
      iob_free_chain(iob);
    }

  wlan_txavail(&priv->dev);

  return ret;
}

/****************************************************************************
 * Function: wlan_rxpoll
 *
 * Description:
 *   Try to receive packets from RX done queue and pass packets into IP
 *   stack and send packets which is from IP stack if necessary.
 *
 * Input Parameters:
 *   priv - Reference to the driver state structure
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void wlan_rxpoll(void *arg)
{
  struct wlan_priv_s *priv = (struct wlan_priv_s *)arg;
  struct net_driver_s *dev = &priv->dev;
  struct eth_hdr_s *eth_hdr;
  struct iob_s *iob;

  /* Try to send all cached TX packets for TX ack and so on */

  wlan_transmit(priv);

  /* Loop while while iob_remove_queue() successfully retrieves valid
   * Ethernet frames.
   */

  net_lock();

  while ((iob = wlan_recvframe(priv)) != NULL)
    {
      dev->d_iob = iob;
      dev->d_len = iob->io_pktlen + NET_LL_HDRLEN(dev);

      // iob_reserve(iob, NET_LL_HDRLEN(dev));

#ifdef CONFIG_NET_PKT

      /* When packet sockets are enabled,
       * feed the frame into the packet tap.
       */

      pkt_input(&priv->dev);
#endif

      eth_hdr = (struct eth_hdr_s *)NETLLBUF;

      dump_ethhdr("RX poll", (unsigned char *)eth_hdr, dev->d_len);

      /* We only accept IP packets of the configured type and ARP packets */

#ifdef CONFIG_NET_IPv4
      if (eth_hdr->type == HTONS(ETHTYPE_IP))
        {
          ninfo("IPv4 frame\n");

          /* Receive an IPv4 packet from the network device */

          ipv4_input(&priv->dev);

          /* If the above function invocation resulted in data
           * that should be sent out on the network,
           * the field  d_len will set to a value > 0.
           */

          if (priv->dev.d_len > 0)
            {
              /* And send the packet */

              wlan_cache_txpkt_tail(priv);
            }
        }
      else
#endif
#ifdef CONFIG_NET_IPv6
      if (eth_hdr->type == HTONS(ETHTYPE_IP6))
        {
          ninfo("IPv6 frame\n");

          /* Give the IPv6 packet to the network layer */

          ipv6_input(&priv->dev);

          /* If the above function invocation resulted in data
           * that should be sent out on the network, the field
           * d_len will set to a value > 0.
           */

          if (priv->dev.d_len > 0)
            {
              /* And send the packet */

              wlan_cache_txpkt_tail(priv);
            }
        }
      else
#endif
#ifdef CONFIG_NET_ARP
      if (eth_hdr->type == HTONS(ETHTYPE_ARP))
        {
          ninfo("ARP frame\n");

          /* Handle ARP packet */

          arp_input(&priv->dev);

          /* If the above function invocation resulted in data
           * that should be sent out on the network, the field
           * d_len will set to a value > 0.
           */

          if (priv->dev.d_len > 0)
            {
              wlan_cache_txpkt_tail(priv);
            }
        }
      else
#endif
        {
          ninfo("INFO: Dropped, Unknown type: %04x\n", eth_hdr->type);
        }

      netdev_iob_release(&priv->dev);
    }

  /* Try to send all cached TX packets */

  wlan_transmit(priv);

  net_unlock();
}

/****************************************************************************
 * Name: wlan_txpoll
 *
 * Description:
 *   The transmitter is available, check if the network has any outgoing
 *   packets ready to send.  This is a callback from devif_poll().
 *   devif_poll() may be called:
 *
 *   1. When the preceding TX packets send times out and the interface is
 *      reset
 *   2. During normal TX polling
 *
 * Input Parameters:
 *   dev - Reference to the NuttX driver state structure
 *
 * Returned Value:
 *   OK on success; a negated errno on failure
 *
 ****************************************************************************/

static int wlan_txpoll(struct net_driver_s *dev)
{
  struct wlan_priv_s *priv = dev->d_private;

  wlan_cache_txpkt_tail(priv);

  /* If zero is returned, the polling will continue until
   * all connections have been examined.
   */

  return 1;
}

/****************************************************************************
 * Function: wlan_dopoll
 *
 * Description:
 *   The function is called in order to perform an out-of-sequence TX poll.
 *   This is done:
 *
 *   1. When new TX data is available (wlan_txavail)
 *   2. After a TX timeout to restart the sending process
 *      (wlan_txtimeout_expiry).
 *
 * Input Parameters:
 *   priv - Reference to the driver state structure
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void wlan_dopoll(struct wlan_priv_s *priv)
{
  struct net_driver_s *dev = &priv->dev;

  /* Try to let TCP/IP to send all packets to netcard driver */

  while (wl80211_mac_tx_ready() && devif_poll(dev, wlan_txpoll))
    {
      wlan_transmit(priv);
    }

  /* Try to send all cached TX packets */

  wlan_transmit(priv);
}

/****************************************************************************
 * Function: wlan_txtimeout_work
 *
 * Description:
 *   Perform TX timeout related work from the worker thread
 *
 * Input Parameters:
 *   arg - The argument passed when work_queue() as called.
 *
 * Returned Value:
 *   OK on success
 *
 ****************************************************************************/

static void wlan_txtimeout_work(void *arg)
{
  struct wlan_priv_s *priv = (struct wlan_priv_s *)arg;

  net_lock();

  /* Try to send all cached TX packets */

  wlan_transmit(priv);

  wlwarn("tx timeout \n");

  /* Then poll for new XMIT data */

  wlan_dopoll(priv);

  net_unlock();
}

/****************************************************************************
 * Function: wlan_txtimeout_expiry
 *
 * Description:
 *   Our TX watchdog timed out.  Called from the timer callback handler.
 *   The last TX never completed.  Reset the hardware and start again.
 *
 * Input Parameters:
 *   argc - The number of available arguments
 *   arg  - The first argument
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void wlan_txtimeout_expiry(wdparm_t arg)
{
  struct wlan_priv_s *priv = (struct wlan_priv_s *)arg;

  /* Schedule to perform the TX timeout processing on the worker thread. */

  if (work_available(&priv->toutwork))
    {
      work_queue(WLAN_WORK, &priv->toutwork, wlan_txtimeout_work, priv, 0);
    }
}

/****************************************************************************
 * Name: wlan_txavail_work
 *
 * Description:
 *   Perform an out-of-cycle poll on the worker thread.
 *
 * Input Parameters:
 *   arg - Reference to the NuttX driver state structure (cast to void*)
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called on the higher priority worker thread.
 *
 ****************************************************************************/

static void wlan_txavail_work(void *arg)
{
  struct wlan_priv_s *priv = (struct wlan_priv_s *)arg;

  net_lock();

  /* Try to send all cached TX packets even if net is down */

  wlan_transmit(priv);

  /* Ignore the notification if the interface is not yet up */

  if (priv->ifup)
    {
      /* Poll the network for new XMIT data */

      wlan_dopoll(priv);
    }

  net_unlock();
}

/****************************************************************************
 * Name: wlan_ifup
 *
 * Description:
 *   NuttX Callback: Bring up the Ethernet interface when an IP address is
 *   provided
 *
 * Input Parameters:
 *   dev - Reference to the NuttX driver state structure
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static int wlan_ifup(struct net_driver_s *dev)
{
  int ret;
  struct wlan_priv_s *priv = (struct wlan_priv_s *)dev->d_private;

#ifdef CONFIG_NET_IPv4
  wlinfo("Bringing up: %d.%d.%d.%d\n",
        (uint8_t)(dev->d_ipaddr),
        (uint8_t)(dev->d_ipaddr >> 8),
        (uint8_t)(dev->d_ipaddr >> 16),
        (uint8_t)(dev->d_ipaddr >> 24));
#endif

#ifdef CONFIG_NET_IPv6
  wlinfo("Bringing up: %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
        dev->d_ipv6addr[0],
        dev->d_ipv6addr[1],
        dev->d_ipv6addr[2],
        dev->d_ipv6addr[3],
        dev->d_ipv6addr[4],
        dev->d_ipv6addr[5],
        dev->d_ipv6addr[6],
        dev->d_ipv6addr[7]);
#endif

#ifdef CONFIG_BL616CL_WLAN_WORK_THREAD
  /* Try to bring up a dedicated thread for work. */

  nxsem_init(&priv->sem, 0, 0);

  if (priv->tid <= 0)
    {
      FAR char *argv[2];
      char arg1[32];
      char name[32];

      snprintf(arg1, sizeof(arg1), "%p", priv);
      snprintf(name, sizeof(name), NETDEV_THREAD_NAME_FMT, dev->d_ifname);
      argv[0] = arg1;
      argv[1] = NULL;

      priv->tid = kthread_create(name,
                                 CONFIG_BL616CL_WLAN_THREAD_PRIORITY,
                                 CONFIG_DEFAULT_TASK_STACKSIZE,
                                 wlan_loop,
                                 argv);
      if (priv->tid < 0)
        {
          return priv->tid;
        }
    }
#endif

  net_lock();

  if (priv->ifup)
    {
      net_unlock();
      return OK;
    }

  ret = priv->ops->start();
  if (ret < 0)
    {
      net_unlock();
      wlerr("ERROR: Failed to start Wi-Fi ret=%d\n", ret);
      return ret;
    }

  IOB_QINIT(&priv->rxb);
  IOB_QINIT(&priv->txb);
  priv->tx_pending = NULL;

  priv->dev.d_buf = NULL;
  priv->dev.d_len = 0;

  priv->ifup = true;
  if (g_callback_register_ref == 0)
    {
      if (ret < 0)
        {
          wlwarn("WARN: Failed to register handler ret=%d\n", ret);
        }
    }

  ++g_callback_register_ref;
  net_unlock();

  return OK;
}

/****************************************************************************
 * Name: wlan_ifdown
 *
 * Description:
 *   NuttX Callback: Stop the interface.
 *
 * Input Parameters:
 *   dev - Reference to the NuttX driver state structure
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static int wlan_ifdown(struct net_driver_s *dev)
{
  int ret;
  struct wlan_priv_s *priv = (struct wlan_priv_s *)dev->d_private;

  net_lock();

  if (!priv->ifup)
    {
      net_unlock();
      return OK;
    }

  /* Cancel the TX timeout timers */

  wd_cancel(&priv->txtimeout);

  /* Mark the device "down" */

  priv->ifup = false;

  iob_free_queue(&priv->rxb);
  iob_free_queue(&priv->txb);
  if (priv->tx_pending != NULL)
    {
      iob_free_chain(priv->tx_pending);
      priv->tx_pending = NULL;
    }

#ifdef CONFIG_BL616CL_WLAN_WORK_THREAD
  if (priv->tid > 0)
    {
      /* Try to tear down the dedicated thread for work. */

      priv->tid = INVALID_PROCESS_ID;
      nxsem_post(&priv->sem);
    }

  nxsem_destroy(&priv->sem);
#endif

  ret = priv->ops->stop();
  if (ret < 0)
    {
      wlerr("ERROR: Failed to stop Wi-Fi ret=%d\n", ret);
    }

  /* Decrement the reference count */

  --g_callback_register_ref;

  if (g_callback_register_ref == 0)
    {
      if (ret < 0)
        {
          wlwarn("WARN: Failed to unregister handler ret=%d\n", ret);
        }
    }

  net_unlock();

  return OK;
}

/****************************************************************************
 * Name: wlan_txavail
 *
 * Description:
 *   Driver callback invoked when new TX data is available.  This is a
 *   stimulus perform an out-of-cycle poll and, thereby, reduce the TX
 *   latency.
 *
 * Input Parameters:
 *   dev - Reference to the NuttX driver state structure
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called in normal user mode
 *
 ****************************************************************************/

static int wlan_txavail(struct net_driver_s *dev)
{
  struct wlan_priv_s *priv = (struct wlan_priv_s *)dev->d_private;

#ifdef CONFIG_BL616CL_WLAN_WORK_THREAD
  wlan_upper_queue_work(priv);
#else
  if (work_available(&priv->txwork))
    {
      /* Schedule to serialize the poll on the worker thread. */

      if (work_queue(WLAN_WORK, &priv->txwork, wlan_txavail_work, priv, 0) <
          0)
        {
          wlerr("error queue fail \n");
        }
    }
#endif

  return OK;
}

/****************************************************************************
 * Name: wlan_ioctl
 *
 * Description:
 *   Handle network IOCTL commands directed to this device.
 *
 * Input Parameters:
 *   dev - Reference to the NuttX driver state structure
 *   cmd - The IOCTL command
 *   arg - The argument for the IOCTL command
 *
 * Returned Value:
 *   OK on success; Negated errno on failure.
 *
 ****************************************************************************/

#ifdef CONFIG_NETDEV_IOCTL
static int wlan_ioctl(struct net_driver_s *dev, int cmd, unsigned long arg)
{
  int ret;
  struct iwreq *iwr = (struct iwreq *)arg;
  struct wlan_priv_s *priv = (struct wlan_priv_s *)dev->d_private;
  const struct wlan_ops_s *ops = priv->ops;
  const uint8_t mac_zero[MAC_LEN] = {0x0};

  /* Decode and dispatch the driver-specific IOCTL command */

  switch (cmd)
    {
      /* Set encoding token & mode */

      case SIOCSIWENCODEEXT:
        ret = ops->passwd(iwr, true);

        break;

      /* Get encoding token & mode */

      case SIOCGIWENCODEEXT:
        ret = ops->passwd(iwr, false);
        break;

      /* Set ESSID */

      case SIOCSIWESSID:
        if ((iwr->u.essid.flags == IW_ESSID_ON) ||
            (iwr->u.essid.flags == IW_ESSID_DELAY_ON))
          {
            ret = ops->essid(iwr, true);
            if (ret < 0)
              {
                break;
              }

            if (iwr->u.essid.flags == IW_ESSID_ON)
              {
                wlinfo("Connect to AP\n");
                ret = ops->connect();
                if (ret < 0)
                  {
                    wlerr("ERROR: Failed to connect\n");
                    break;
                  }
              }
          }
        else
          {
            ret = ops->disconnect();
            if (ret < 0)
              {
                wlerr("ERROR: Failed to disconnect\n");
                break;
              }
          }

        break;

      /* Get ESSID */

      case SIOCGIWESSID:
        ret = ops->essid(iwr, false);
        break;

      /* Set access point MAC addresses */

      case SIOCSIWAP:
        if (memcmp(iwr->u.ap_addr.sa_data, mac_zero, MAC_LEN) != 0)
          {
            ret = ops->bssid(iwr, true);
            if (ret < 0)
              {
                wlerr("ERROR: Failed to set BSSID\n");
                break;
              }

            ret = ops->connect();
            if (ret < 0)
              {
                wlerr("ERROR: Failed to connect\n");
                break;
              }
          }
        else
          {
            ret = ops->disconnect();
            if (ret < 0)
              {
                wlerr("ERROR: Failed to disconnect\n");
                break;
              }
          }
        break;

      /* Get access point MAC addresses */

      case SIOCGIWAP:
        ret = ops->bssid(iwr, false);
        break;

      /* Trigger scanning (list cells) */

      case SIOCSIWSCAN:
        ret = ops->scan(iwr);
        break;

      /* Get scanning results */

      case SIOCGIWSCAN:
        ret = ops->scan_result(iwr);
        break;

      case SIOCGIWCOUNTRY: /* Get country code */
        ret = ops->country(iwr, false);
        break;

      case SIOCSIWCOUNTRY: /* Set country code */
        ret = ops->country(iwr, true);
        break;

      case SIOCGIWSENS: /* Get sensitivity (dBm) */
        ret = ops->rssi(iwr, false);
        break;

      case SIOCSIWMODE: /* Set operation mode */
        ret = ops->mode(iwr, true);
        break;

      case SIOCGIWMODE: /* Get operation mode */
        ret = ops->mode(iwr, false);
        break;

      case SIOCSIWAUTH: /* Set authentication mode params */
        ret = ops->auth(iwr, true);
        break;

      case SIOCGIWAUTH: /* Get authentication mode params */
        ret = ops->auth(iwr, false);
        break;

      case SIOCSIWFREQ: /* Set channel/frequency (MHz) */
        ret = ops->freq(iwr, true);
        break;

      case SIOCGIWFREQ: /* Get channel/frequency (MHz) */
        ret = ops->freq(iwr, false);
        break;

      case SIOCSIWRATE: /* Set default bit rate (Mbps) */
        wlwarn("WARNING: SIOCSIWRATE not implemented\n");
        ret = -ENOSYS;
        break;

      case SIOCGIWRATE: /* Get default bit rate (Mbps) */
        ret = ops->bitrate(iwr, false);
        break;

      case SIOCSIWTXPOW: /* Set transmit power (dBm) */
        ret = ops->txpower(iwr, true);
        break;

      case SIOCGIWTXPOW: /* Get transmit power (dBm) */
        ret = ops->txpower(iwr, false);
        break;

      case SIOCGIWRANGE: /* Get range of parameters */
        ret = ops->channel(iwr, false);
        break;

      case SIOCSIWPTAPRIO: /* Set PTA priority */
        ret = ops->pta(iwr, true);
        break;

      case SIOCGIWPTAPRIO: /* Get PTA priority */
        ret = ops->pta(iwr, false);
        break;

      case SIOCSIWDTIM: /* Set DTIM period */
        ret = ops->dtim(iwr, true);
        break;

      case SIOCGIWDTIM: /* Get DTIM period */
        ret = ops->dtim(iwr, false);
        break;

      case SIOCSIWPWSAVE: /* Set power save mode */
        ret = ops->powersave(iwr, true);
        break;

      case SIOCGIWPWSAVE: /* Get power save mode */
        ret = ops->powersave(iwr, false);
        break;

      case SIOCSIWPMKSA: /* Set PMKSA cache */
        ret = ops->pmksa(iwr, true);
        break;

      case SIOCGIWPMKSA: /* Get PMKSA cache */
        ret = ops->pmksa(iwr, false);
        break;

      default:
        wlerr("ERROR: Unrecognized IOCTL command: %d\n", cmd);
        ret = -ENOTTY; /* Special return value for this case */
        break;
    }

  return ret;
}
#endif /* CONFIG_NETDEV_IOCTL */

/****************************************************************************
 * Name: bl616_net_initialize
 *
 * Description:
 *   Initialize the bl616 driver
 *
 * Input Parameters:
 *   devno    - The device number
 *   mac_addr - MAC address
 *
 * Returned Value:
 *   OK on success; Negated errno on failure.
 *
 ****************************************************************************/

static int bl616_net_initialize(int devno,
                                uint8_t *mac_addr,
                                const struct wlan_ops_s *ops)
{
  int ret;
  struct wlan_priv_s *priv;
  struct net_driver_s *netdev;

  priv = &g_wlan_priv[devno];

  if (priv->ref != 0)
    {
      priv->ref++;
      return OK;
    }

  netdev = &priv->dev;

  /* Initialize the driver structure */

  memset(priv, 0, sizeof(struct wlan_priv_s));
  netdev->d_ifup = wlan_ifup;       /* I/F down callback */
  netdev->d_ifdown = wlan_ifdown;   /* I/F up (new IP address) callback */
  netdev->d_txavail = wlan_txavail; /* New TX data callback */
#ifdef CONFIG_NETDEV_IOCTL
  netdev->d_ioctl = wlan_ioctl; /* Handle network IOCTL commands */
#endif

  /* Used to recover private state from dev */

  netdev->d_private = (void *)priv;

  memcpy(netdev->d_mac.ether.ether_addr_octet, mac_addr, MAC_LEN);

  ret = netdev_register(netdev, NET_LL_IEEE80211);
  if (ret < 0)
    {
      wlerr("ERROR: Initialization of IEEE 802.11 block failed: %d\n", ret);
      return ret;
    }

  priv->ops = ops;

  priv->ref++;
  wlinfo("INFO: Initialize Wi-Fi adapter No.%d success\n", devno);

  return OK;
}

/****************************************************************************
 * Name: up_pm_notify
 *
 * Description:
 *   Notify power management event
 *
 ****************************************************************************/

#ifdef CONFIG_PM
static void up_pm_notify(struct pm_callback_s *cb,
                         int domain,
                         enum pm_state_e pmstate)
{
  /* Logic to prepare for a reduced power state goes here. */

  switch (pmstate)
    {
      case PM_NORMAL:
      case PM_IDLE:
      case PM_STANDBY:
      case PM_SLEEP:

      default:
        /* Should not get here */
        break;
    }

  return;
}

/****************************************************************************
 * Name: bl616_net_initialize
 *
 * Description:
 *   Initialize the bl616 driver
 *
 * Input Parameters:
 *   devno    - The device number
 *   mac_addr - MAC address
 *
 * Returned Value:
 *   OK on success; Negated errno on failure.
 *
 ****************************************************************************/

static int bl616_wlan_pm_init(void)
{
  int ret;
  struct wlan_priv_s *priv = &g_wlan_priv[BL616_WLAN_STA_DEVNO];

  /* Register to receive power management callbacks */

  pm_wakelock_init(&priv->wakelock, "wlan0", PM_IDLE_DOMAIN, PM_NORMAL);

  return OK;
}
#endif  /* CONFIG_PM */

/****************************************************************************
 * Function: wlan_sta_rx_done
 *
 * Description:
 *   Wi-Fi station RX done callback function. If this is called, it means
 *   station receiveing packet.
 *
 * Input Parameters:
 *   buffer - Wi-Fi received packet buffer
 *   len    - Length of received packet
 *   eb     - Wi-Fi receive callback input eb pointer
 *
 * Returned Value:
 *   0 on success or a negated errno on failure
 *
 ****************************************************************************/

#ifdef BL616_WLAN_HAS_STA
static int wlan_sta_rx_done(void *net, void *buffer, uint16_t len, void *eb)
{
  struct wlan_priv_s *priv = &g_wlan_priv[BL616_WLAN_STA_DEVNO];

  return wlan_rx_done(priv, buffer, len, net, eb);
}

/****************************************************************************
 * Name: wlan_sta_tx_done
 *
 * Description:
 *   Wi-Fi station TX done callback function. If this is called, it means
 *   station sending next packet.
 *
 * Input Parameters:
 *   ifidx  - The interface ID that the TX callback has been triggered from.
 *   data   - Pointer to the data transmitted.
 *   len    - Length of the data transmitted.
 *   status - True if data was transmitted successfully or false if failed.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void wlan_sta_tx_done(void *arg)
{
  struct wlan_priv_s *priv = &g_wlan_priv[BL616_WLAN_STA_DEVNO];

  wlan_tx_done(priv);
}

/****************************************************************************
 * Name: wlan_sta_probe_status_work
 *
 * Description:
 *   Wi-Fi station probe status work function. If this is called, it means
 *   station sending arp probe packet.
 *
 * Input Parameters:
 *   net_dev - Pointer to the net device structure.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifdef CONFIG_BL616CL_WLAN_PROBE
static void wlan_sta_probe_status_work(void *net_dev)
{
  struct wlan_priv_s *dev = (struct wlan_priv_s *)net_dev;
  struct net_driver_s *netdev = &dev->dev;
  int ret;

  DEBUGASSERT(netdev != NULL);

#ifdef CONFIG_BL616CL_WLAN_PROBE_ARP

  /* ipv4_acd_announce */

  if (IFF_IS_RUNNING(netdev->d_flags))
    {
      extern int arp_send_single(in_addr_t ipaddr);
      extern int arp_send(in_addr_t ipaddr);
      extern int arp_delete(in_addr_t ipaddr, FAR struct net_driver_s *dev);

      /* delete arp cache */

      arp_delete(netdev->d_draddr, netdev);

    /* Do not enter low power while waiting for arp response */

#ifdef CONFIG_BL616CL_LOWPOWER
      pm_wakelock_stay(&dev->wakelock);
#endif

      if (OK != arp_send(netdev->d_draddr))
        {
          /* arp_send return:
           * Zero (OK) is returned on success and the IP address mapping can now be
           * found in the ARP table.  On error a negated errno value is returned:
           *
           *   -ETIMEDOUT:    The number or retry counts has been exceed.
           *   -EHOSTUNREACH: Could not find a route to the host
           */

#if 0
          ret = dev->ops->disconnect();
          if (ret < 0)
            {
              wlerr("ERROR: Failed to disconnect\n");
            }
#endif

          wlerr("ERROR: arp probe failed\n");
        }

#ifdef CONFIG_BL616CL_LOWPOWER
      pm_wakelock_relax(&dev->wakelock);
#endif

      ret = work_queue(WLAN_WORK,
                       &dev->probework,
                       wlan_sta_probe_status_work,
                       (void *)dev,
#ifdef CONFIG_MIIO_OT_KPLV_TIMEOUT_MS
                       /* When miio is turned on, the task with the largest
                        * cycle is miio kplv, and the probe cycle needs to
                        * be greater than it.
                        * Probe will only work if miio kplv fails
                        */

                       MSEC2TICK(CONFIG_MIIO_OT_KPLV_TIMEOUT_MS / 3 + 1e4));
#else
                       SEC2TICK(CONFIG_BL616CL_WLAN_PROBE_ARP_INTERVAL));
#endif
      if (ret != OK)
        {
          wlerr("ERROR ret %d \n", ret);
        }
    }
  else
    {
      wlinfo("INFO: Wi-Fi station link down, stop probe\n");
    }
#endif
}

static void wlan_sta_probe_status(struct wlan_priv_s *dev)
{
  int ret = work_queue(WLAN_WORK,
                       &dev->probework,
                       wlan_sta_probe_status_work,
                       (void *)dev,
#ifdef CONFIG_MIIO_OT_KPLV_TIMEOUT_MS
                       /* Same as above */

                       MSEC2TICK(CONFIG_MIIO_OT_KPLV_TIMEOUT_MS / 3 + 1e4));
#else
                       SEC2TICK(CONFIG_BL616CL_WLAN_PROBE_ARP_INTERVAL));
#endif
  if (ret != OK)
    {
      wlerr("ERROR ret %d \n", ret);
    }
}
#else
#define wlan_sta_probe_status(dev)
#endif
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616_wlan_sta_get_netdev
 *
 * Description:
 *   Get Wi-Fi station netcard driver
 *
 * Parameters:
 *   None
 *
 * Returned Value:
 *   Pointer to Wi-Fi station netcard driver
 *
 ****************************************************************************/

struct net_driver_s *bl616_wlan_sta_get_netdev(void)
{
  return &g_wlan_priv[BL616_WLAN_STA_DEVNO].dev;
}

/****************************************************************************
 * Name: bl616_wlan_sta_set_linkstatus
 *
 * Description:
 *   Set Wi-Fi station link status
 *
 * Parameters:
 *   linkstatus - true Notifies the networking layer about an available
 *                carrier, false Notifies the networking layer about an
 *                disappeared carrier.
 *
 * Returned Value:
 *   OK on success; Negated errno on failure.
 *
 ****************************************************************************/

#ifdef BL616_WLAN_HAS_STA
int bl616_wlan_sta_set_linkstatus(bool linkstatus)
{
  int ret = -EINVAL;
  struct wlan_priv_s *priv = &g_wlan_priv[BL616_WLAN_STA_DEVNO];

  if (priv != NULL)
    {
      if (linkstatus == true)
        {
          netdev_carrier_on(&priv->dev);
          ret = OK;
          wlinfo("INFO: Wi-Fi station link up %d\n", ret);
        }
      else
        {
          netdev_carrier_off(&priv->dev);
          ret = OK;
          wlinfo("INFO: Wi-Fi station link down %d\n", ret);
        }

      if (ret < 0)
        {
          wlerr("ERROR: Failed to notify the networking layer\n");
        }
    }

  return ret;
}

/****************************************************************************
 * Name: bl616_wlan_sta_initialize
 *
 * Description:
 *   Initialize the bl616 WLAN station netcard driver
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success; Negated errno on failure.
 *
 ****************************************************************************/

int bl616_wlan_sta_initialize(void)
{
  int ret;
  uint8_t eth_mac[6];

  ret = bl616_wifi_adapter_init();
  if (ret < 0)
    {
      wlerr("ERROR: Initialize Wi-Fi adapter error: %d\n", ret);
      return ret;
    }

#ifdef CONFIG_PM
  bl616_wlan_pm_init();
#endif

  ret = bl616_wifi_sta_read_mac(eth_mac);
  if (ret < 0)
    {
      wlerr("ERROR: Failed to read MAC address\n");
      return ret;
    }

  wlinfo("Wi-Fi station MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        eth_mac[0],
        eth_mac[1],
        eth_mac[2],
        eth_mac[3],
        eth_mac[4],
        eth_mac[5]);

  g_wlan_priv[BL616_WLAN_STA_DEVNO].ref = 0;
  ret = bl616_net_initialize(BL616_WLAN_STA_DEVNO, eth_mac, &g_sta_ops);
  if (ret < 0)
    {
      wlerr("ERROR: Failed to initialize net\n");
      return ret;
    }

  bl616_wifi_sta_register_recv_cb(wlan_sta_rx_done);
  bl616_wifi_sta_register_txdone_cb(wlan_sta_tx_done);

#ifdef CONFIG_BL616CL_WLAN_SDIO
  sdiowifi_mgmr_start();
#endif

  ninfo("INFO: Initialize Wi-Fi station success net\n");

  return OK;
}

#endif

