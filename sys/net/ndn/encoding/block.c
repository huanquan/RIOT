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

int ndn_block_get_var_number(const uint8_t* buf, int len, uint32_t* num)
{
    if (buf == NULL || len <= 0 || num == NULL) return -1;

    uint8_t val = buf[0];
    if (val > 0 && val < 253) {
	*num = val;
	return 1;
    } else if (val == 253 && len >= 3) {
	*num = (buf[1] << 8) + buf[2];
	return 3;
    } else if (val == 254 && len >= 5) {
	*num = (buf[1] << 24) + (buf[2] << 16) + (buf[3] << 8) + buf[4];
	return 5;
    }
    else return -1;  //TODO: support 8-byte var-number.
}

int ndn_block_put_var_number(uint32_t num, uint8_t* buf, int len)
{
    if (buf == NULL || len <= 0) return -1;

    if (num < 253) {
	buf[0] = num & 0xFF;
	return 1;
    } else if (num <= 0xFFFF) {
	if (len < 3) return -1;
	buf[0] = 253;
	buf[1] = (num >> 8) & 0xFF;
	buf[2] = num & 0xFF;
	return 3;
    } else {
	if (len < 5) return -1;
	buf[0] = 254;
	buf[1] = (num >> 24) & 0xFF;
	buf[2] = (num >> 16) & 0xFF;
	buf[3] = (num >> 8) & 0xFF;
	buf[4] = num & 0xFF;
	return 5;
    }
}

int ndn_block_var_number_length(uint32_t num)
{
    if (num < 253) return 1;
    if (num <= 0xFFFF) return 3;
    else return 5;
}

int ndn_block_total_length(uint32_t type, uint32_t length)
{
    int type_len = ndn_block_var_number_length(type);
    int length_len = ndn_block_var_number_length(length);
    return (type_len + length_len + length);
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

int ndn_block_get_integer(const uint8_t* buf, int len, uint32_t* num)
{
    if (buf == NULL || len <= 0) return -1;

    if (len == 1) {
	*num = buf[0];
	return 1;
    } else if (len == 2) {
	*num = (buf[0] << 8) + buf[1];
	return 2;
    } else if (len == 4) {
	*num = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
	return 4;
    } else return -1;
}

/** @} */
