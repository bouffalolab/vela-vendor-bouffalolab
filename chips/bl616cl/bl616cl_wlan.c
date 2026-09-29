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
#include <string.h>
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
#endif

#include "bl616cl_wifi_adapter.h"
#include "bl616cl_wlan.h"
#include "wl80211_mac.h"
#include "wifi_mgmr_ext.h"

#ifdef CONFIG_BL616CL_WLAN_RX_ZEROCOPY
#  include CONFIG_MACSW_SELECT_INCLUDE
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* RX/TX work queue.  LPWORK falls back to HPWORK when CONFIG_SCHED_LPWORK
 * is disabled.  The wl80211 timers and events run on the same queue
 * (WL80211_WORK, see components/wireless/wifi/wl80211/CMakeLists.txt).
 */

#define WLAN_WORK    LPWORK

/* TX frames waiting for a completion are retried if none arrives in time */

#define WLAN_TXTOUT  SEC2TICK(1)

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

/* The stack writes the IPv4 and TCP headers (up to 80 bytes with options)
 * right after the guard and needs them in the first IOB.
 */

static_assert(CONFIG_NET_LL_GUARDSIZE + 80 <= CONFIG_IOB_BUFSIZE,
              "first IOB too small for the guard and IPv4/TCP headers");

#ifdef CONFIG_BL616CL_WLAN_RX_ZEROCOPY
/* RX frames at least this long stay in their wl80211 host RX slot, which
 * is wrapped as an IOB, instead of being copied into a pool IOB.  Short
 * frames are cheap to copy, and TCP ACKs must stay in pool IOBs: the stack
 * may clone new TCP data into the IOB of a received ACK, and a slot has no
 * room for the TX header.  lwIP in the Bouffalo SDK uses the same limit.
 */

#  define WLAN_RX_ZC_MIN    500

/* Slots lent to the stack at a time.  wl80211 has
 * CFG_BARX * CFG_REORD_BUF + 2 host RX slots; one BA session may hold
 * CFG_REORD_BUF of them while reordering and two are kept for frames in
 * hand-over.  When no slot is free the MAC stops taking frames, beacons
 * included.
 */

#  define WLAN_RX_ZC_SLOTS  ((CFG_BARX - 1) * CFG_REORD_BUF)
#endif

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
static struct wlan_priv_s g_wlan_priv[BL616CL_WLAN_DEVS];

#ifdef CONFIG_BL616CL_WLAN_RX_ZEROCOPY
/* Host RX slots currently wrapped as IOBs */

static int g_wlan_rx_held;
#endif

static const struct wlan_ops_s g_sta_ops =
{
  .start       = bl616cl_wifi_sta_start,
  .send        = bl616cl_wifi_sta_send_data,
  .essid       = bl616cl_wifi_sta_essid,
  .bssid       = bl616cl_wifi_sta_bssid,
  .passwd      = bl616cl_wifi_sta_password,
  .mode        = bl616cl_wifi_sta_mode,
  .auth        = bl616cl_wifi_sta_auth,
  .freq        = bl616cl_wifi_sta_freq,
  .bitrate     = bl616cl_wifi_sta_bitrate,
  .txpower     = bl616cl_wifi_sta_txpower,
  .channel     = bl616cl_wifi_sta_channel,
  .country     = bl616cl_wifi_sta_country,
  .rssi        = bl616cl_wifi_sta_rssi,
  .connect     = bl616cl_wifi_sta_connect,
  .scan        = bl616cl_wifi_sta_scan,
  .scan_result = bl616cl_wifi_sta_scan_result,
  .disconnect  = bl616cl_wifi_sta_disconnect,
  .pta         = bl616cl_wifi_sta_pta,
  .stop        = bl616cl_wifi_sta_stop,
  .dtim        = bl616cl_wifi_sta_dtim,
  .powersave   = bl616cl_wifi_sta_powersave,
  .pmksa       = bl616cl_wifi_sta_pmksa,
};

