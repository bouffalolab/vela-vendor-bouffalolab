/****************************************************************************
 * vendor/bouffalolab/apps/mcu_peripheral_tests/crypto/crypto_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/compiler.h>

#include <sys/ioctl.h>
#include <sys/param.h>

#include <crypto/cryptodev.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CRYPTO_DEVICE     "/dev/crypto"
#define CRYPTO_BLOCK_SIZE 16
#define CRYPTO_MAX_KEY    64
#define CRYPTO_MAX_LEN    5000
#define CRYPTO_MAX_PIECES 4
#define CRYPTO_GUARD_SIZE 8
#define CRYPTO_BUF_SIZE   (CRYPTO_MAX_LEN + CRYPTO_GUARD_SIZE)
#define CRYPTO_NSESSIONS  3
#define CRYPTO_NBUFS      (CRYPTO_NSESSIONS + 1)
#define CRYPTO_CANARY     0xa5
#define CRYPTO_DATA_SEED  0x5b

/* Expected FNV-1a checksums of the ciphertext, printed by
 * crypto_vectors.py from an independent AES implementation.
 */

#define CRYPTO_CBC_BULK_FNV UINT32_C(0x6b7607fa)
#define CRYPTO_CTR_WORD_FNV UINT32_C(0xe62bc615)
#define CRYPTO_CTR_ALL_FNV  UINT32_C(0x9f224e8a)
#define CRYPTO_CTR_BULK_FNV UINT32_C(0x1ab3f11e)
#define CRYPTO_XTS_BULK_FNV UINT32_C(0x412a210c)
#define CRYPTO_XTS_128_FNV  UINT32_C(0x85abb628)

#define CRYPTO_SHA1_BULK_FNV    UINT32_C(0xd0ba7a07)
#define CRYPTO_SHA1_EMPTY_FNV   UINT32_C(0xbc65db8b)
#define CRYPTO_SHA224_BULK_FNV  UINT32_C(0xdf6d72cf)
#define CRYPTO_SHA224_EMPTY_FNV UINT32_C(0x4c2ef116)
#define CRYPTO_SHA256_BULK_FNV  UINT32_C(0x08310087)
#define CRYPTO_SHA256_EMPTY_FNV UINT32_C(0xff3c98e2)
#define CRYPTO_SHA384_BULK_FNV  UINT32_C(0x5f551922)
#define CRYPTO_SHA384_EMPTY_FNV UINT32_C(0xbcd2d1d3)
#define CRYPTO_SHA512_BULK_FNV  UINT32_C(0x6b94fc4a)
#define CRYPTO_SHA512_EMPTY_FNV UINT32_C(0xe64d8497)

/* Hashed message length; a multiple of neither SHA block size. */

#define CRYPTO_HASH_LEN    3000
#define CRYPTO_MAX_DIGEST  64
#define CRYPTO_NHASH_SESSIONS 2

/* Requests around the default 256-byte threshold of the driver's direct
 * path, and a large one; all are whole AES blocks.
 */

#define CRYPTO_DIRECT_MAX     4096
#define CRYPTO_DIRECT_STACK   288
#define CRYPTO_DIRECT_BUF     (CRYPTO_DIRECT_MAX + 64)
#define CRYPTO_LINE_SIZE      32
#define CRYPTO_NWORKERS       4
#define CRYPTO_WORKER_LOOPS   50
#define CRYPTO_PERF_LOOPS     64

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* A message of crypto_fill(CRYPTO_DATA_SEED) bytes, encrypted in one call
 * or in pieces continued with COP_FLAG_UPDATE.
 */

struct crypto_vector_s
{
  const char *name;
  uint32_t cipher;
  uint8_t keyseed;                  /* Key bytes are crypto_fill(keyseed) */
  uint8_t keylen;
  uint8_t iv[CRYPTO_BLOCK_SIZE];
  size_t len;
  uint32_t fnv;                     /* Expected ciphertext checksum */
  size_t pieces[CRYPTO_MAX_PIECES]; /* Streaming split, ends with 0 */
};

/* A hash of CRYPTO_HASH_LEN bytes of crypto_fill(CRYPTO_DATA_SEED). */

struct crypto_hash_s
{
  const char *name;
  uint32_t mac;
  size_t digestlen;
  uint32_t fnv;                     /* Expected digest checksum */
  uint32_t emptyfnv;                /* Digest checksum of no data */
};

/* One thread of the concurrency test. */

struct crypto_worker_s
{
  int id;
  int result;
  const uint8_t *expect;            /* Reference output of the message */
};

/* A NIST SP 800-38A F.2 CBC encryption vector. */

