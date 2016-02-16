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


gnrc_pktsnip_t* ndn_interest_create(ndn_name_t* name, void* selectors, unsigned int lifetime)
{
    if (name == NULL) return NULL;

    if (selectors != NULL) return NULL;  //TODO: support selectors.

    int name_len = ndn_name_total_length(name);
    if (name_len <= 0) return NULL;

    int nonce_lt_len = 8 + ndn_block_integer_length(lifetime);

    if (name_len + nonce_lt_len > 253)
	return NULL;  //TODO: support multi-byte length field.

    gnrc_pktsnip_t *head_snip = NULL, *nonce_lt_snip = NULL;
    uint8_t* buf = NULL;

    // Create nonce+lifetime snip.
    nonce_lt_snip = gnrc_pktbuf_add(NULL, NULL, nonce_lt_len, GNRC_NETTYPE_NDN);
    if (nonce_lt_snip == NULL) {
	DEBUG("ndn_encoding: cannot create nonce+lifetime snip: unable to allocate packet\n");
        return NULL;
    }
    buf = (uint8_t*) (nonce_lt_snip->data);
    
    // Fill in the nonce.
    uint32_t nonce = genrand_uint32();
    buf[0] = NDN_TLV_NONCE;
    buf[1] = 4;  // Nonce field length
    buf[2] = (nonce >> 24) & 0xFF;
    buf[3] = (nonce >> 16) & 0xFF;
    buf[4] = (nonce >> 8) & 0xFF;
    buf[5] = nonce & 0xFF;
    buf[6] = NDN_TLV_INTERESTLIFETIME;
    buf[7] = nonce_lt_len - 8;  // Lifetime field length
    ndn_block_put_integer(lifetime, buf + 8, buf[7]);


    // Create header+name snip.
    head_snip = gnrc_pktbuf_add(nonce_lt_snip, NULL, 2 + name_len, GNRC_NETTYPE_NDN);
    if (head_snip == NULL) {
	DEBUG("ndn_encoding: cannot create header+name snip: unable to allocate packet\n");
	gnrc_pktbuf_release(nonce_lt_snip);
        return NULL;
    }
    buf = (uint8_t*) (head_snip->data);

    // Fill in the Interest header and name field.
    buf[0] = NDN_TLV_INTEREST;
    buf[1] = name_len + nonce_lt_len;
    ndn_name_wire_encode(name, buf + 2, name_len);

    return head_snip;
}

/** @} */
