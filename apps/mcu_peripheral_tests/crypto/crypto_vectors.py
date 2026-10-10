#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# Print the expected checksums used by crypto_test_main.c. The ciphertext is
# computed with an independent AES implementation (Python "cryptography")
# over the same deterministic inputs as the on-board test.

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def fill(seed, length):
    return bytes((seed + i * 13 + (i >> 7)) & 0xff for i in range(length))


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def encrypt(algo, mode, data):
    enc = Cipher(algo, mode).encryptor()
    return enc.update(data) + enc.finalize()


CBC_IV = bytes.fromhex("a0a1a2a3a4a5a6a7a8a9aaabacadaeaf")
CTR_IV_WORD = bytes.fromhex("f0f1f2f3f4f5f6f7f8f9fafbfffffffe")
CTR_IV_ALL = bytes.fromhex("fffffffffffffffffffffffffffffffe")
CTR_IV_BULK = bytes.fromhex("000102030405060708090a0bffffff10")


def xts_iv(sector):
    return sector.to_bytes(16, "little")


CASES = [
    ("CBC_BULK", algorithms.AES(fill(0x30, 16)),
     modes.CBC(CBC_IV), 2064),
    ("CTR_WORD", algorithms.AES(fill(0x30, 16)), modes.CTR(CTR_IV_WORD), 69),
    ("CTR_ALL", algorithms.AES(fill(0x30, 16)), modes.CTR(CTR_IV_ALL), 69),
    ("CTR_BULK", algorithms.AES(fill(0x91, 32)),
     modes.CTR(CTR_IV_BULK), 5000),
    ("XTS_BULK", algorithms.AES(fill(0x30, 64)),
     modes.XTS(xts_iv(0x1234)), 4096),
    ("XTS_128", algorithms.AES(fill(0x91, 32)), modes.XTS(xts_iv(7)), 1040),
]

for name, algo, mode, length in CASES:
    ct = encrypt(algo, mode, fill(0x5b, length))
    print("#define CRYPTO_%s_LEN %d" % (name, length))
    print("#define CRYPTO_%s_FNV UINT32_C(0x%08x)" % (name, fnv1a(ct)))