#ifdef CONFIG_PM
struct bl616cl_wlan_pm_config_s
{
  struct pm_callback_s pm_cb;
};

static void up_pm_notify(struct pm_callback_s *cb,
                         int domain,
                         enum pm_state_e pmstate);

static struct bl616cl_wlan_pm_config_s g_wlan_pm =
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

/* wl80211 NuttX host port: STA TX backpressure */

extern bool wl80211_output_ready(void);

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
static int bl616cl_wlan_pm_init(void);
static void up_pm_notify(struct pm_callback_s *cb,
                         int domain,
                         enum pm_state_e pmstate);
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
      NETDEV_TXERRORS(&priv->dev);
      netdev_iob_release(&priv->dev);
      return;
    }

  NETDEV_TXPACKETS(&priv->dev);
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

#ifdef CONFIG_BL616CL_WLAN_RX_ZEROCOPY
/****************************************************************************
 * Function: wlan_rx_slot_free
 *
 * Description:
 *   io_free callback of a wrapped RX slot: give the slot back to wl80211.
 *   Runs in whichever thread frees the IOB; wl80211_mac_rx_free() queues
 *   the slot with interrupts disabled and may be called from any thread.
 *
 * Input Parameters:
 *   iob - The IOB header, which sits on the rx_info of the slot
 *
 ****************************************************************************/

static void wlan_rx_slot_free(void *iob)
{
  irqstate_t flags = up_irq_save();

  g_wlan_rx_held--;
  up_irq_restore(flags);

  wl80211_mac_rx_free(iob);
}

/****************************************************************************
 * Function: wlan_rx_wrap
 *
 * Description:
 *   Wrap a received frame in its host RX slot as an IOB.  The IOB header
 *   takes the rx_info of the slot, which wl80211 no longer reads once it
 *   calls the RX callback; io_offset points at the L3 header, as on the
 *   copy path, with the Ethernet header in front of it.
 *
 * Input Parameters:
 *   dev    - Reference to the NuttX driver state structure
 *   rxhdr  - rx_info of the slot, passed to the RX callback as "net"
 *   buffer - Ethernet frame inside the slot
 *   len    - Length of the frame
 *
 * Returned Value:
 *   The IOB, or NULL if the frame starts too close to rx_info.
 *
 ****************************************************************************/

static struct iob_s *wlan_rx_wrap(struct net_driver_s *dev, void *rxhdr,
                                  uint8_t *buffer, uint16_t len)
{
  struct iob_s *iob;
  irqstate_t flags;

  if (buffer < (uint8_t *)rxhdr + sizeof(struct iob_s))
    {
      return NULL;
    }

  iob = iob_init_with_data(rxhdr, buffer + len - (uint8_t *)rxhdr,
                           wlan_rx_slot_free);
  iob->io_offset = buffer + NET_LL_HDRLEN(dev) - iob->io_data;
  iob->io_len    = len - NET_LL_HDRLEN(dev);
  iob->io_pktlen = iob->io_len;

  flags = up_irq_save();
  g_wlan_rx_held++;
  up_irq_restore(flags);

  return iob;
}

/****************************************************************************
 * Function: wlan_tx_headroom
 *
 * Description:
 *   wl80211_output() writes the TX header in front of the frame, inside
 *   the CONFIG_NET_LL_GUARDSIZE headroom of the first IOB.  A reply that
 *   the stack built in a wrapped RX slot has less, so copy it into pool
 *   IOBs and free the original, which returns the slot.
 *
 * Input Parameters:
 *   iob      - Frame whose first IOB lacks the headroom
 *   llhdrlen - Link layer header length in front of IOB_DATA()
 *
 * Returned Value:
 *   The copy, or NULL if the pool had no IOB; the frame is then dropped
 *   and left to retransmission.
 *
 ****************************************************************************/

static struct iob_s *wlan_tx_headroom(struct iob_s *iob, uint16_t llhdrlen)
{
  struct iob_s *copy = iob_tryalloc(false);

