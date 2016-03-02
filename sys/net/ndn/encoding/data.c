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

#include "net/ndn/encoding/data.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

ndn_shared_block_t* ndn_data_create(ndn_name_t* name,
				    ndn_metainfo_t* metainfo,
				    ndn_block_t* content,
				    const unsigned char* hmac_key,
				    size_t hmac_key_len)
{
    if (name == NULL || metainfo == NULL || content == NULL)
	return NULL;

    if (content->buf == NULL || content->len < 0)
	return NULL;

    if (hmac_key != NULL && hmac_key_len <= 0)
	return NULL;

    int nl = ndn_name_total_length(name);
    if (nl <= 0) return NULL;

    int ml = ndn_metainfo_total_length(metainfo);
    if (ml <= 0) return NULL;

    int cl = ndn_block_total_length(NDN_TLV_CONTENT, content->len);

    int dl = nl + ml + cl + 39;
    if (dl > 253) return NULL;  //TODO: support multi-byte length field

    ndn_block_t data;
    data.len = dl + 2;
    uint8_t* buf = (uint8_t*)malloc(data.len);
    if (buf == NULL) {
	DEBUG("ndn_encoding: cannot allocate memory for data block\n");
	return NULL;
    }
    data.buf = buf;

    // Write data type and length
    buf[0] = NDN_TLV_DATA;
    buf[1] = dl;

    // Write name
    ndn_name_wire_encode(name, buf + 2, nl);
    buf += nl + 2;

    // Write metainfo
    ndn_metainfo_wire_encode(metainfo, buf, ml);
    buf += ml;

    // Write content
    buf[0] = NDN_TLV_CONTENT;
    buf[1] = content->len;
    memcpy(buf + 2, content->buf, content->len);
    buf += content->len + 2;

    // Write signature info
    buf[0] = NDN_TLV_SIGNATURE_INFO;
    buf[1] = 3;
    buf[2] = NDN_TLV_SIGNATURE_TYPE;
    buf[3] = 1;
    if (hmac_key == NULL)
	buf[4] = NDN_SIG_TYPE_DIGEST_SHA256;
    else
	buf[4] = NDN_SIG_TYPE_HMAC_SHA256;
    buf += 5;
    //TODO: support keylocator for HMAC signature

    // Write signature value
    buf[0] = NDN_TLV_SIGNATURE_VALUE;
    buf[1] = 32;
    if (hmac_key == NULL)
	sha256(data.buf + 2, dl - 34, buf + 2);
    else
	hmac_sha256(hmac_key, hmac_key_len,
		    (const unsigned*)(data.buf + 2), dl - 34, buf + 2);

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
	algorithm != NDN_SIG_TYPE_HMAC_SHA256) {
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

    if (num != 32) {
	DEBUG("ndn_encoding: invalid signature value length (%u)\n", num);
	return -1;
    }

    uint8_t sig[32];
    memset(sig, 0, 32);
    /* verify signature */
    switch (algorithm) {
	case NDN_SIG_TYPE_DIGEST_SHA256:
	    sha256(sig_start, sig_value.buf - sig_start, sig);
	    if (memcmp(sig, sig_value.buf + 2, sizeof(sig)) != 0) {
		DEBUG("ndn_encoding: failed to verify DigestSha256 signature\n");
		return -1;
	    }
	    else
		return 0;

	case NDN_SIG_TYPE_HMAC_SHA256:
	    if (key == NULL || key_len <= 0) {
		DEBUG("ndn_encoding: no hmac key, cannot verify signature\n");
		return -1;
	    }
	    hmac_sha256(key, key_len, (const unsigned*)sig_start,
			sig_value.buf - sig_start, sig);
	    if (memcmp(sig, sig_value.buf + 2, sizeof(sig)) != 0) {
		DEBUG("ndn_encoding: failed to verify HMAC_SHA256 signature\n");
		return -1;
	    }
	    else
		return 0;

	default:
	    break;
    }
    return -1; // never reach here
}
