/*
 * Copyright (C) 2016 Wentao Shang
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     net_ndn_encoding
 * @{
 *
 * @file
 *
 * @author  Wentao Shang <wentaoshang@gmail.com>
 */
#include <stdlib.h>
#include <string.h>

#include "hashes/sha256.h"
#include "net/gnrc/nettype.h"
#include "uECC.h"

#include "net/ndn/encoding/data.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

ndn_shared_block_t* ndn_data_create(ndn_block_t* name,
				    ndn_metainfo_t* metainfo,
				    ndn_block_t* content,
				    uint8_t sig_type,
				    const unsigned char* key,
				    size_t key_len)
{
    if (name == NULL || name->buf == NULL || name->len <= 0 ||
	metainfo == NULL || content == NULL || content->buf == NULL ||
	content->len < 0)
	return NULL;

    if (sig_type != NDN_SIG_TYPE_DIGEST_SHA256 &&
	sig_type != NDN_SIG_TYPE_ECDSA_SHA256 &&
	sig_type != NDN_SIG_TYPE_HMAC_SHA256)
	return NULL;

    if (sig_type != NDN_SIG_TYPE_DIGEST_SHA256 && key == NULL)
	return NULL;

    if (sig_type == NDN_SIG_TYPE_ECDSA_SHA256 && key_len != 32)
	return NULL;

    if (key != NULL && key_len <= 0)
	return NULL;

    int ml = ndn_metainfo_total_length(metainfo);
    if (ml <= 0) return NULL;

    int cl = ndn_block_total_length(NDN_TLV_CONTENT, content->len);

    int dl = name->len + ml + cl + 39;
    if (sig_type == NDN_SIG_TYPE_ECDSA_SHA256)
	dl += 32;  // ecc p256 signature length is 64 bytes

    ndn_block_t data;
    data.len = ndn_block_total_length(NDN_TLV_DATA, dl);
    uint8_t* buf = (uint8_t*)malloc(data.len);
    if (buf == NULL) {
	DEBUG("ndn_encoding: cannot allocate memory for data block\n");
	return NULL;
    }
    data.buf = buf;

    // Write data type and length
    buf[0] = NDN_TLV_DATA;
    int l = ndn_block_put_var_number(dl, buf + 1, data.len - 1);
    buf += l + 1;
    assert(data.len == dl + 1 + l);

    // Write name
    memcpy(buf, name->buf, name->len);
    buf += name->len;

    // Write metainfo
    ndn_metainfo_wire_encode(metainfo, buf, ml);
    buf += ml;

    // Write content
    buf[0] = NDN_TLV_CONTENT;
    l = ndn_block_put_var_number(content->len, buf + 1, dl - name->len - ml);
    buf += l + 1;
    memcpy(buf, content->buf, content->len);
    buf += content->len;

    // Write signature info
    buf[0] = NDN_TLV_SIGNATURE_INFO;
    buf[1] = 3;
    buf[2] = NDN_TLV_SIGNATURE_TYPE;
    buf[3] = 1;
    buf[4] = sig_type;
    buf += 5;
    //TODO: support keylocator

    // Write signature value
    buf[0] = NDN_TLV_SIGNATURE_VALUE;

    switch (sig_type) {
	case NDN_SIG_TYPE_DIGEST_SHA256:
	    buf[1] = 32;
	    sha256(data.buf + 2, dl - 34, buf + 2);
	    break;

	case NDN_SIG_TYPE_HMAC_SHA256:
	    buf[1] = 32;
	    hmac_sha256(key, key_len, (const unsigned*)(data.buf + 2),
			dl - 34, buf + 2);
	    break;

	case NDN_SIG_TYPE_ECDSA_SHA256:
	{
	    buf[1] = 64;
	    uint8_t h[32] = {0};
	    sha256(data.buf + 2, dl - 66, h);
	    uECC_Curve curve = uECC_secp256r1();
	    if (uECC_sign(key, h, sizeof(h), buf + 2, curve) == 0) {
		free(buf);
		return NULL;
	    }
	}
	break;

	default:
	    break;
    }

    ndn_shared_block_t* sd = ndn_shared_block_create_by_move(&data);
    if (sd == NULL) {
	free((void*)data.buf);
	return NULL;
    }
    return sd;
}