  if (copy != NULL)
    {
      iob_reserve(copy, CONFIG_NET_LL_GUARDSIZE);
      if (iob_clone_partial(iob, iob->io_pktlen, 0, copy, 0,
                            false, false) < 0)
        {
          iob_free_chain(copy);
          copy = NULL;
        }
      else
        {
          memcpy(IOB_DATA(copy) - llhdrlen, IOB_DATA(iob) - llhdrlen,
                 llhdrlen);
        }
    }

  if (copy == NULL)
    {
      wlwarn("WARNING: No IOB to move a reply out of an RX slot\n");
    }

  iob_free_chain(iob);
  return copy;
}
#endif

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

  while (wl80211_output_ready())
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

#ifdef CONFIG_BL616CL_WLAN_RX_ZEROCOPY
      /* Every TX frame passes here; replies built in RX slots are moved */

      if (iob->io_offset < CONFIG_NET_LL_GUARDSIZE)
        {
          iob = wlan_tx_headroom(iob, llhdrlen);
          if (iob == NULL)
            {
              NETDEV_TXERRORS(&priv->dev);
              continue;
            }
        }
#endif

      /* -EAGAIN (too many frames in flight) leaves ownership with this
       * driver. The completion callback schedules another transmit pass. */
      ret = priv->ops->send(iob, llhdrlen, offset);
      if (ret == -EAGAIN)
        {
          priv->tx_pending = iob;
          break;
        }
      if (ret < 0)
        {
          wlerr("Wi-Fi TX failed: %d\n", ret);
          NETDEV_TXERRORS(&priv->dev);
        }
    }

  /* Frames left behind wait for a TX completion, which cancels the
   * watchdog; if none comes, wlan_txtimeout_work() retries them.
   */

  if (priv->tx_pending != NULL || !IOB_QEMPTY(&priv->txb))
    {
      wd_start(&priv->txtimeout, WLAN_TXTOUT,
               wlan_txtimeout_expiry, (wdparm_t)priv);
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
  NETDEV_TXDONE(&priv->dev);
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

#ifdef CONFIG_BL616CL_WLAN_RX_ZEROCOPY
  /* Long frames stay in their slot while the budget allows.  Short ones
   * are dropped as before when the pool is empty: a wrapped ACK makes the
   * stack build the next TCP segment in the slot, and moving it out then
   * needs a pool IOB too.
   */

  if (g_wlan_rx_held < WLAN_RX_ZC_SLOTS && len >= WLAN_RX_ZC_MIN)
    {
      iob = wlan_rx_wrap(dev, net, buffer, len);
      if (iob != NULL)
        {
          free_cb = NULL;
          goto recv_frame;
        }
    }
#endif

  if (len > iob_navail(false) * CONFIG_IOB_BUFSIZE)
    {
      // wlwarn("ERROR: No enough iob to receive pkt, len: %d\n", len);
      ret = -ENOBUFS;
      goto out;
    }

  /* Unthrottled, as netdev_upperhalf RX: TCP write buffers allocate
   * throttled, so the last CONFIG_IOB_THROTTLE IOBs stay available here.
   * A throttled RX allocation starves the ACKs a full send buffer waits
   * for, and the connection never recovers.
   */

  iob = iob_tryalloc(false);
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

  /*  Release the occupied WLAN RX buf as soon as
   *  possible after copying the data to the IOB buffer
   */

  free_cb(net);
  free_cb = NULL;

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

  if (work_available(&priv->rxwork))
    {
      work_queue(WLAN_WORK, &priv->rxwork, wlan_rxpoll, priv, 0);
    }

  /* wlinfo("rx done, return\n"); */

  return OK;

out:

  NETDEV_RXDROPPED(dev);

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

  net_lock();

  /* Try to send all cached TX packets for TX ack and so on */

  wlan_transmit(priv);

  /* Loop while while iob_remove_queue() successfully retrieves valid
   * Ethernet frames.
   */

  while ((iob = wlan_recvframe(priv)) != NULL)
    {
      dev->d_iob = iob;
      dev->d_len = iob->io_pktlen + NET_LL_HDRLEN(dev);
      NETDEV_RXPACKETS(dev);

      // iob_reserve(iob, NET_LL_HDRLEN(dev));

#ifdef CONFIG_NET_PKT

      /* When packet sockets are enabled,
       * feed the frame into the packet tap.
       */

      pkt_input(&priv->dev);
#endif

      eth_hdr = (struct eth_hdr_s *)NETLLBUF;

      /* We only accept IP packets of the configured type and ARP packets */

#ifdef CONFIG_NET_IPv4
      if (eth_hdr->type == HTONS(ETHTYPE_IP))
        {
          ninfo("IPv4 frame\n");
          NETDEV_RXIPV4(dev);

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
          NETDEV_RXIPV6(dev);

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
          NETDEV_RXARP(dev);

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
          NETDEV_RXDROPPED(dev);
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

  while (wl80211_output_ready() && devif_poll(dev, wlan_txpoll))
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

  NETDEV_TXTIMEOUTS(&priv->dev);
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
 *   Called on the WLAN_WORK worker thread.
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

  if (work_available(&priv->txwork))
    {
      /* Schedule to serialize the poll on the worker thread. */

      if (work_queue(WLAN_WORK, &priv->txwork, wlan_txavail_work, priv, 0) <
          0)
        {
          wlerr("error queue fail \n");
        }
    }

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
 * Name: bl616cl_net_initialize
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

static int bl616cl_net_initialize(int devno,
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
 * Name: bl616cl_net_initialize
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

static int bl616cl_wlan_pm_init(void)
{
  int ret;
  struct wlan_priv_s *priv = &g_wlan_priv[BL616CL_WLAN_STA_DEVNO];

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

static int wlan_sta_rx_done(void *net, void *buffer, uint16_t len, void *eb)
{
  struct wlan_priv_s *priv = &g_wlan_priv[BL616CL_WLAN_STA_DEVNO];

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
  struct wlan_priv_s *priv = &g_wlan_priv[BL616CL_WLAN_STA_DEVNO];

  wlan_tx_done(priv);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_wlan_sta_get_netdev
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

struct net_driver_s *bl616cl_wlan_sta_get_netdev(void)
{
  return &g_wlan_priv[BL616CL_WLAN_STA_DEVNO].dev;
}

/****************************************************************************
 * Name: bl616cl_wlan_sta_set_linkstatus
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

int bl616cl_wlan_sta_set_linkstatus(bool linkstatus)
{
  int ret = -EINVAL;
  struct wlan_priv_s *priv = &g_wlan_priv[BL616CL_WLAN_STA_DEVNO];

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
 * Name: bl616cl_wlan_sta_initialize
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

int bl616cl_wlan_sta_initialize(void)
{
  int ret;
  uint8_t eth_mac[6];

  ret = bl616cl_wifi_adapter_init();
  if (ret < 0)
    {
      wlerr("ERROR: Initialize Wi-Fi adapter error: %d\n", ret);
      return ret;
    }

#ifdef CONFIG_PM
  bl616cl_wlan_pm_init();
#endif

  ret = bl616cl_wifi_sta_read_mac(eth_mac);
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

  g_wlan_priv[BL616CL_WLAN_STA_DEVNO].ref = 0;
  ret = bl616cl_net_initialize(BL616CL_WLAN_STA_DEVNO, eth_mac, &g_sta_ops);
  if (ret < 0)
    {
      wlerr("ERROR: Failed to initialize net\n");
      return ret;
    }

  bl616cl_wifi_sta_register_recv_cb(wlan_sta_rx_done);
  bl616cl_wifi_sta_register_txdone_cb(wlan_sta_tx_done);

  ninfo("INFO: Initialize Wi-Fi station success net\n");

  return OK;
}

