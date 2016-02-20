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

#include "net/gnrc/nettype.h"
#include "net/ndn/encoding/block.h"
#include "net/ndn/encoding/interest.h"
#include "random.h"

#define ENABLE_DEBUG (0)
#include "debug.h"


gnrc_pktsnip_t* ndn_interest_create(ndn_name_t* name, void* selectors, uint32_t lifetime)
{
    if (name == NULL) return NULL;

    (void)selectors;  //TODO: support selectors.

    int name_len = ndn_name_total_length(name);
    if (name_len <= 0) return NULL;

    int lt_len = ndn_block_integer_length(lifetime); // length of the lifetime value

    if (name_len + lt_len + 8 > 253)
	return NULL;  //TODO: support multi-byte length field.

    gnrc_pktsnip_t *pkt = NULL;
    uint8_t* buf = NULL;

    // Create nonce+lifetime snip.
    pkt = gnrc_pktbuf_add(NULL, NULL, 10 + name_len + lt_len, GNRC_NETTYPE_NDN);
    if (pkt == NULL) {
	DEBUG("ndn_encoding: cannot create interest packet snip: unable to allocate packet\n");
        return NULL;
    }
    buf = (uint8_t*)pkt->data;

    // Fill in the Interest header and name field.
    buf[0] = NDN_TLV_INTEREST;
    buf[1] = pkt->size - 2;
    ndn_name_wire_encode(name, buf + 2, name_len);
    
    // Fill in the nonce.
    buf += name_len + 2;
    uint32_t nonce = genrand_uint32();
    buf[0] = NDN_TLV_NONCE;
    buf[1] = 4;  // Nonce field length
    buf[2] = (nonce >> 24) & 0xFF;
    buf[3] = (nonce >> 16) & 0xFF;
    buf[4] = (nonce >> 8) & 0xFF;
    buf[5] = nonce & 0xFF;

    // Fill in the lifetime
    buf[6] = NDN_TLV_INTERESTLIFETIME;
    buf[7] = lt_len;
    ndn_block_put_integer(lifetime, buf + 8, buf[7]);

    return pkt;
}

int ndn_interest_get_block(gnrc_pktsnip_t* pkt, ndn_block_t* block)
{
    if (block == NULL || pkt == NULL || pkt->type != GNRC_NETTYPE_NDN) return -1;

    const uint8_t* buf = (uint8_t*)pkt->data;
    int len = pkt->size;
    uint32_t num;
    int l;

    /* read interest type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_INTEREST) return -1;
    buf += l;
    len -= l;

    /* read interest length and ignore the value */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;

    if ((int)num > len - l)  // Interest packet is incomplete
	return -1;

    block->buf = buf - 1;
    block->len = (int)num + l + 1;
    return 0;
}

int ndn_interest_get_name(ndn_block_t* block, ndn_block_t* name)
{
    if (name == NULL || block == NULL) return -1;

    const uint8_t* buf = block->buf;
    int len = block->len;
    uint32_t num;
    int l;

    /* read interest type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_INTEREST) return -1;
    buf += l;
    len -= l;

    /* read interest length and ignore the value */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l;
    len -= l;

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

int ndn_interest_get_nonce(ndn_block_t* block, uint32_t* nonce)
{
    if (nonce == NULL || block == NULL) return -1;

    const uint8_t* buf = block->buf;
    int len = block->len;
    uint32_t num;
    int l;

    /* read interest type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_INTEREST) return -1;
    buf += l;
    len -= l;

    /* read interest length and ignore the value */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l;
    len -= l;

    /* read name type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_NAME) return -1;
    buf += l;
    len -= l;

    /* read name length and skip the components */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += (l + num);
    len -= (l + num);

    /* read and skip selectors */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num == NDN_TLV_SELECTORS) {
	/* skip type */
	buf += l;
	len -= l;

	/* read length and skip length and value */
	l = ndn_block_get_var_number(buf, len, &num);
	if (l < 0) return -1;
	buf += (l + num);
	len -= (l + num);

	/* read nonce type */
	l = ndn_block_get_var_number(buf, len, &num);
	if (l < 0) return -1;
    }

    /* check for nonce type */
    if (num != NDN_TLV_NONCE) return -1;
    buf += l;
    len -= l;

    /* check nonce length */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != 4) return -1;
    buf += l;
    len -= l;

    /* read nonce value */
    if (len < 4) return -1;
    ndn_block_get_integer(buf, 4, nonce);
    return 0;
}

int ndn_interest_get_lifetime(ndn_block_t* block, uint32_t* life)
{
    if (life == NULL || block == NULL) return -1;

    const uint8_t* buf = block->buf;
    int len = block->len;
    uint32_t num;
    int l;

    /* read interest type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_INTEREST) return -1;
    buf += l;
    len -= l;

    /* read interest length and ignore the value */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l;
    len -= l;

    /* read name type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_NAME) return -1;
    buf += l;
    len -= l;

    /* read name length and skip the components */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += (l + num);
    len -= (l + num);

    /* read and skip selectors */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num == NDN_TLV_SELECTORS) {
	/* skip type */
	buf += l;
	len -= l;

	/* read length and skip length and value */
	l = ndn_block_get_var_number(buf, len, &num);
	if (l < 0) return -1;
	buf += (l + num);
	len -= (l + num);

	/* read nonce type */
	l = ndn_block_get_var_number(buf, len, &num);
	if (l < 0) return -1;
    }

    /* check for nonce type */
    if (num != NDN_TLV_NONCE) return -1;
    buf += l;
    len -= l;

    /* check nonce length and skip the value */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != 4) return -1;
    buf += (l + num);
    len -= (l + num);

    /* read lifetime type */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num != NDN_TLV_INTERESTLIFETIME) return -1;
    buf += l;
    len -= l;

    /* read lifetime length */
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    buf += l;
    len -= l;

    l = ndn_block_get_integer(buf, num, life);
    if (l < 0) return -1;
    else return 0;
}

/** @} */
