/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_ble.c
 *
 * HCI transport between the BL616CL BLE controller and a host stack.
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

/* The controller library takes and returns HCI packets as structures
 * (hci_onchip.h).  This file converts them to and from the HCI packets of
 * a bt_driver_s, which the NuttX H4 pseudo device (/dev/ttyHCI0) carries
 * to the host.  The conversion follows bl_hci_wrapper.c of the Bouffalo
 * SDK host.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/clock.h>
#include <nuttx/signal.h>
#include <nuttx/wireless/bluetooth/bt_driver.h>
#include <nuttx/wireless/bluetooth/bt_hci.h>

#include "hci_onchip.h"
#include "bl616cl_ble.h"

#ifndef CONFIG_BL_COMPONENT_WL80211
/* rfparam_adapter.h has legacy non-prototype declarations. */

#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wstrict-prototypes"
#  include "rfparam_adapter.h"
#  pragma GCC diagnostic pop
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BLE_HCI_EVT_VENDOR   0xff

/* Largest packet the controller hands over: an event with 255 parameter
 * bytes, or an ACL packet with a 251-byte LE payload.
 */

#define BLE_RX_BUFSIZE       260

/* How long the controller thread waits for room in the host RX buffer
 * before it drops a packet that is not an advertising report.
 */

#define BLE_RX_WAIT_MS       50

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_ble_s
{
  struct bt_driver_s drv;
  uint32_t           adv_dropped;  /* Advertising reports dropped */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  bl616cl_ble_open(FAR struct bt_driver_s *drv);
static int  bl616cl_ble_send(FAR struct bt_driver_s *drv,
                             enum bt_buf_type_e type,
                             FAR void *data, size_t len);
static void bl616cl_ble_close(FAR struct bt_driver_s *drv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bl616cl_ble_s g_ble =
{
  .drv =
  {
    .open  = bl616cl_ble_open,
    .send  = bl616cl_ble_send,
    .close = bl616cl_ble_close,
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_ble_deliver
 *
 * Description:
 *   Pass one packet to the host.  Advertising reports are dropped at once
 *   when the host RX buffer is full, so that scanning cannot stall the
 *   controller; other packets wait up to BLE_RX_WAIT_MS for room.
 *
 ****************************************************************************/

static void bl616cl_ble_deliver(enum bt_buf_type_e type,
                                FAR uint8_t *data, size_t len, bool adv)
{
  clock_t start = clock_systime_ticks();
  int ret;

  while ((ret = bt_netdev_receive(&g_ble.drv, type, data, len)) ==
         -ENOMEM)
    {
      if (adv)
        {
          if ((g_ble.adv_dropped++ & 0x3f) == 0)
            {
              wlwarn("Host RX full, %" PRIu32 " adv reports dropped\n",
                     g_ble.adv_dropped);
            }

          return;
        }

      if (clock_systime_ticks() - start >= MSEC2TICK(BLE_RX_WAIT_MS))
        {
          break;
        }

      nxsig_usleep(USEC_PER_TICK);
    }

  if (ret < 0)
    {
      wlerr("ERROR: HCI type %d len %zu dropped: %d\n", type, len, ret);
    }
}

/****************************************************************************
 * Name: bl616cl_ble_recv
 *
 * Description:
 *   Controller callback: rebuild the HCI packet and pass it to the host.
 *   Runs in the controller thread.
 *
 ****************************************************************************/

static void bl616cl_ble_recv(uint8_t pkt_type, uint16_t src_id,
                             FAR uint8_t *param, uint8_t param_len)
{
  uint8_t buf[BLE_RX_BUFSIZE];
  enum bt_buf_type_e type = BT_EVT;
  bool adv = false;
  size_t len;

  switch (pkt_type)
    {
      case BT_HCI_CMD_CMP_EVT:

        /* src_id is the opcode; one more command may be sent */

        if (param_len > UINT8_MAX - 3)
          {
            wlerr("ERROR: Command Complete too long: %u\n", param_len);
            return;
          }

        buf[0] = BT_HCI_EVT_CMD_COMPLETE;
        buf[1] = 3 + param_len;
        buf[2] = 1;
        buf[3] = src_id & 0xff;
        buf[4] = src_id >> 8;
        memcpy(&buf[5], param, param_len);
        len = 5 + param_len;
        break;

      case BT_HCI_CMD_STAT_EVT:

        /* param[0] is the status, src_id the opcode */

        buf[0] = BT_HCI_EVT_CMD_STATUS;
        buf[1] = 4;
        buf[2] = param[0];
        buf[3] = 1;
        buf[4] = src_id & 0xff;
        buf[5] = src_id >> 8;
        len = 6;
        break;

      case BT_HCI_LE_EVT:

        /* param[0] is the LE subevent code */

        adv = param[0] == BT_HCI_EVT_LE_ADVERTISING_REPORT;
        buf[0] = BT_HCI_EVT_LE_META_EVENT;
        buf[1] = param_len;
        memcpy(&buf[2], param, param_len);
        len = 2 + param_len;
        break;

      case BT_HCI_EVT:

        /* src_id is the event code */

        buf[0] = src_id;
        buf[1] = param_len;
        memcpy(&buf[2], param, param_len);
        len = 2 + param_len;
        break;

      case BT_HCI_DBG_EVT:
        buf[0] = BLE_HCI_EVT_VENDOR;
        buf[1] = param_len;
        memcpy(&buf[2], param, param_len);
        len = 2 + param_len;
        break;

      case BT_HCI_ACL_DATA:

        /* The library writes the ACL header and payload */

        type = BT_ACL_IN;
        len  = bt_onchiphci_handle_rx_acl(param, buf);
        break;

      default:
        wlwarn("Unsupported HCI packet type %u\n", pkt_type);
        return;
    }

  bl616cl_ble_deliver(type, buf, len, adv);
}

/****************************************************************************
 * Name: bl616cl_ble_open
 *
 * Description:
 *   Called on the first open of the HCI device: start the controller.
 *   The on-chip HCI task must be registered after the controller init,
 *   which resets the kernel task table; otherwise the first event to the
 *   host is scheduled to an empty task slot.
 *
 ****************************************************************************/

static int bl616cl_ble_open(FAR struct bt_driver_s *drv)
{
  int ret;

  ret = bl616cl_ble_controller_start();
  if (ret < 0)
    {
      return ret;
    }

  ret = bt_onchiphci_interface_init(bl616cl_ble_recv);
  if (ret != 0)
    {
      wlerr("ERROR: bt_onchiphci_interface_init failed: %d\n", ret);
      bl616cl_ble_controller_stop();
      return -EIO;
    }

  return 0;
}

/****************************************************************************
 * Name: bl616cl_ble_send
 *
 * Description:
 *   Pass one HCI command or ACL packet from the host to the controller.
 *   The controller copies the packet before this returns.
 *
 ****************************************************************************/

static int bl616cl_ble_send(FAR struct bt_driver_s *drv,
                            enum bt_buf_type_e type,
                            FAR void *data, size_t len)
{
  FAR uint8_t *buf = data;
  hci_pkt_struct pkt;
  uint16_t dest_id = 0;
  uint16_t handle;
  uint16_t acl_len;
  uint8_t pkt_type;
  int ret;

  memset(&pkt, 0, sizeof(pkt));

  switch (type)
    {
      case BT_CMD:
        if (len < sizeof(struct bt_hci_cmd_hdr_s) ||
            len != sizeof(struct bt_hci_cmd_hdr_s) + buf[2])
          {
            return -EINVAL;
          }

        pkt_type                  = BT_HCI_CMD;
        pkt.p.hci_cmd.opcode      = buf[0] | (buf[1] << 8);
        pkt.p.hci_cmd.param_len   = buf[2];
        pkt.p.hci_cmd.params      = &buf[3];
        break;

      case BT_ACL_OUT:
        if (len < sizeof(struct bt_hci_acl_hdr_s))
          {
            return -EINVAL;
          }

        handle  = buf[0] | (buf[1] << 8);
        acl_len = buf[2] | (buf[3] << 8);
        if (len != sizeof(struct bt_hci_acl_hdr_s) + acl_len)
          {
            return -EINVAL;
          }

        pkt_type                  = BT_HCI_ACL_DATA;
        dest_id                   = bt_acl_handle(handle);
        pkt.p.acl_data.conhdl     = dest_id;
        pkt.p.acl_data.pb_bc_flag = handle >> 12;
        pkt.p.acl_data.len        = acl_len;
        pkt.p.acl_data.buffer     = &buf[4];
        break;

      default:
        return -EINVAL;
    }

  ret = bt_onchiphci_send(pkt_type, dest_id, &pkt);
  if (ret != 0)
    {
      wlerr("ERROR: bt_onchiphci_send type %u failed: %d\n", pkt_type, ret);
      return -EIO;
    }

  return len;
}

/****************************************************************************
 * Name: bl616cl_ble_close
 *
 * Description:
 *   Called on the last close of the HCI device: stop the controller.
 *
 ****************************************************************************/

static void bl616cl_ble_close(FAR struct bt_driver_s *drv)
{
  bl616cl_ble_controller_stop();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_ble_initialize
 *
 * Description:
 *   Load the RF parameters and register the on-chip controller as the
 *   Bluetooth HCI device.
 *
 ****************************************************************************/

int bl616cl_ble_initialize(void)
{
#ifndef CONFIG_BL_COMPONENT_WL80211
  int ret;

  /* With Wi-Fi enabled the Wi-Fi adapter loads the RF parameters */

  ret = rfparam_init(0, NULL, 0);
  if (ret != 0)
    {
      wlerr("ERROR: rfparam_init failed: %d\n", ret);
      return -EIO;
    }
#endif

  return bt_driver_register(&g_ble.drv);
}
