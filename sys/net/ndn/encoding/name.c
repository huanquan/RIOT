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

#include "net/ndn/ndn-constants.h"
#include "net/ndn/encoding/block.h"
#include "net/ndn/encoding/name.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

int ndn_name_component_compare(ndn_name_component_t* lhs, ndn_name_component_t* rhs)
{
    if (lhs == NULL || rhs == NULL) return -2;

    if (lhs->buf == NULL && lhs->len != 0) return -2;
    if (rhs->buf == NULL && rhs->len != 0) return -2;

    if (lhs->len < rhs->len) return -1;
    else if (lhs->len > rhs->len) return 1;
    else
    {
	int n = memcmp(lhs->buf, rhs->buf, rhs->len);
	if (n < 0) return -1;
	else if (n > 0) return 1;
	else return 0;
    }
}

int ndn_name_component_wire_encode(ndn_name_component_t* comp, uint8_t* buf, int len)
{
    if (comp == NULL || buf == NULL) return -1;
    if (comp->buf == NULL) return -1;

    int tl = ndn_block_total_length(NDN_TLV_NAME_COMPONENT, comp->len);
    if (tl < 0 || tl > len) return -1;

    int bytes_written = ndn_block_put_var_number(NDN_TLV_NAME_COMPONENT, buf, len);
    bytes_written += ndn_block_put_var_number(comp->len, buf + bytes_written, len - bytes_written);
    memcpy(buf + bytes_written, comp->buf, comp->len);
    return tl;
}


int ndn_name_compare(ndn_name_t* lhs, ndn_name_t* rhs)
{
    if (lhs == NULL || rhs == NULL) return -2;
    if (lhs->comps == NULL && lhs->size != 0) return -2;
    if (rhs->comps == NULL && rhs->size != 0) return -2;

    if (lhs->size < rhs->size) return -1;
    else if (lhs->size > rhs->size) return 1;
    else
    {
	for (int i = 0; i < lhs->size; ++i)
	{
	    int res = ndn_name_component_compare(&lhs->comps[i], &rhs->comps[i]);
	    if (res == 0) continue;
	    else return res;
	}
	return 0;
    }
}

int ndn_name_get_component(ndn_name_t* name, int pos, ndn_name_component_t* comp)
{
    if (name == NULL || comp == NULL) return -1;

    if (pos >= name->size || pos < -1 * (name->size)) return -1;

    if (pos < 0) pos += name->size;
    *comp = name->comps[pos];
    return 0;
}

/* computes the total length of TLV-encoded components in the name */
static int _ndn_name_length(ndn_name_t* name)
{
    if (name == NULL) return -1;
    if (name->comps == NULL) return 0;
    int res = 0;
    for (int i = 0; i < name->size; ++i)
    {
	ndn_name_component_t* comp = &name->comps[i];
	if (comp->buf == NULL || comp->len <= 0) return -1;
	int l = ndn_block_total_length(NDN_TLV_NAME_COMPONENT, comp->len);
	if (l == -1) return -1;
	else res += l;
    }
    return res;
}

int ndn_name_total_length(ndn_name_t* name)
{
    int cl = _ndn_name_length(name);
    if (cl <= 0) return cl;
    int tl = ndn_block_total_length(NDN_TLV_NAME, cl);
    return tl;
}

int ndn_name_wire_encode(ndn_name_t* name, uint8_t* buf, int len)
{
    if (name == NULL || buf == NULL) return -1;

    int cl = _ndn_name_length(name);
    if (cl <= 0) return cl;
    int tl = ndn_block_total_length(NDN_TLV_NAME, cl);
    if (tl > len) return -1;

    int bytes_written = ndn_block_put_var_number(NDN_TLV_NAME, buf, len);
    bytes_written += ndn_block_put_var_number(cl, buf + bytes_written, len - bytes_written);
    for (int i = 0; i < name->size; ++i)
    {
	bytes_written += ndn_name_component_wire_encode(&name->comps[i], buf + bytes_written,
							len - bytes_written);
    }
    return tl;
}

/** @} */