struct crypto_kat_s
{
  const char *name;
  uint32_t cipher;
  const uint8_t *key;
  size_t keylen;
  const uint8_t *ct;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The bulk lengths cross the default 1024-byte bounce buffer. */

static const struct crypto_vector_s g_cbc_bulk =
{
  "cbc-bulk", CRYPTO_AES_CBC, 0x30, 16,
  {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf
  },
  2064, CRYPTO_CBC_BULK_FNV,
  {
    1040, 1024
  }
};

/* The low 32 bits of the counter wrap after two blocks and carry into the
 * next word; the second piece starts at the wrap.
 */

static const struct crypto_vector_s g_ctr_word =
{
  "ctr-word", CRYPTO_AES_CTR, 0x30, 16,
  {
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xff, 0xff, 0xff, 0xfe
  },
  69, CRYPTO_CTR_WORD_FNV,
  {
    32, 37
  }
};

/* The carry ripples through all 128 bits and wraps to zero inside the
 * second piece.
 */

static const struct crypto_vector_s g_ctr_all =
{
  "ctr-all", CRYPTO_AES_CTR, 0x30, 16,
  {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe
  },
  69, CRYPTO_CTR_ALL_FNV,
  {
    16, 32, 21
  }
};

/* The low 32 bits wrap after 240 blocks, inside a bounce buffer chunk. */

static const struct crypto_vector_s g_ctr_bulk =
{
  "ctr-bulk", CRYPTO_AES_CTR, 0x91, 32,
  {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0xff, 0xff, 0xff, 0x10
  },
  5000, CRYPTO_CTR_BULK_FNV,
  {
    1040, 3008, 952
  }
};

/* One 4096-byte data unit (sector 0x1234) with two 256-bit keys. */

static const struct crypto_vector_s g_xts_bulk =
{
  "xts-bulk", CRYPTO_AES_XTS, 0x30, 64,
  {
    0x34, 0x12
  },
  4096, CRYPTO_XTS_BULK_FNV,
  {
    16, 1008, 1040, 2032
  }
};

static const struct crypto_vector_s g_xts_128 =
{
  "xts-128", CRYPTO_AES_XTS, 0x91, 32,
  {
    0x07
  },
  1040, CRYPTO_XTS_128_FNV,
  {
    528, 512
  }
};

static const uint8_t g_nist_iv[CRYPTO_BLOCK_SIZE] =
{
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

static const uint8_t g_nist_pt[64] =
{
  0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
  0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
  0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
  0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
  0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
  0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
  0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
  0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
};

static const uint8_t g_nist_key192[24] =
{
  0x8e, 0x73, 0xb0, 0xf7, 0xda, 0x0e, 0x64, 0x52,
  0xc8, 0x10, 0xf3, 0x2b, 0x80, 0x90, 0x79, 0xe5,
  0x62, 0xf8, 0xea, 0xd2, 0x52, 0x2c, 0x6b, 0x7b
};

static const uint8_t g_nist_ct192[64] =
{
  0x4f, 0x02, 0x1d, 0xb2, 0x43, 0xbc, 0x63, 0x3d,
  0x71, 0x78, 0x18, 0x3a, 0x9f, 0xa0, 0x71, 0xe8,
  0xb4, 0xd9, 0xad, 0xa9, 0xad, 0x7d, 0xed, 0xf4,
  0xe5, 0xe7, 0x38, 0x76, 0x3f, 0x69, 0x14, 0x5a,
  0x57, 0x1b, 0x24, 0x20, 0x12, 0xfb, 0x7a, 0xe0,
  0x7f, 0xa9, 0xba, 0xac, 0x3d, 0xf1, 0x02, 0xe0,
  0x08, 0xb0, 0xe2, 0x79, 0x88, 0x59, 0x88, 0x81,
  0xd9, 0x20, 0xa9, 0xe6, 0x4f, 0x56, 0x15, 0xcd
};

static const uint8_t g_nist_key256[32] =
{
  0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
  0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
  0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
  0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
};

static const uint8_t g_nist_ct256[64] =
{
  0xf5, 0x8c, 0x4c, 0x04, 0xd6, 0xe5, 0xf1, 0xba,
  0x77, 0x9e, 0xab, 0xfb, 0x5f, 0x7b, 0xfb, 0xd6,
  0x9c, 0xfc, 0x4e, 0x96, 0x7e, 0xdb, 0x80, 0x8d,
  0x67, 0x9f, 0x77, 0x7b, 0xc6, 0x70, 0x2c, 0x7d,
  0x39, 0xf2, 0x33, 0x69, 0xa9, 0xd9, 0xba, 0xcf,
  0xa5, 0x30, 0xe2, 0x63, 0x04, 0x23, 0x14, 0x61,
  0xb2, 0xeb, 0x05, 0xe2, 0xc3, 0x9b, 0xe9, 0xfc,
  0xda, 0x6c, 0x19, 0x07, 0x8c, 0x6a, 0x9d, 0x1b
};

/* The three CBC ids are the same cipher; the key length selects AES-128,
 * AES-192 or AES-256.
 */

static const struct crypto_kat_s g_kats[] =
{
  {
    "cbc192", CRYPTO_AES_192_CBC, g_nist_key192, sizeof(g_nist_key192),
    g_nist_ct192
  },
  {
    "cbc256", CRYPTO_AES_256_CBC, g_nist_key256, sizeof(g_nist_key256),
    g_nist_ct256
  },
  {
    "cbc-id-256", CRYPTO_AES_CBC, g_nist_key256, sizeof(g_nist_key256),
    g_nist_ct256
  }
};

#ifdef CONFIG_BL616CL_CRYPTO_SHA
static const struct crypto_hash_s g_hashes[] =
{
  {
    "sha1", CRYPTO_SHA1, 20, CRYPTO_SHA1_BULK_FNV, CRYPTO_SHA1_EMPTY_FNV
  },
  {
    "sha224", CRYPTO_SHA2_224, 28, CRYPTO_SHA224_BULK_FNV,
    CRYPTO_SHA224_EMPTY_FNV
  },
  {
    "sha256", CRYPTO_SHA2_256, 32, CRYPTO_SHA256_BULK_FNV,
    CRYPTO_SHA256_EMPTY_FNV
  },
  {
    "sha384", CRYPTO_SHA2_384, 48, CRYPTO_SHA384_BULK_FNV,
    CRYPTO_SHA384_EMPTY_FNV
  },
  {
    "sha512", CRYPTO_SHA2_512, 64, CRYPTO_SHA512_BULK_FNV,
    CRYPTO_SHA512_EMPTY_FNV
  }
};

/* After the 1-byte piece, the next piece completes a SHA-256 block from 63
 * bytes and the engine reads the following blocks from an odd address.
 * The last piece crosses the default 1024-byte bounce buffer.
 */

static const size_t g_hash_pieces[CRYPTO_MAX_PIECES] =
{
  1, 200, 1000, 1799
};

/* FIPS 180-2 "abc" digests of the truncated SHA-2 variants. */

static const uint8_t g_sha224_abc[28] =
{
  0x23, 0x09, 0x7d, 0x22, 0x34, 0x05, 0xd8, 0x22,
  0x86, 0x42, 0xa4, 0x77, 0xbd, 0xa2, 0x55, 0xb3,
  0x2a, 0xad, 0xbc, 0xe4, 0xbd, 0xa0, 0xb3, 0xf7,
  0xe3, 0x6c, 0x9d, 0xa7
};

static const uint8_t g_sha384_abc[48] =
{
  0xcb, 0x00, 0x75, 0x3f, 0x45, 0xa3, 0x5e, 0x8b,
  0xb5, 0xa0, 0x3d, 0x69, 0x9a, 0xc6, 0x50, 0x07,
  0x27, 0x2c, 0x32, 0xab, 0x0e, 0xde, 0xd1, 0x63,
  0x1a, 0x8b, 0x60, 0x5a, 0x43, 0xff, 0x5b, 0xed,
  0x80, 0x86, 0x07, 0x2b, 0xa1, 0xe7, 0xcc, 0x23,
  0x58, 0xba, 0xec, 0xa1, 0x34, 0xc8, 0x25, 0xa7
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void crypto_usage(const char *progname)
{
  printf("Usage: %s <all|cbc|ctr|xts|sha|session|edge|direct>\n",
         progname);
}

static void crypto_fill(uint8_t *buffer, size_t len, uint8_t seed)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      buffer[i] = (uint8_t)(seed + i * 13 + (i >> 7));
    }
}

static uint32_t crypto_checksum(const uint8_t *buffer, size_t len)
{
  uint32_t hash = UINT32_C(2166136261);
  size_t i;

  for (i = 0; i < len; i++)
    {
      hash ^= buffer[i];
      hash *= UINT32_C(16777619);
    }

  return hash;
}

static void crypto_guard(uint8_t *buffer, size_t len)
{
  memset(buffer + len, CRYPTO_CANARY, CRYPTO_GUARD_SIZE);
}

/* Fill a digest buffer and its guard before a finish, so a finish that
 * returns 0 without writing the digest is caught.
 */

static void crypto_digest_reset(uint8_t *digest, size_t len)
{
  memset(digest, CRYPTO_CANARY, len + CRYPTO_GUARD_SIZE);
}

static bool crypto_guard_ok(const uint8_t *buffer, size_t len)
{
  size_t i;

  for (i = 0; i < CRYPTO_GUARD_SIZE; i++)
    {
      if (buffer[len + i] != CRYPTO_CANARY)
        {
          return false;
        }
    }

  return true;
}

static int crypto_open(void)
{
  int cfd = -1;
  int fd;

  fd = open(CRYPTO_DEVICE, O_RDWR);
  if (fd < 0)
    {
      printf("CRYPTO_TEST FAIL open errno=%d\n", errno);
      return -1;
    }

  if (ioctl(fd, CRIOGET, &cfd) < 0)
    {
      printf("CRYPTO_TEST FAIL CRIOGET errno=%d\n", errno);
      cfd = -1;
    }

  close(fd);
  return cfd;
}

static int crypto_session_new(int cfd, uint32_t cipher,
                              const uint8_t *key, size_t keylen,
                              uint32_t *ses)
{
  struct session_op sop;

  memset(&sop, 0, sizeof(sop));
  sop.cipher = cipher;
  sop.op = COP_ENCRYPT;
  sop.keylen = keylen;
  sop.key = (caddr_t)key;

  if (ioctl(cfd, CIOCGSESSION, &sop) < 0)
    {
      return -errno;
    }

  *ses = sop.ses;
  return 0;
}

static void crypto_session_free(int cfd, uint32_t ses)
{
  ioctl(cfd, CIOCFSESSION, &ses);
}

static int crypto_crypt(int cfd, uint32_t ses, uint16_t op, uint16_t flags,
                        const uint8_t *iv, const uint8_t *src,
                        uint8_t *dst, size_t len)
{
  struct crypt_op cop;

  memset(&cop, 0, sizeof(cop));
  cop.ses = ses;
  cop.op = op;
  cop.flags = flags;
  cop.len = len;
  cop.olen = len;
  cop.ivlen = CRYPTO_BLOCK_SIZE;
  cop.src = (caddr_t)src;
  cop.dst = (caddr_t)dst;
  cop.iv = (caddr_t)iv;

  return ioctl(cfd, CIOCCRYPT, &cop) < 0 ? -errno : 0;
}

/* Run a vector in one call, or in its pieces when split is true. Later
 * pieces still pass the IV, which COP_FLAG_UPDATE must ignore.
 */

static int crypto_stream(int cfd, uint32_t ses, uint16_t op,
                         const struct crypto_vector_s *vector,
                         const uint8_t *src, uint8_t *dst, bool split)
{
  size_t off = 0;
  int ret;
  int i;

  if (!split)
    {
      return crypto_crypt(cfd, ses, op, 0, vector->iv, src, dst,
                          vector->len);
    }

  for (i = 0; i < CRYPTO_MAX_PIECES && vector->pieces[i] != 0; i++)
    {
      ret = crypto_crypt(cfd, ses, op, i == 0 ? 0 : COP_FLAG_UPDATE,
                         vector->iv, src + off, dst + off,
                         vector->pieces[i]);
      if (ret < 0)
        {
          return ret;
        }

      off += vector->pieces[i];
    }

  return off == vector->len ? 0 : -EINVAL;
}

/* Check one result: the call succeeded, nothing was written past len, and
 * the output is the plaintext (plain != NULL) or has the expected
 * ciphertext checksum.
 */

static int crypto_expect(const struct crypto_vector_s *vector,
                         const char *step, int ret, const uint8_t *buffer,
                         const uint8_t *plain)
{
  uint32_t checksum;

  if (ret < 0)
    {
      printf("CRYPTO_TEST %s FAIL step=%s ret=%d\n",
             vector->name, step, ret);
      return -1;
    }

  if (!crypto_guard_ok(buffer, vector->len))
    {
      printf("CRYPTO_TEST %s FAIL step=%s overrun\n", vector->name, step);
      return -1;
    }

  if (plain != NULL)
    {
      if (memcmp(buffer, plain, vector->len) != 0)
        {
          printf("CRYPTO_TEST %s FAIL step=%s plaintext mismatch\n",
                 vector->name, step);
          return -1;
        }

      return 0;
    }

  checksum = crypto_checksum(buffer, vector->len);
  if (checksum != vector->fnv)
    {
      printf("CRYPTO_TEST %s FAIL step=%s checksum=%08" PRIx32
             " expected=%08" PRIx32 "\n",
             vector->name, step, checksum, vector->fnv);
      return -1;
    }

  return 0;
}

/* Encrypt and decrypt a vector in one call, in pieces, and in place. */

static int crypto_check_vector(int cfd, const struct crypto_vector_s *vector,
                               uint8_t *buf[])
{
  uint8_t key[CRYPTO_MAX_KEY];
  uint8_t *in = buf[0];
  uint8_t *out = buf[1];
  uint8_t *aux = buf[2];
  uint32_t ses;
  int ret;

  crypto_fill(key, vector->keylen, vector->keyseed);
  ret = crypto_session_new(cfd, vector->cipher, key, vector->keylen, &ses);
  if (ret < 0)
    {
      printf("CRYPTO_TEST %s FAIL session ret=%d\n", vector->name, ret);
      return -1;
    }

  crypto_fill(in, vector->len, CRYPTO_DATA_SEED);

  crypto_guard(out, vector->len);
  ret = crypto_stream(cfd, ses, COP_ENCRYPT, vector, in, out, false);
  if (crypto_expect(vector, "encrypt", ret, out, NULL) < 0)
    {
      goto fail;
    }

  crypto_guard(aux, vector->len);
  ret = crypto_stream(cfd, ses, COP_DECRYPT, vector, out, aux, false);
  if (crypto_expect(vector, "decrypt", ret, aux, in) < 0)
    {
      goto fail;
    }

  crypto_guard(aux, vector->len);
  ret = crypto_stream(cfd, ses, COP_ENCRYPT, vector, in, aux, true);
  if (crypto_expect(vector, "encrypt-split", ret, aux, NULL) < 0)
    {
      goto fail;
    }

  ret = crypto_stream(cfd, ses, COP_DECRYPT, vector, aux, aux, true);
  if (crypto_expect(vector, "decrypt-split-inplace", ret, aux, in) < 0)
    {
      goto fail;
    }

  ret = crypto_stream(cfd, ses, COP_ENCRYPT, vector, aux, aux, false);
  if (crypto_expect(vector, "encrypt-inplace", ret, aux, NULL) < 0)
    {
      goto fail;
    }

  crypto_session_free(cfd, ses);
  printf("CRYPTO_TEST vector=%s PASS len=%zu checksum=%08" PRIx32 "\n",
         vector->name, vector->len, vector->fnv);
  return 0;

fail:
  crypto_session_free(cfd, ses);
  return -1;
}

/* Encrypt a NIST vector from src to dst, then decrypt it in place at
 * work. The addresses may be unaligned.
 */

static int crypto_check_kat(int cfd, const struct crypto_kat_s *kat,
                            uint8_t *src, uint8_t *dst, uint8_t *work)
{
  uint32_t ses;
  int ret;

  ret = crypto_session_new(cfd, kat->cipher, kat->key, kat->keylen, &ses);
  if (ret < 0)
    {
      printf("CRYPTO_TEST %s FAIL session ret=%d\n", kat->name, ret);
      return -1;
    }

  memcpy(src, g_nist_pt, sizeof(g_nist_pt));
  ret = crypto_crypt(cfd, ses, COP_ENCRYPT, 0, g_nist_iv, src, dst,
                     sizeof(g_nist_pt));
  if (ret < 0 || memcmp(dst, kat->ct, sizeof(g_nist_pt)) != 0)
    {
      printf("CRYPTO_TEST %s FAIL encrypt ret=%d src=%p dst=%p\n",
             kat->name, ret, src, dst);
      crypto_session_free(cfd, ses);
      return -1;
    }

  memcpy(work, kat->ct, sizeof(g_nist_pt));
  ret = crypto_crypt(cfd, ses, COP_DECRYPT, 0, g_nist_iv, work, work,
                     sizeof(g_nist_pt));
  crypto_session_free(cfd, ses);
  if (ret < 0 || memcmp(work, g_nist_pt, sizeof(g_nist_pt)) != 0)
    {
      printf("CRYPTO_TEST %s FAIL decrypt-inplace ret=%d work=%p\n",
             kat->name, ret, work);
      return -1;
    }

  return 0;
}

static int crypto_test_cbc(int cfd, uint8_t *buf[])
{
  size_t i;

  for (i = 0; i < sizeof(g_kats) / sizeof(g_kats[0]); i++)
    {
      if (crypto_check_kat(cfd, &g_kats[i], buf[0], buf[1], buf[2]) < 0)
        {
          return -1;
        }

      printf("CRYPTO_TEST kat=%s PASS\n", g_kats[i].name);
    }

  if (crypto_check_vector(cfd, &g_cbc_bulk, buf) < 0)
    {
      return -1;
    }

  printf("CRYPTO_TEST RESULT case=cbc PASS\n");
  return 0;
}

static int crypto_test_ctr(int cfd, uint8_t *buf[])
{
  if (crypto_check_vector(cfd, &g_ctr_word, buf) < 0 ||
      crypto_check_vector(cfd, &g_ctr_all, buf) < 0 ||
      crypto_check_vector(cfd, &g_ctr_bulk, buf) < 0)
    {
      return -1;
    }

  printf("CRYPTO_TEST RESULT case=ctr PASS\n");
  return 0;
}

static int crypto_test_xts(int cfd, uint8_t *buf[])
{
  if (crypto_check_vector(cfd, &g_xts_bulk, buf) < 0 ||
      crypto_check_vector(cfd, &g_xts_128, buf) < 0)
    {
      return -1;
    }

  printf("CRYPTO_TEST RESULT case=xts PASS\n");
  return 0;
}

#ifdef CONFIG_BL616CL_CRYPTO_SHA
static int crypto_hash_session(int cfd, uint32_t mac, uint32_t *ses)
{
  struct session_op sop;

  memset(&sop, 0, sizeof(sop));
  sop.mac = mac;

  if (ioctl(cfd, CIOCGSESSION, &sop) < 0)
    {
      return -errno;
    }

  *ses = sop.ses;
  return 0;
}

/* Add data with COP_FLAG_UPDATE, or finish into mac without it. */

static int crypto_hash_op(int cfd, uint32_t ses, uint16_t flags,
                          const uint8_t *src, size_t len, uint8_t *mac,
                          size_t olen)
{
  struct crypt_op cop;

  memset(&cop, 0, sizeof(cop));
  cop.ses = ses;
  cop.op = COP_ENCRYPT;
  cop.flags = flags;
  cop.len = len;
  cop.olen = olen;
  cop.src = (caddr_t)src;
  cop.mac = (caddr_t)mac;

  return ioctl(cfd, CIOCCRYPT, &cop) < 0 ? -errno : 0;
}

/* Check a digest: the call succeeded, exactly digestlen bytes were
 * written, and the digest has the expected checksum.
 */

static int crypto_expect_digest(const struct crypto_hash_s *hash,
                                const char *step, int ret,
                                const uint8_t *digest, uint32_t fnv)
{
  uint32_t checksum;

  if (ret < 0)
    {
      printf("CRYPTO_TEST %s FAIL step=%s ret=%d\n", hash->name, step, ret);
      return -1;
    }

  if (!crypto_guard_ok(digest, hash->digestlen))
    {
      printf("CRYPTO_TEST %s FAIL step=%s overrun\n", hash->name, step);
      return -1;
    }

  checksum = crypto_checksum(digest, hash->digestlen);
  if (checksum != fnv)
    {
      printf("CRYPTO_TEST %s FAIL step=%s checksum=%08" PRIx32
             " expected=%08" PRIx32 "\n",
             hash->name, step, checksum, fnv);
      return -1;
    }

  return 0;
}

/* Hash no data, the message in one call, then in pieces, on one session.
 * Each finish starts a new message on the session. The digest length
 * passed as olen is too short and must be ignored.
 */

static int crypto_check_hash(int cfd, const struct crypto_hash_s *hash,
                             const uint8_t *msg)
{
  uint8_t digest[CRYPTO_MAX_DIGEST + CRYPTO_GUARD_SIZE];
  size_t off = 0;
  uint32_t ses;
  int ret;
  int i;

  ret = crypto_hash_session(cfd, hash->mac, &ses);
  if (ret < 0)
    {
      printf("CRYPTO_TEST %s FAIL session ret=%d\n", hash->name, ret);
      return -1;
    }

  crypto_digest_reset(digest, hash->digestlen);
  ret = crypto_hash_op(cfd, ses, 0, NULL, 0, digest, 1);
  if (crypto_expect_digest(hash, "empty", ret, digest,
                           hash->emptyfnv) < 0)
    {
      goto fail;
    }

  crypto_digest_reset(digest, hash->digestlen);
  ret = crypto_hash_op(cfd, ses, COP_FLAG_UPDATE, msg, CRYPTO_HASH_LEN,
                       NULL, 0);
  if (ret == 0)
    {
      ret = crypto_hash_op(cfd, ses, 0, NULL, 0, digest, 1);
    }

  if (crypto_expect_digest(hash, "one-call", ret, digest, hash->fnv) < 0)
    {
      goto fail;
    }

  crypto_digest_reset(digest, hash->digestlen);
  for (i = 0; i < CRYPTO_MAX_PIECES && ret == 0; i++)
    {
      ret = crypto_hash_op(cfd, ses, COP_FLAG_UPDATE, msg + off,
                           g_hash_pieces[i], NULL, 0);
      off += g_hash_pieces[i];
    }

  if (ret == 0)
    {
      ret = crypto_hash_op(cfd, ses, 0, NULL, 0, digest, 1);
    }

  if (crypto_expect_digest(hash, "split", ret, digest, hash->fnv) < 0)
    {
      goto fail;
    }

  crypto_session_free(cfd, ses);
  printf("CRYPTO_TEST hash=%s PASS digest=%zu checksum=%08" PRIx32 "\n",
         hash->name, hash->digestlen, hash->fnv);
  return 0;

fail:
  crypto_session_free(cfd, ses);
  return -1;
}

static int crypto_check_hash_kat(int cfd, uint32_t mac,
                                 const uint8_t *expect, size_t len)
{
  uint8_t digest[CRYPTO_MAX_DIGEST + CRYPTO_GUARD_SIZE];
  uint32_t ses;
  int ret;

  ret = crypto_hash_session(cfd, mac, &ses);
  if (ret < 0)
    {
      return ret;
    }

  crypto_digest_reset(digest, len);
  ret = crypto_hash_op(cfd, ses, COP_FLAG_UPDATE, (const uint8_t *)"abc",
                       3, NULL, 0);
  if (ret == 0)
    {
      ret = crypto_hash_op(cfd, ses, 0, NULL, 0, digest, CRYPTO_MAX_DIGEST);
    }

  crypto_session_free(cfd, ses);
  if (ret == 0 && (memcmp(digest, expect, len) != 0 ||
                   !crypto_guard_ok(digest, len)))
    {
      ret = -EIO;
    }

  return ret;
}

static int crypto_test_sha(int cfd, uint8_t *buf[])
{
  size_t i;
  int ret;

  ret = crypto_check_hash_kat(cfd, CRYPTO_SHA2_224, g_sha224_abc,
                              sizeof(g_sha224_abc));
  if (ret == 0)
    {
      ret = crypto_check_hash_kat(cfd, CRYPTO_SHA2_384, g_sha384_abc,
                                  sizeof(g_sha384_abc));
    }

  if (ret < 0)
    {
      printf("CRYPTO_TEST sha FAIL kat ret=%d\n", ret);
      return -1;
    }

  printf("CRYPTO_TEST kat=sha224,sha384 PASS\n");

  crypto_fill(buf[0], CRYPTO_HASH_LEN, CRYPTO_DATA_SEED);
  for (i = 0; i < sizeof(g_hashes) / sizeof(g_hashes[0]); i++)
    {
      if (crypto_check_hash(cfd, &g_hashes[i], buf[0]) < 0)
        {
          return -1;
        }
    }

  printf("CRYPTO_TEST RESULT case=sha PASS\n");
  return 0;
}
#endif

/* Stream the cipher and hash sessions at once, one piece of each in turn,
 * so that every call continues from a chaining or hash state saved while
 * the engines served the other sessions.
 */

static int crypto_test_session(int cfd, uint8_t *buf[])
{
  const struct crypto_vector_s *vectors[CRYPTO_NSESSIONS] =
  {
    &g_cbc_bulk, &g_ctr_bulk, &g_xts_bulk
  };

#ifdef CONFIG_BL616CL_CRYPTO_SHA
  const struct crypto_hash_s *hashes[CRYPTO_NHASH_SESSIONS] =
  {
    &g_hashes[2], &g_hashes[3]
  };

  uint8_t digest[CRYPTO_MAX_DIGEST + CRYPTO_GUARD_SIZE];
  uint32_t hses[CRYPTO_NHASH_SESSIONS];
  size_t hoff = 0;
  int hopened = 0;
#endif

  uint8_t key[CRYPTO_MAX_KEY];
  uint32_t ses[CRYPTO_NSESSIONS];
  size_t off[CRYPTO_NSESSIONS];
  int opened = 0;
  int ret = 0;
  int piece;
  int i;

  crypto_fill(buf[0], CRYPTO_MAX_LEN, CRYPTO_DATA_SEED);

#ifdef CONFIG_BL616CL_CRYPTO_SHA
  for (i = 0; i < CRYPTO_NHASH_SESSIONS; i++)
    {
      ret = crypto_hash_session(cfd, hashes[i]->mac, &hses[i]);
      if (ret < 0)
        {
          printf("CRYPTO_TEST session FAIL open=%s ret=%d\n",
                 hashes[i]->name, ret);
          goto out;
        }

      hopened++;
    }
#endif

  for (i = 0; i < CRYPTO_NSESSIONS; i++)
    {
      crypto_fill(key, vectors[i]->keylen, vectors[i]->keyseed);
      ret = crypto_session_new(cfd, vectors[i]->cipher, key,
                               vectors[i]->keylen, &ses[i]);
      if (ret < 0)
        {
          printf("CRYPTO_TEST session FAIL open=%s ret=%d\n",
                 vectors[i]->name, ret);
          goto out;
        }

      crypto_guard(buf[i + 1], vectors[i]->len);
      off[i] = 0;
      opened++;
    }

  for (piece = 0; piece < CRYPTO_MAX_PIECES; piece++)
    {
      for (i = 0; i < CRYPTO_NSESSIONS; i++)
        {
          size_t len = vectors[i]->pieces[piece];

          if (len == 0)
            {
              continue;
            }

          ret = crypto_crypt(cfd, ses[i], COP_ENCRYPT,
                             piece == 0 ? 0 : COP_FLAG_UPDATE,
                             vectors[i]->iv, buf[0] + off[i],
                             buf[i + 1] + off[i], len);
          if (ret < 0)
            {
              printf("CRYPTO_TEST session FAIL %s piece=%d ret=%d\n",
                     vectors[i]->name, piece, ret);
              goto out;
            }

          off[i] += len;
        }

#ifdef CONFIG_BL616CL_CRYPTO_SHA
      for (i = 0; i < CRYPTO_NHASH_SESSIONS; i++)
        {
          ret = crypto_hash_op(cfd, hses[i], COP_FLAG_UPDATE, buf[0] + hoff,
                               g_hash_pieces[piece], NULL, 0);
          if (ret < 0)
            {
              printf("CRYPTO_TEST session FAIL %s piece=%d ret=%d\n",
                     hashes[i]->name, piece, ret);
              goto out;
            }
        }

      hoff += g_hash_pieces[piece];
#endif
    }

  for (i = 0; i < CRYPTO_NSESSIONS; i++)
    {
      ret = crypto_expect(vectors[i], "interleaved", 0, buf[i + 1], NULL);
      if (ret < 0)
        {
          goto out;
        }
    }

#ifdef CONFIG_BL616CL_CRYPTO_SHA
  for (i = 0; i < CRYPTO_NHASH_SESSIONS; i++)
    {
      crypto_digest_reset(digest, hashes[i]->digestlen);
      ret = crypto_hash_op(cfd, hses[i], 0, NULL, 0, digest,
                           hashes[i]->digestlen);
      ret = crypto_expect_digest(hashes[i], "interleaved", ret, digest,
                                 hashes[i]->fnv);
      if (ret < 0)
        {
          goto out;
        }
    }

  printf("CRYPTO_TEST RESULT case=session PASS sessions=%d\n",
         CRYPTO_NSESSIONS + CRYPTO_NHASH_SESSIONS);
#else
  printf("CRYPTO_TEST RESULT case=session PASS sessions=%d\n",
         CRYPTO_NSESSIONS);
#endif

out:
  for (i = 0; i < opened; i++)
    {
      crypto_session_free(cfd, ses[i]);
    }

#ifdef CONFIG_BL616CL_CRYPTO_SHA
  for (i = 0; i < hopened; i++)
    {
      crypto_session_free(cfd, hses[i]);
    }
#endif

  return ret < 0 ? -1 : 0;
}

static int crypto_check_zero(int cfd, uint32_t cipher, size_t keylen,
                             uint8_t *buf[])
{
  uint8_t key[CRYPTO_MAX_KEY];
  uint32_t ses;
  int ret;

  crypto_fill(key, keylen, 0x30);
  ret = crypto_session_new(cfd, cipher, key, keylen, &ses);
  if (ret < 0)
    {
      printf("CRYPTO_TEST edge FAIL zero cipher=%" PRIu32 " session=%d\n",
             cipher, ret);
      return -1;
    }

  crypto_guard(buf[1], 0);
  ret = crypto_crypt(cfd, ses, COP_ENCRYPT, 0, g_nist_iv, buf[0], buf[1],
                     0);
  crypto_session_free(cfd, ses);
  if (ret < 0 || !crypto_guard_ok(buf[1], 0))
    {
      printf("CRYPTO_TEST edge FAIL zero cipher=%" PRIu32 " ret=%d\n",
             cipher, ret);
      return -1;
    }

  return 0;
}

static int crypto_check_badlen(int cfd, uint32_t cipher, size_t keylen,
                               size_t len, uint8_t *buf[])
{
  uint8_t key[CRYPTO_MAX_KEY];
  uint32_t ses;
  int ret;

  crypto_fill(key, keylen, 0x30);
  ret = crypto_session_new(cfd, cipher, key, keylen, &ses);
  if (ret < 0)
    {
      printf("CRYPTO_TEST edge FAIL badlen cipher=%" PRIu32 " session=%d\n",
             cipher, ret);
      return -1;
    }

  ret = crypto_crypt(cfd, ses, COP_ENCRYPT, 0, g_nist_iv, buf[0], buf[1],
                     len);
  crypto_session_free(cfd, ses);
  if (ret != -EINVAL)
    {
      printf("CRYPTO_TEST edge FAIL badlen cipher=%" PRIu32
             " len=%zu ret=%d\n", cipher, len, ret);
      return -1;
    }

  return 0;
}

static int crypto_test_edge(int cfd, uint8_t *buf[])
{
  const struct crypto_kat_s *kat = &g_kats[1];

  if (crypto_check_zero(cfd, CRYPTO_AES_CBC, 16, buf) < 0 ||
      crypto_check_zero(cfd, CRYPTO_AES_CTR, 16, buf) < 0 ||
      crypto_check_zero(cfd, CRYPTO_AES_XTS, 32, buf) < 0)
    {
      return -1;
    }

  printf("CRYPTO_TEST edge zero-length PASS\n");

  if (crypto_check_badlen(cfd, CRYPTO_AES_CBC, 16, 15, buf) < 0 ||
      crypto_check_badlen(cfd, CRYPTO_AES_XTS, 32, 20, buf) < 0)
    {
      return -1;
    }

  printf("CRYPTO_TEST edge partial-block PASS\n");

  if (crypto_check_kat(cfd, kat, buf[0] + 1, buf[1] + 3, buf[2] + 1) < 0 ||
      crypto_check_kat(cfd, kat, buf[0] + 2, buf[0] + 2, buf[2] + 3) < 0)
    {
      return -1;
    }

  printf("CRYPTO_TEST edge unaligned PASS\n");
  printf("CRYPTO_TEST RESULT case=edge PASS\n");
  return 0;
}

/* Direct path: the driver may run a request on the caller's buffers when
 * it is large enough, the input is word aligned and the output covers
 * whole cache lines; otherwise it copies through its bounce buffer. Both
 * paths must give the same result, and neither may touch bytes next to
 * the output. The reference is computed from byte-offset buffers, which
 * always use the bounce buffer.
 */

static const size_t g_direct_lens[] =
{
  224, 240, 256, 272, 288, CRYPTO_DIRECT_MAX
};

static int crypto_direct_cbc(int cfd, uint32_t ses, const uint8_t *src,
                             uint8_t *dst, size_t len)
{
  return crypto_crypt(cfd, ses, COP_ENCRYPT, 0, g_nist_iv, src, dst, len);
}

/* Encrypt len bytes of the pattern at src into dst, compare with ref and
 * the guard after dst, then decrypt dst in place back to the pattern.
 */

static int crypto_direct_check(int cfd, uint32_t ses, const char *kind,
                               uint8_t *src, uint8_t *dst, size_t len,
                               const uint8_t *ref, const uint8_t *plain)
{
  int ret;

  memcpy(src, plain, len);
  crypto_guard(dst, len);
  ret = crypto_direct_cbc(cfd, ses, src, dst, len);
  if (ret < 0 || memcmp(dst, ref, len) != 0 || !crypto_guard_ok(dst, len))
    {
      printf("CRYPTO_TEST direct FAIL cbc kind=%s len=%zu ret=%d\n",
             kind, len, ret);
      return -1;
    }

  ret = crypto_crypt(cfd, ses, COP_DECRYPT, 0, g_nist_iv, dst, dst, len);
  if (ret < 0 || memcmp(dst, plain, len) != 0 ||
      !crypto_guard_ok(dst, len))
    {
      printf("CRYPTO_TEST direct FAIL cbc-decrypt kind=%s len=%zu ret=%d\n",
             kind, len, ret);
      return -1;
    }

  return 0;
}

#ifdef CONFIG_BL616CL_CRYPTO_SHA
static int crypto_direct_sha(int cfd, const uint8_t *msg, size_t len,
                             uint8_t *digest)
{
  uint32_t ses;
  int ret;

  ret = crypto_hash_session(cfd, CRYPTO_SHA2_256, &ses);
  if (ret < 0)
    {
      return ret;
    }

  memset(digest, CRYPTO_CANARY, 32);
  ret = crypto_hash_op(cfd, ses, COP_FLAG_UPDATE, msg, len, NULL, 0);
  if (ret == 0)
    {
      ret = crypto_hash_op(cfd, ses, 0, NULL, 0, digest, 32);
    }

  crypto_session_free(cfd, ses);
  return ret;
}

static int crypto_direct_sha_check(int cfd, const char *kind,
                                   const uint8_t *msg, size_t len,
                                   const uint8_t *ref)
{
  uint8_t digest[32];
  int ret;

  ret = crypto_direct_sha(cfd, msg, len, digest);
  if (ret < 0 || memcmp(digest, ref, sizeof(digest)) != 0)
    {
      printf("CRYPTO_TEST direct FAIL sha256 kind=%s len=%zu ret=%d\n",
             kind, len, ret);
      return -1;
    }

  return 0;
}
#endif

static int crypto_direct_lengths(int cfd, uint8_t *a, uint8_t *b,
                                 uint8_t *plain, uint8_t *ref)
{
  aligned_data(CRYPTO_LINE_SIZE) uint8_t stacksrc[CRYPTO_DIRECT_STACK];
  aligned_data(CRYPTO_LINE_SIZE)
    uint8_t stackdst[CRYPTO_DIRECT_STACK + CRYPTO_GUARD_SIZE];
#ifdef CONFIG_BL616CL_CRYPTO_SHA
  uint8_t refdigest[32];
#endif
  uint32_t ses;
  size_t len;
  size_t i;
  int ret;

  ret = crypto_session_new(cfd, CRYPTO_AES_256_CBC, g_nist_key256,
                           sizeof(g_nist_key256), &ses);
  if (ret < 0)
    {
      printf("CRYPTO_TEST direct FAIL session ret=%d\n", ret);
      return -1;
    }

  for (i = 0; i < sizeof(g_direct_lens) / sizeof(g_direct_lens[0]); i++)
    {
      len = g_direct_lens[i];

      /* Reference through the bounce buffer: byte-offset buffers. */

      memcpy(a + 1, plain, len);
      ret = crypto_direct_cbc(cfd, ses, a + 1, b + 1, len);
      if (ret < 0)
        {
          printf("CRYPTO_TEST direct FAIL ref len=%zu ret=%d\n", len, ret);
          goto fail;
        }

      memcpy(ref, b + 1, len);

      if (crypto_direct_check(cfd, ses, "aligned", a, b, len, ref,
                              plain) < 0 ||
          crypto_direct_check(cfd, ses, "inplace", b, b, len, ref,
                              plain) < 0 ||
          crypto_direct_check(cfd, ses, "word", a + 4, b, len, ref,
                              plain) < 0 ||
          crypto_direct_check(cfd, ses, "line-offset", a, b + 16, len, ref,
                              plain) < 0)
        {
          goto fail;
        }

      if (len <= CRYPTO_DIRECT_STACK &&
          crypto_direct_check(cfd, ses, "stack", stacksrc, stackdst, len,
                              ref, plain) < 0)
        {
          goto fail;
        }

#ifdef CONFIG_BL616CL_CRYPTO_SHA
      memcpy(a + 1, plain, len);
      if (crypto_direct_sha(cfd, a + 1, len, refdigest) < 0)
        {
          printf("CRYPTO_TEST direct FAIL sha256 ref len=%zu\n", len);
          goto fail;
        }

      memcpy(b, plain, len);
      memcpy(a + 4, plain, len);
      memcpy(stacksrc, plain, MIN(len, CRYPTO_DIRECT_STACK));
      if (crypto_direct_sha_check(cfd, "aligned", b, len, refdigest) < 0 ||
          crypto_direct_sha_check(cfd, "word", a + 4, len, refdigest) < 0 ||
          (len <= CRYPTO_DIRECT_STACK &&
           crypto_direct_sha_check(cfd, "stack", stacksrc, len,
                                   refdigest) < 0))
        {
          goto fail;
        }
#endif

      printf("CRYPTO_TEST direct len=%zu PASS\n", len);
    }

  crypto_session_free(cfd, ses);
  return 0;

fail:
  crypto_session_free(cfd, ses);
  return -1;
}

/* Time CBC encryption of CRYPTO_DIRECT_MAX bytes from aligned buffers
 * (direct path when enabled) and from byte-offset ones (bounce buffer).
 */

static void crypto_direct_perf(int cfd, uint8_t *a, uint8_t *b)
{
  struct timespec start;
  struct timespec end;
  uint32_t ses;
  long us[2];
  int pass;
  int i;

  if (crypto_session_new(cfd, CRYPTO_AES_CBC, g_nist_key256, 16,
                         &ses) < 0)
    {
      return;
    }

  for (pass = 0; pass < 2; pass++)
    {
      clock_gettime(CLOCK_MONOTONIC, &start);
      for (i = 0; i < CRYPTO_PERF_LOOPS; i++)
        {
          crypto_direct_cbc(cfd, ses, a + pass, b + pass, CRYPTO_DIRECT_MAX);
        }

      clock_gettime(CLOCK_MONOTONIC, &end);
      us[pass] = (end.tv_sec - start.tv_sec) * 1000000 +
                 (end.tv_nsec - start.tv_nsec) / 1000;
    }

  crypto_session_free(cfd, ses);
  printf("CRYPTO_TEST direct perf cbc len=%d aligned_us=%ld "
         "offset_us=%ld loops=%d\n",
         CRYPTO_DIRECT_MAX, us[0], us[1], CRYPTO_PERF_LOOPS);
}

/* Workers 0 and 1 encrypt and decrypt with CBC and CTR, worker 2 hashes,
 * worker 3 reads the TRNG, all at once, each with its own descriptor.
 */

static void *crypto_worker(void *arg)
{
  struct crypto_worker_s *worker = arg;
  uint8_t digest[32];
  uint8_t *in = NULL;
  uint8_t *out = NULL;
  uint32_t ses = 0;
  int cfd;
  int fd;
  int i;

  worker->result = -1;
  if (worker->id == 3)
    {
      fd = open("/dev/random", O_RDONLY);
      if (fd < 0)
        {
          return NULL;
        }

      for (i = 0; i < CRYPTO_WORKER_LOOPS * 4; i++)
        {
          if (read(fd, digest, sizeof(digest)) != sizeof(digest))
            {
              close(fd);
              return NULL;
            }
        }

      close(fd);
      worker->result = 0;
      return NULL;
    }

  cfd = crypto_open();
  in = memalign(CRYPTO_LINE_SIZE, CRYPTO_DIRECT_MAX);
  out = memalign(CRYPTO_LINE_SIZE, CRYPTO_DIRECT_MAX);
  if (cfd < 0 || in == NULL || out == NULL)
    {
      goto out;
    }

  crypto_fill(in, CRYPTO_DIRECT_MAX, CRYPTO_DATA_SEED);

#ifdef CONFIG_BL616CL_CRYPTO_SHA
  if (worker->id == 2)
    {
      for (i = 0; i < CRYPTO_WORKER_LOOPS; i++)
        {
          if (crypto_direct_sha(cfd, in, CRYPTO_DIRECT_MAX, digest) < 0 ||
              memcmp(digest, worker->expect, sizeof(digest)) != 0)
            {
              goto out;
            }
        }

      worker->result = 0;
      goto out;
    }
#endif

  if (crypto_session_new(cfd, worker->id == 0 ? CRYPTO_AES_CBC :
                         CRYPTO_AES_CTR, g_nist_key256, 32, &ses) < 0)
    {
      goto out;
    }

  for (i = 0; i < CRYPTO_WORKER_LOOPS; i++)
    {
      if (crypto_crypt(cfd, ses, COP_ENCRYPT, 0, g_nist_iv, in, out,
                       CRYPTO_DIRECT_MAX) < 0 ||
          memcmp(out, worker->expect, CRYPTO_DIRECT_MAX) != 0 ||
          crypto_crypt(cfd, ses, COP_DECRYPT, 0, g_nist_iv, out, out,
                       CRYPTO_DIRECT_MAX) < 0 ||
          memcmp(out, in, CRYPTO_DIRECT_MAX) != 0)
        {
          crypto_session_free(cfd, ses);
          goto out;
        }
    }

  crypto_session_free(cfd, ses);
  worker->result = 0;

out:
  free(in);
  free(out);
  if (cfd >= 0)
    {
      close(cfd);
    }

  return NULL;
}

static int crypto_direct_workers(int cfd, uint8_t *a, uint8_t *b)
{
  struct crypto_worker_s workers[CRYPTO_NWORKERS];
  pthread_t threads[CRYPTO_NWORKERS];
  uint8_t *expect[2];
#ifdef CONFIG_BL616CL_CRYPTO_SHA
  uint8_t digest[32];
  uint8_t *msg;
#endif
  uint32_t ses;
  int created = 0;
  int ret = 0;
  int i;

  /* Reference outputs, computed single-threaded through the bounce
   * buffer.
   */

  expect[0] = a;
  expect[1] = b;
  for (i = 0; i < 2; i++)
    {
      crypto_fill(expect[i] + 1, CRYPTO_DIRECT_MAX, CRYPTO_DATA_SEED);
      if (crypto_session_new(cfd, i == 0 ? CRYPTO_AES_CBC : CRYPTO_AES_CTR,
                             g_nist_key256, 32, &ses) < 0)
        {
          return -1;
        }

      ret = crypto_crypt(cfd, ses, COP_ENCRYPT, 0, g_nist_iv,
                         expect[i] + 1, expect[i] + 1, CRYPTO_DIRECT_MAX);
      crypto_session_free(cfd, ses);
      if (ret < 0)
        {
          return -1;
        }

      memmove(expect[i], expect[i] + 1, CRYPTO_DIRECT_MAX);
    }

#ifdef CONFIG_BL616CL_CRYPTO_SHA
  msg = malloc(CRYPTO_DIRECT_MAX + 1);
  if (msg == NULL)
    {
      return -1;
    }

  crypto_fill(msg + 1, CRYPTO_DIRECT_MAX, CRYPTO_DATA_SEED);
  ret = crypto_direct_sha(cfd, msg + 1, CRYPTO_DIRECT_MAX, digest);
  free(msg);
  if (ret < 0)
    {
      return -1;
    }
#endif

  memset(workers, 0, sizeof(workers));
  for (i = 0; i < CRYPTO_NWORKERS; i++)
    {
      workers[i].id = i;
#ifdef CONFIG_BL616CL_CRYPTO_SHA
      workers[i].expect = i < 2 ? expect[i] : digest;
#else
      workers[i].expect = i < 2 ? expect[i] : NULL;
      if (i == 2)
        {
          workers[i].result = 0;
          continue;
        }
#endif

      ret = pthread_create(&threads[i], NULL, crypto_worker, &workers[i]);
      if (ret != 0)
        {
          printf("CRYPTO_TEST direct FAIL create=%d error=%d\n", i, ret);
          break;
        }

      created |= 1 << i;
    }

  ret = i < CRYPTO_NWORKERS ? -1 : 0;
  for (i = 0; i < CRYPTO_NWORKERS; i++)
    {
      if ((created & (1 << i)) != 0)
        {
          pthread_join(threads[i], NULL);
        }

      if (workers[i].result != 0)
        {
          printf("CRYPTO_TEST direct FAIL worker=%d\n", i);
          ret = -1;
        }
    }

  if (ret == 0)
    {
      printf("CRYPTO_TEST direct workers PASS threads=%d loops=%d\n",
             CRYPTO_NWORKERS, CRYPTO_WORKER_LOOPS);
    }

  return ret;
}

static int crypto_test_direct(int cfd, uint8_t *buf[])
{
  uint8_t *a;
  uint8_t *b;
  int ret = -1;

  a = memalign(CRYPTO_LINE_SIZE, CRYPTO_DIRECT_BUF);
  b = memalign(CRYPTO_LINE_SIZE, CRYPTO_DIRECT_BUF);
  if (a == NULL || b == NULL)
    {
      printf("CRYPTO_TEST direct FAIL memalign\n");
      goto out;
    }

  crypto_fill(buf[0], CRYPTO_DIRECT_MAX, CRYPTO_DATA_SEED);
  if (crypto_direct_lengths(cfd, a, b, buf[0], buf[1]) < 0 ||
      crypto_direct_workers(cfd, a, b) < 0)
    {
      goto out;
    }

  crypto_direct_perf(cfd, a, b);
  printf("CRYPTO_TEST RESULT case=direct PASS\n");
  ret = 0;

out:
  free(a);
  free(b);
  return ret;
}

static int crypto_run_case(const char *name, int cfd, uint8_t *buf[])
{
  if (strcmp(name, "cbc") == 0)
    {
      return crypto_test_cbc(cfd, buf);
    }

  if (strcmp(name, "ctr") == 0)
    {
      return crypto_test_ctr(cfd, buf);
    }

  if (strcmp(name, "xts") == 0)
    {
      return crypto_test_xts(cfd, buf);
    }

#ifdef CONFIG_BL616CL_CRYPTO_SHA
  if (strcmp(name, "sha") == 0)
    {
      return crypto_test_sha(cfd, buf);
    }
#endif

  if (strcmp(name, "session") == 0)
    {
      return crypto_test_session(cfd, buf);
    }

  if (strcmp(name, "edge") == 0)
    {
      return crypto_test_edge(cfd, buf);
    }

  if (strcmp(name, "direct") == 0)
    {
      return crypto_test_direct(cfd, buf);
    }

  return -EINVAL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  static const char *cases[] =
  {
    "cbc", "ctr", "xts",
#ifdef CONFIG_BL616CL_CRYPTO_SHA
    "sha",
#endif
    "session", "edge", "direct"
  };

  uint8_t *buf[CRYPTO_NBUFS];
  int cfd;
  int ret = 0;
  size_t i;

  if (argc != 2)
    {
      crypto_usage(argv[0]);
      return EXIT_FAILURE;
    }

  printf("CRYPTO_TEST BEGIN case=%s\n", argv[1]);

  memset(buf, 0, sizeof(buf));
  for (i = 0; i < CRYPTO_NBUFS; i++)
    {
      buf[i] = malloc(CRYPTO_BUF_SIZE);
      if (buf[i] == NULL)
        {
          printf("CRYPTO_TEST FAIL malloc size=%d\n", CRYPTO_BUF_SIZE);
          ret = -1;
          goto out;
        }
    }

  cfd = crypto_open();
  if (cfd < 0)
    {
      ret = -1;
      goto out;
    }

  if (strcmp(argv[1], "all") != 0)
    {
      ret = crypto_run_case(argv[1], cfd, buf);
      if (ret == -EINVAL)
        {
          crypto_usage(argv[0]);
        }
    }
  else
    {
      for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        {
          if (crypto_run_case(cases[i], cfd, buf) < 0)
            {
              ret = -1;
            }
        }

      printf("CRYPTO_TEST RESULT case=all %s\n", ret == 0 ? "PASS" : "FAIL");
    }

  close(cfd);

out:
  for (i = 0; i < CRYPTO_NBUFS; i++)
    {
      free(buf[i]);
    }

  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
