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
#include "net/ndn/encoding/block.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

/* helper function to read variable-length encoded number */
static int _ndn_block_get_var_number(const uint8_t* buf, int len)
{
    if (buf == NULL || len <= 0) return -1;

    uint8_t val = *buf;
    if (val > 0 && val < 253) return val;
    else return -1;  //TODO: support multi-byte var-number.
}

int ndn_block_put_var_number(unsigned int num, uint8_t* buf, int len)
{
    if (buf == NULL || len <= 0) return -1;

    if (num >= 253) return -1;  //TODO: support multi-byte var-number.

    buf[0] = num & 0xFF;
    return 1;
}

int ndn_block_get_type(const uint8_t* buf, int len)
{
    return _ndn_block_get_var_number(buf, len);
}


int ndn_block_get_length(const uint8_t* buf, int len)
{
    int type = _ndn_block_get_var_number(buf, len);
    if (type == -1) return -1;
    else if (type > 0 && type < 253)
	return _ndn_block_get_var_number(buf + 1, len - 1);
    else return -1;  //TODO: support multi-byte var-number.
}


const uint8_t* ndn_block_get_value(const uint8_t* buf, int len)
{
    if (buf == NULL) return NULL;

    int delta = 0;
    for (int i = 0; i < 2; ++i)
    {
	if (len <= delta) return NULL;

	uint8_t val = *(buf + delta);
	if (val > 0 && val < 253) delta += 1;
	else return NULL;  //TODO: support multi-byte var-number.
    }

    if (len <= delta) return NULL;
    else return (buf + delta);
}


int ndn_block_integer_length(uint32_t num)
{
    if (num <= 0xFF) return 1;
    else if (num <= 0xFFFF) return 2;
    else return 4;
}

int ndn_block_put_integer(uint32_t num, uint8_t* buf, int len)
{
    if (buf == NULL || len <= 0) return -1;

    if (num <= 0xFF) {
	buf[0] = num & 0xFF;
	return 1;
    } else if (num <= 0xFFFF && len >= 2) {
	buf[0] = (num >> 8) & 0xFF;
	buf[1] = num & 0xFF;
	return 2;
    } else if (len >= 4) {
	buf[0] = (num >> 24) & 0xFF;
	buf[1] = (num >> 16) & 0xFF;
	buf[2] = (num >> 8) & 0xFF;
	buf[3] = num & 0xFF;
	return 4;	
    }

    return -1;
}

static int _ndn_block_var_number_length(int num)
{
    if (num >= 0 && num < 253) return 1;
    else return -1;  //TODO: support multi-byte var-number.
}


int ndn_block_total_length(int type, int length)
{
    int type_len = _ndn_block_var_number_length(type);
    int length_len = _ndn_block_var_number_length(length);
    if (type_len == -1 || length_len == -1) return -1;
    else return (type_len + length_len + length);
}


/** @} */
