/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_rfparam_ext.c
 *
 * BL616CL rfparam adapter extensions for the STA-only build. The newer
 * rfparam blob (BL4 baseline) exposes ant-gain management and a country
 * code setter consumed by bl616cl_wifi_adapter ioctl paths; the current
 * drivers/rfparam sources do not carry them yet.
 *
 * This file provides the interface while keeping behavior explicit:
 * the ant gain is tracked in a static variable (0 dB default until the
 * RF calibration flow is wired up), and the country setter reports
 * EOPNOTSUPP so callers see a clear error instead of a silent no-op.
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <stdint.h>

#include "bl616cl_wifi_adapter.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int8_t g_rfparam_ant_gain;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rfparam_get_ant_gain
 *
 * Description:
 *   Return the currently applied antenna gain (dB). Zero until the RF
 *   calibration parameter flow provides a real value.
 *
 ****************************************************************************/

int8_t rfparam_get_ant_gain(void)
{
  return g_rfparam_ant_gain;
}

/****************************************************************************
 * Name: rfparam_update_ant_gain
 *
 * Description:
 *   Record a new antenna gain value. TODO(bl616cl-wifi): forward to the
 *   PHY/RF power management when the RF calibration parameter flow is
 *   ported for BL616CL.
 *
 ****************************************************************************/

void rfparam_update_ant_gain(int8_t ant_gain)
{
  g_rfparam_ant_gain = ant_gain;
}

/****************************************************************************
 * Name: rfparam_set_country_code
 *
 * Description:
 *   Country-code setter for the WLAN ioctl path. Not supported by the
 *   current BL616CL rfparam flow; reported explicitly to the caller.
 *
 * Returned Value:
 *   -EOPNOTSUPP always (see TODO above).
 *
 ****************************************************************************/

int rfparam_set_country_code(const char *country)
{
  UNUSED(country);
  return -EOPNOTSUPP;
}