ndn_shared_block_t* ndn_data_create2(ndn_name_t* name,
				     ndn_metainfo_t* metainfo,
				     ndn_block_t* content,
				     uint8_t sig_type,
				     const unsigned char* key,
				     size_t key_len)
{
    if (name == NULL || metainfo == NULL || content == NULL)
	return NULL;

    if (content->buf == NULL || content->len < 0)
	return NULL;


    if (sig_type != NDN_SIG_TYPE_DIGEST_SHA256 &&
	sig_type != NDN_SIG_TYPE_ECDSA_SHA256 &&
	sig_type != NDN_SIG_TYPE_HMAC_SHA256)
	return NULL;

    if (sig_type != NDN_SIG_TYPE_DIGEST_SHA256 && key == NULL)
	return NULL;

    if (sig_type == NDN_SIG_TYPE_ECDSA_SHA256 && key_len != 32)
	return NULL;

    if (key != NULL && key_len <= 0)
	return NULL;

    int nl = ndn_name_total_length(name);
    if (nl <= 0) return NULL;

    int ml = ndn_metainfo_total_length(metainfo);
    if (ml <= 0) return NULL;

    int cl = ndn_block_total_length(NDN_TLV_CONTENT, content->len);

    int dl = nl + ml + cl + 39;
    if (sig_type == NDN_SIG_TYPE_ECDSA_SHA256)
	dl += 32;  // ecc p256 signature length is 64 bytes

    ndn_block_t data;
    data.len = ndn_block_total_length(NDN_TLV_DATA, dl);
    uint8_t* buf = (uint8_t*)malloc(data.len);
    if (buf == NULL) {
	DEBUG("ndn_encoding: cannot allocate memory for data block\n");
	return NULL;
    }
    data.buf = buf;

    // Write data type and length
    buf[0] = NDN_TLV_DATA;
    int l = ndn_block_put_var_number(dl, buf + 1, data.len - 1);
    buf += l + 1;
    assert(data.len == dl + 1 + l);

    // Write name
    ndn_name_wire_encode(name, buf, nl);
    buf += nl;

    // Write metainfo
    ndn_metainfo_wire_encode(metainfo, buf, ml);
    buf += ml;

    // Write content
    buf[0] = NDN_TLV_CONTENT;
    l = ndn_block_put_var_number(content->len, buf + 1, dl - nl - ml);
    buf += l + 1;
    memcpy(buf, content->buf, content->len);
    buf += content->len;

    // Write signature info
    buf[0] = NDN_TLV_SIGNATURE_INFO;
    buf[1] = 3;
    buf[2] = NDN_TLV_SIGNATURE_TYPE;
    buf[3] = 1;
    buf[4] = sig_type;
    buf += 5;
    //TODO: support keylocator for HMAC signature

    // Write signature value
    buf[0] = NDN_TLV_SIGNATURE_VALUE;

    switch (sig_type) {
	case NDN_SIG_TYPE_DIGEST_SHA256:
	    buf[1] = 32;
	    sha256(data.buf + 2, dl - 34, buf + 2);
	    break;

	case NDN_SIG_TYPE_HMAC_SHA256:
	    buf[1] = 32;
	    hmac_sha256(key, key_len, (const unsigned*)(data.buf + 2),
			dl - 34, buf + 2);
	    break;

	case NDN_SIG_TYPE_ECDSA_SHA256:
	{
	    buf[1] = 64;
	    uint8_t h[32] = {0};
	    sha256(data.buf + 2, dl - 66, h);
	    uECC_Curve curve = uECC_secp256r1();
	    if (uECC_sign(key, h, sizeof(h), buf + 2, curve) == 0) {
		free(buf);
		return NULL;
	    }
	}
	break;

	default:
	    break;
    }

    ndn_shared_block_t* sd = ndn_shared_block_create_by_move(&data);
    if (sd == NULL) {
	free((void*)data.buf);
	return NULL;
    }
    return sd;
}

int ndn_data_get_name(ndn_block_t* block, ndn_block_t* name)
{
    if (name == NULL || block == NULL) return -1;

    const uint8_t* buf = block->buf;
    int len = block->len;
    uint32_t num;
    int l;

    /* read data type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_DATA) return -1;
    buf += l;
    len -= l;

    /* read data length */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l;
    len -= l;

    if ((int)num > len) return -1;  // incomplete packet

    /* read name type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_NAME) return -1;
    buf += l;
    len -= l;

    /* read name length */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;

    if ((int)num > len - l)  // name block is incomplete
	return -1;

    name->buf = buf - 1;
    name->len = (int)num + l + 1;
    return 0;
}

int ndn_data_get_metainfo(ndn_block_t* block, ndn_metainfo_t* meta)
{
    if (block == NULL || meta == NULL) return -1;

    const uint8_t* buf = block->buf;
    int len = block->len;
    uint32_t num;
    int l;

    /* read data type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_DATA) return -1;
    buf += l;
    len -= l;

    /* read data length */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l;
    len -= l;

    if ((int)num > len) return -1;  // incomplete packet

    /* read name type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_NAME) return -1;
    buf += l;
    len -= l;

    /* read name length and skip value */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l + (int)num;
    len -= l + (int)num;

    if (ndn_metainfo_from_block(buf, len, meta) <= 0) return -1;
    else return 0;
}

