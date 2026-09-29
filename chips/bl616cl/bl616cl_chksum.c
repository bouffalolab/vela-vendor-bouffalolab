/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_chksum.c
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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/net/ip.h>
#include <nuttx/net/netdev.h>

/* CONFIG_NET_ARCH_CHKSUM implementation.  The generic checksum() in
 * net/utils/net_chksum.c loads two bytes per 16-bit word, and on BL616CL
 * the IOB data sits in non-cacheable WRAM, so every byte is a bus access.
 * Here aligned 32-bit words are summed instead; the Internet checksum does
 * not depend on the byte order it is summed in (RFC 1071), so the
 * little-endian sum is folded and byte-swapped at the end.
 */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: chksum_fold
 *
 * Description:
 *   Fold a 64-bit sum into 16 bits by repeatedly adding the carries back in
 *   (one's complement addition).
 *
 * Input Parameters:
 *   sum - 64-bit partial sum
 *
 * Returned Value:
 *   The folded 16-bit sum.
 *
 ****************************************************************************/

static inline uint16_t chksum_fold(uint64_t sum)
{
  sum = (sum & 0xffffffff) + (sum >> 32);
  sum = (sum & 0xffff) + (sum >> 16);
  sum = (sum & 0xffff) + (sum >> 16);
  return (uint16_t)((sum & 0xffff) + (sum >> 16));
}

/****************************************************************************
 * Name: chksum_swap
 *
 * Description:
 *   Swap the two bytes of a 16-bit value.
 *
 * Input Parameters:
 *   sum - 16-bit value
 *
 * Returned Value:
 *   The byte-swapped value.
 *
 ****************************************************************************/

static inline uint16_t chksum_swap(uint16_t sum)
{
  return (uint16_t)((sum << 8) | (sum >> 8));
}

/****************************************************************************
 * Name: chksum_be
 *
 * Description:
 *   Return the one's complement sum of data as big-endian 16-bit words, with
 *   a trailing odd byte taken as the high byte of a last word. Sum aligned
 *   32-bit little-endian words for speed and fix up the byte order at the
 *   end.
 *
 * Input Parameters:
 *   data - Data to sum
 *   len - Length of data in bytes
 *
 * Returned Value:
 *   The folded 16-bit sum in big-endian word order.
 *
 ****************************************************************************/

static uint16_t chksum_be(FAR const uint8_t *data, uint16_t len)
{
  FAR const uint32_t *word;
  uint64_t sum = 0;
  bool shifted = false;
  uint16_t le;

  /* Sum little-endian words.  Starting from an odd address shifts the
   * byte pairing by one, which byte-swaps the result; undo that below.
   */

  if (((uintptr_t)data & 1) != 0 && len > 0)
    {
      sum     = (uint32_t)data[0] << 8;
      shifted = true;
      data++;
      len--;
    }

  if (((uintptr_t)data & 2) != 0 && len >= 2)
    {
      sum  += *(FAR const uint16_t *)data;
      data += 2;
      len  -= 2;
    }

  word = (FAR const uint32_t *)data;
  while (len >= 32)
    {
      sum += word[0];
      sum += word[1];
      sum += word[2];
      sum += word[3];
      sum += word[4];
      sum += word[5];
      sum += word[6];
      sum += word[7];
      word += 8;
      len  -= 32;
    }

  while (len >= 4)
    {
      sum += *word++;
      len -= 4;
    }

  data = (FAR const uint8_t *)word;
  if (len >= 2)
    {
      sum  += *(FAR const uint16_t *)data;
      data += 2;
      len  -= 2;
    }

  if (len > 0)
    {
      sum += data[0];
    }

  le = chksum_fold(sum);
  return shifted ? le : chksum_swap(le);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: checksum
 *
 * Description:
 *   CONFIG_NET_ARCH_CHKSUM architecture implementation. Continue the raw sum
 *   over data and len; see netdev.h. odd tells whether the previous region
 *   ended on an odd byte, whose word this region's first byte completes.
 *
 * Input Parameters:
 *   sum - Sum of the previous regions
 *   data - Data to add
 *   len - Length of data in bytes
 *   odd - On entry true if the previous region ended on an odd byte; updated
 *     to reflect this region
 *
 * Returned Value:
 *   The updated raw sum.
 *
 ****************************************************************************/

uint16_t checksum(uint16_t sum, FAR const uint8_t *data, uint16_t len,
                  FAR bool *odd)
{
  uint32_t total = sum;

  if (len == 0)
    {
      return sum;
    }

  if (*odd)
    {
      total += data[0];
      data++;
      len--;
    }

  total += chksum_be(data, len);
  *odd   = (len & 1) != 0;
  return chksum_fold(total);
}

/****************************************************************************
 * Name: chksum
 *
 * Description:
 *   CONFIG_NET_ARCH_CHKSUM architecture implementation. Continue the raw sum
 *   over one contiguous region that starts on a word boundary.
 *
 * Input Parameters:
 *   sum - Sum of the previous regions
 *   data - Data to add
 *   len - Length of data in bytes
 *
 * Returned Value:
 *   The updated raw sum.
 *
 ****************************************************************************/

uint16_t chksum(uint16_t sum, FAR const uint8_t *data, uint16_t len)
{
  bool odd = false;

  return checksum(sum, data, len, &odd);
}

/****************************************************************************
 * Name: net_chksum
 *
 * Description:
 *   CONFIG_NET_ARCH_CHKSUM architecture implementation. Calculate the one's
 *   complement sum of a buffer, converted with HTONS().
 *
 * Input Parameters:
 *   data - Data to sum
 *   len - Length of data in bytes
 *
 * Returned Value:
 *   The sum in network byte order.
 *
 ****************************************************************************/

uint16_t net_chksum(FAR uint16_t *data, uint16_t len)
{
  return HTONS(chksum(0, (FAR const uint8_t *)data, len));
}

#ifdef CONFIG_NET_IPv4

/* The upper-layer helpers below follow net/utils/net_ipchksum.c; they are
 * compiled out there when CONFIG_NET_ARCH_CHKSUM is set.
 */

uint16_t ipv4_upperlayer_header_chksum(FAR struct net_driver_s *dev,
                                       uint8_t proto);
uint16_t ipv4_upperlayer_payload_chksum(FAR struct net_driver_s *dev,
                                        uint16_t sum);

/****************************************************************************
 * Name: ipv4_upperlayer_header_chksum
 *
 * Description:
 *   CONFIG_NET_ARCH_CHKSUM architecture implementation. Sum the IPv4
 *   pseudo-header of the packet in dev: the upper-layer length, the protocol,
 *   and the source and destination addresses.
 *
 * Input Parameters:
 *   dev - Network device holding the packet
 *   proto - Upper-layer protocol number
 *
 * Returned Value:
 *   The pseudo-header sum.
 *
 ****************************************************************************/

uint16_t ipv4_upperlayer_header_chksum(FAR struct net_driver_s *dev,
                                       uint8_t proto)
{
  FAR struct ipv4_hdr_s *ipv4 = IPv4BUF;
  uint16_t iphdrlen = (ipv4->vhl & IPv4_HLMASK) << 2;
  uint16_t upperlen;

  upperlen = (((uint16_t)(ipv4->len[0]) << 8) + ipv4->len[1]) - iphdrlen;

  /* Pseudo-header: protocol, length, then source and destination. */

  return chksum(upperlen + proto, (FAR uint8_t *)&ipv4->srcipaddr,
                2 * sizeof(in_addr_t));
}

/****************************************************************************
 * Name: ipv4_upperlayer_payload_chksum
 *
 * Description:
 *   CONFIG_NET_ARCH_CHKSUM architecture implementation. Continue the sum over
 *   the upper-layer payload in the packet IOB chain, skipping the IPv4
 *   header.
 *
 * Input Parameters:
 *   dev - Network device holding the packet
 *   sum - Sum of the pseudo-header
 *
 * Returned Value:
 *   The updated sum.
 *
 ****************************************************************************/

uint16_t ipv4_upperlayer_payload_chksum(FAR struct net_driver_s *dev,
                                        uint16_t sum)
{
  FAR struct ipv4_hdr_s *ipv4 = IPv4BUF;

  return chksum_iob(sum, dev->d_iob, (ipv4->vhl & IPv4_HLMASK) << 2);
}

/****************************************************************************
 * Name: ipv4_upperlayer_chksum
 *
 * Description:
 *   CONFIG_NET_ARCH_CHKSUM architecture implementation. Calculate the
 *   upper-layer (TCP, UDP, ICMP) checksum of an IPv4 packet from the
 *   pseudo-header and payload sums.
 *
 * Input Parameters:
 *   dev - Network device holding the packet
 *   proto - Upper-layer protocol number
 *
 * Returned Value:
 *   The checksum in network byte order; 0xffff if the sum is zero.
 *
 ****************************************************************************/

uint16_t ipv4_upperlayer_chksum(FAR struct net_driver_s *dev, uint8_t proto)
{
  uint16_t sum;

  sum = ipv4_upperlayer_header_chksum(dev, proto);
  sum = ipv4_upperlayer_payload_chksum(dev, sum);
  return (sum == 0) ? 0xffff : HTONS(sum);
}

/****************************************************************************
 * Name: ipv4_chksum
 *
 * Description:
 *   CONFIG_NET_ARCH_CHKSUM architecture implementation. Calculate the
 *   checksum of an IPv4 header, whose length is taken from the header.
 *
 * Input Parameters:
 *   ipv4 - IPv4 header
 *
 * Returned Value:
 *   The checksum in network byte order; 0xffff if the sum is zero.
 *
 ****************************************************************************/

uint16_t ipv4_chksum(FAR struct ipv4_hdr_s *ipv4)
{
  uint16_t sum;

  sum = chksum(0, (FAR const uint8_t *)ipv4, (ipv4->vhl & IPv4_HLMASK) << 2);
  return (sum == 0) ? 0xffff : HTONS(sum);
}
#endif /* CONFIG_NET_IPv4 */

#ifdef CONFIG_NET_IPv6

uint16_t ipv6_upperlayer_header_chksum(FAR struct net_driver_s *dev,
                                       uint8_t proto, unsigned int iplen);
uint16_t ipv6_upperlayer_payload_chksum(FAR struct net_driver_s *dev,
                                        unsigned int iplen, uint16_t sum);

/****************************************************************************
 * Name: ipv6_upperlayer_header_chksum
 *
 * Description:
 *   CONFIG_NET_ARCH_CHKSUM architecture implementation. Sum the IPv6
 *   pseudo-header of the packet in dev: the upper-layer length (with
 *   extension headers removed), the protocol, and the source and destination
 *   addresses.
 *
 * Input Parameters:
 *   dev - Network device holding the packet
 *   proto - Upper-layer protocol number
 *   iplen - Length of the IPv6 header including extension headers
 *
 * Returned Value:
 *   The pseudo-header sum.
 *
 ****************************************************************************/

uint16_t ipv6_upperlayer_header_chksum(FAR struct net_driver_s *dev,
                                       uint8_t proto, unsigned int iplen)
{
  FAR struct ipv6_hdr_s *ipv6 = IPv6BUF;
  uint16_t upperlen;

  DEBUGASSERT(dev != NULL && iplen >= IPv6_HDRLEN);

  /* The IPv6 length includes any extension headers; drop them. */

  upperlen  = ((uint16_t)ipv6->len[0] << 8) + ipv6->len[1];
  upperlen -= (iplen - IPv6_HDRLEN);

  return chksum(upperlen + proto, (FAR uint8_t *)&ipv6->srcipaddr,
                2 * sizeof(net_ipv6addr_t));
}

/****************************************************************************
 * Name: ipv6_upperlayer_payload_chksum
 *
 * Description:
 *   CONFIG_NET_ARCH_CHKSUM architecture implementation. Continue the sum over
 *   the upper-layer payload in the packet IOB chain, skipping iplen bytes of
 *   IPv6 header.
 *
 * Input Parameters:
 *   dev - Network device holding the packet
 *   iplen - Length of the IPv6 header including extension headers
 *   sum - Sum of the pseudo-header
 *
 * Returned Value:
 *   The updated sum.
 *
 ****************************************************************************/

uint16_t ipv6_upperlayer_payload_chksum(FAR struct net_driver_s *dev,
                                        unsigned int iplen, uint16_t sum)
{
  return chksum_iob(sum, dev->d_iob, iplen);
}

/****************************************************************************
 * Name: ipv6_upperlayer_chksum
 *
 * Description:
 *   CONFIG_NET_ARCH_CHKSUM architecture implementation. Calculate the
 *   upper-layer (TCP, UDP, ICMPv6) checksum of an IPv6 packet from the
 *   pseudo-header and payload sums.
 *
 * Input Parameters:
 *   dev - Network device holding the packet
 *   proto - Upper-layer protocol number
 *   iplen - Length of the IPv6 header including extension headers
 *
 * Returned Value:
 *   The checksum in network byte order; 0xffff if the sum is zero.
 *
 ****************************************************************************/

uint16_t ipv6_upperlayer_chksum(FAR struct net_driver_s *dev,
                                uint8_t proto, unsigned int iplen)
{
  uint16_t sum;

  sum = ipv6_upperlayer_header_chksum(dev, proto, iplen);
  sum = ipv6_upperlayer_payload_chksum(dev, iplen, sum);
  return (sum == 0) ? 0xffff : HTONS(sum);
}
#endif /* CONFIG_NET_IPv6 */
