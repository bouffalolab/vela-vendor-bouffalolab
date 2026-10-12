/****************************************************************************
 * vendor/bouffalolab/apps/wireless_tests/pmksa/pmksa_main.c
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

/* Raw PMKSA / key ioctls for host-driven tests:
 *
 *   pmksa get [len]          SIOCGIWPMKSA into a len-byte buffer
 *   pmksa set <pmk> [len]    SIOCSIWPMKSA with length len
 *   pmksa key [key]          SIOCSIWENCODEEXT, CCMP; no key clears
 *
 * All on wlan0: with an interface argument a 64-digit PMK no longer fits
 * the 80-byte NSH line.  len defaults to strlen(pmk) for set and 64 for
 * get.  Every command prints one line "pmksa: <cmd> ret=<0 or -errno> ..."
 * for the script.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/net/netconfig.h>
#include <nuttx/wireless/wireless.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PMKSA_IFNAME  "wlan0"
#define PMKSA_BUF_MAX 128

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int pmksa_ioctl(int sock, int cmd, FAR struct iwreq *iwr)
{
  return ioctl(sock, cmd, (unsigned long)((uintptr_t)iwr)) < 0 ? -errno : 0;
}

static long pmksa_elapsed_ms(FAR const struct timespec *start)
{
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - start->tv_sec) * 1000 +
         (now.tv_nsec - start->tv_nsec) / 1000000;
}

static int pmksa_get(int sock, FAR const char *ifname, int len)
{
  char buf[PMKSA_BUF_MAX + 1];
  struct timespec start;
  struct iwreq iwr;
  int ret;

  memset(buf, 0, sizeof(buf));
  memset(&iwr, 0, sizeof(iwr));
  strlcpy(iwr.ifr_name, ifname, IFNAMSIZ);
  iwr.u.data.pointer = buf;
  iwr.u.data.length = len;

  clock_gettime(CLOCK_MONOTONIC, &start);
  ret = pmksa_ioctl(sock, SIOCGIWPMKSA, &iwr);
  printf("pmksa: get ret=%d len=%d pmk=%s time_ms=%ld\n",
         ret, len, ret == 0 ? buf : "-", pmksa_elapsed_ms(&start));
  return ret;
}

static int pmksa_set(int sock, FAR const char *ifname, FAR const char *pmk,
                     int len)
{
  char buf[PMKSA_BUF_MAX];
  struct iwreq iwr;
  int ret;

  memset(buf, 0, sizeof(buf));
  memcpy(buf, pmk, strnlen(pmk, sizeof(buf)));
  memset(&iwr, 0, sizeof(iwr));
  strlcpy(iwr.ifr_name, ifname, IFNAMSIZ);
  iwr.u.data.pointer = buf;
  iwr.u.data.length = len;

  ret = pmksa_ioctl(sock, SIOCSIWPMKSA, &iwr);
  printf("pmksa: set ret=%d len=%d\n", ret, len);
  return ret;
}

static int pmksa_key(int sock, FAR const char *ifname, FAR const char *key)
{
  FAR struct iw_encode_ext *ext;
  size_t len = key != NULL ? strlen(key) : 0;
  struct iwreq iwr;
  int ret;

  ext = zalloc(sizeof(*ext) + len);
  if (ext == NULL)
    {
      return -ENOMEM;
    }

  ext->alg = len > 0 ? IW_ENCODE_ALG_CCMP : IW_ENCODE_ALG_NONE;
  ext->key_len = len;
  memcpy(ext->key, key, len);

  memset(&iwr, 0, sizeof(iwr));
  strlcpy(iwr.ifr_name, ifname, IFNAMSIZ);
  iwr.u.encoding.pointer = ext;
  iwr.u.encoding.length = sizeof(*ext) + len;

  ret = pmksa_ioctl(sock, SIOCSIWENCODEEXT, &iwr);
  printf("pmksa: key ret=%d len=%zu\n", ret, len);
  free(ext);
  return ret;
}

static void pmksa_usage(void)
{
  printf("Usage:\n"
         "  pmksa get [len]\n"
         "  pmksa set <pmk> [len]\n"
         "  pmksa key [key]\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  int sock;
  int len;
  int ret;

  if (argc < 2)
    {
      pmksa_usage();
      return EXIT_FAILURE;
    }

  sock = socket(NET_SOCK_FAMILY, NET_SOCK_TYPE, NET_SOCK_PROTOCOL);
  if (sock < 0)
    {
      printf("pmksa: socket failed: %d\n", errno);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "get") == 0)
    {
      len = argc > 2 ? atoi(argv[2]) : 64;
      ret = len >= 0 && len <= PMKSA_BUF_MAX ?
            pmksa_get(sock, PMKSA_IFNAME, len) : -EINVAL;
    }
  else if (strcmp(argv[1], "set") == 0 && argc > 2)
    {
      len = argc > 3 ? atoi(argv[3]) : (int)strlen(argv[2]);
      ret = len >= 0 && len <= PMKSA_BUF_MAX ?
            pmksa_set(sock, PMKSA_IFNAME, argv[2], len) : -EINVAL;
    }
  else if (strcmp(argv[1], "key") == 0)
    {
      ret = pmksa_key(sock, PMKSA_IFNAME, argc > 2 ? argv[2] : NULL);
    }
  else
    {
      pmksa_usage();
      ret = -EINVAL;
    }

  close(sock);
  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