int ndn_data_get_content(ndn_block_t* block, ndn_block_t* content)
{
    if (block == NULL || content == NULL) return -1;

    const uint8_t* buf = block->buf;
    int len = block->len;
    uint32_t num;
    int l;

    /* read data type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_DATA) return -1;
    buf += l;
    len -= l;

    /* read data length */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l;
    len -= l;

    if ((int)num > len) return -1;  // incomplete packet

    /* read name type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_NAME) return -1;
    buf += l;
    len -= l;

    /* read name length and skip value */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l + (int)num;
    len -= l + (int)num;

    /* read metainfo type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_METAINFO) return -1;
    buf += l;
    len -= l;

    /* read metainfo length and skip value */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l + (int)num;
    len -= l + (int)num;

    /* read content type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_CONTENT) return -1;
    buf += l;
    len -= l;

    /* read content length */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;

    if ((int)num > len - l)  // content block is incomplete
	return -1;

    content->buf = buf - 1;
    content->len = (int)num + l + 1;
    return 0;
}

int ndn_data_verify_signature(ndn_block_t* block,
			      const unsigned char* key,
			      size_t key_len)
{
    if (block == NULL) return -1;

    const uint8_t* buf = block->buf;
    int len = block->len;
    uint32_t num;
    int l;
    uint32_t algorithm;

    /* read data type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_DATA) return -1;
    buf += l;
    len -= l;

    /* read data length */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l;
    len -= l;

    if ((int)num > len) return -1;  // incomplete packet

    const uint8_t* sig_start = buf;

    /* read name type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_NAME) return -1;
    buf += l;
    len -= l;

    /* read name length and skip value */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l + (int)num;
    len -= l + (int)num;

    /* read metainfo type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_METAINFO) return -1;
    buf += l;
    len -= l;

    /* read metainfo length and skip value */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l + (int)num;
    len -= l + (int)num;

    /* read content type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_CONTENT) return -1;
    buf += l;
    len -= l;

    /* read content length and skip value */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l + (int)num;
    len -= l + (int)num;

    /* read signature info type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_SIGNATURE_INFO) return -1;
    buf += l;
    len -= l;

    /* read signature info length */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l;
    len -= l;

    ndn_block_t sig_value = { buf + (int)num, len - (int)num };

    /* read signature type type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_SIGNATURE_TYPE) return -1;
    buf += l;
    len -= l;

    /* read signature type length */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l;
    len -= l;

    /* read integer */
    l = ndn_block_get_integer(buf, (int)num, &algorithm);
    if (l < 0) return -1;
    buf += l;
    len -= l;

    if (algorithm != NDN_SIG_TYPE_DIGEST_SHA256 &&
	algorithm != NDN_SIG_TYPE_HMAC_SHA256 &&
	algorithm != NDN_SIG_TYPE_ECDSA_SHA256) {
	DEBUG("ndn_encoding: unknown signature type, cannot verify\n");
	return -1;
    }

    /* read signature value type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_SIGNATURE_VALUE) return -1;
    buf += l;
    len -= l;

    /* read signature value length */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l;
    len -= l;

    /* verify signature */
    switch (algorithm) {
	case NDN_SIG_TYPE_DIGEST_SHA256:
	{
	    if (num != 32) {
		DEBUG("ndn_encoding: invalid digest sig value length (%u)\n",
		      num);
		return -1;
	    }
	    uint8_t h[32] = {0};
	    sha256(sig_start, sig_value.buf - sig_start, h);
	    if (memcmp(h, sig_value.buf + 2, sizeof(h)) != 0) {
		DEBUG("ndn_encoding: fail to verify DigestSha256 signature\n");
		return -1;
	    }
	    else
		return 0;
	}

	case NDN_SIG_TYPE_HMAC_SHA256:
	{
	    if (num != 32) {
		DEBUG("ndn_encoding: invalid hmac sig value length (%u)\n",
		      num);
		return -1;
	    }
	    uint8_t h[32] = {0};
	    if (key == NULL || key_len <= 0) {
		DEBUG("ndn_encoding: no hmac key, cannot verify signature\n");
		return -1;
	    }
	    hmac_sha256(key, key_len, (const unsigned*)sig_start,
			sig_value.buf - sig_start, h);
	    if (memcmp(h, sig_value.buf + 2, sizeof(h)) != 0) {
		DEBUG("ndn_encoding: fail to verify HMAC_SHA256 signature\n");
		return -1;
	    }
	    else
		return 0;
	}

	case NDN_SIG_TYPE_ECDSA_SHA256:
	{
	    if (num != 64) {
		DEBUG("ndn_encoding: invalid ecdsa sig value length (%u)\n",
		      num);
		return -1;
	    }
	    if (key == NULL || key_len != 64) {
		DEBUG("ndn_encoding: invalid ecdsa key\n");
		return -1;
	    }
	    uint8_t h[32] = {0};
	    sha256(sig_start, sig_value.buf - sig_start, h);
	    uECC_Curve curve = uECC_secp256r1();
	    if (uECC_verify(key, h, sizeof(h),
			    sig_value.buf + 2, curve) == 0) {
		DEBUG("ndn_encoding: fail to verify ECDSA_SHA256 signature\n");
		return -1;
	    }
	    else
		return 0;
	}

	default:
	    break;
    }
    return -1; // never reach here
}
