/****************************************************************************
 * vendor/bouffalolab/components/wireless/rfparam/bl616cl_rfparam_log.c
 *
 * Line-buffered logger for the rfparam adapter sources. rfparam prints a
 * table row as a LOG_I() header followed by one printf() per item and a
 * final "\r\n". syslog ends every message with a newline, so mapping the
 * pieces one by one to wlinfo() splits the row across lines. This logger
 * collects the pieces and emits the line once "\n" arrives.
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

#include <stdarg.h>
#include <stdio.h>

/* Tag the lines "rfparam:" instead of the name of this function */

#define EXTRA_FMT "rfparam: "
#define EXTRA_ARG

#include <debug.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_rfparam_log
 *
 * Description:
 *   Append the formatted text to the pending line.  When the text ends the
 *   line (or the buffer is full), log the line with wlinfo() without its
 *   trailing CR/LF.  rfparam only logs while initializing the Wi-Fi, from
 *   one context, so the pending line is not locked.
 *
 ****************************************************************************/

int bl616cl_rfparam_log(FAR const char *fmt, ...)
{
#ifdef CONFIG_DEBUG_WIRELESS_INFO
  static char line[256];
  static size_t len;
  va_list ap;
  int ret;

  va_start(ap, fmt);
  ret = vsnprintf(line + len, sizeof(line) - len, fmt, ap);
  va_end(ap);

  if (ret < 0)
    {
      return ret;
    }

  len += ret;
  if (len >= sizeof(line))
    {
      len = sizeof(line) - 1;
    }

  if (len > 0 && (line[len - 1] == '\n' || len == sizeof(line) - 1))
    {
      while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        {
          len--;
        }

      line[len] = '\0';
      wlinfo("%s\n", line);
      len = 0;
    }

  return ret;
#else
  return 0;
#endif
}
