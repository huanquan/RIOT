/*
 * Copyright (C) 2016 Wentao Shang
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    net_ndn_encoding    NDN packet encoding
 * @ingroup     net_ndn
 * @brief       NDN TLV packet encoding and decoding.
 * @{
 *
 * @file
 * @brief   NDN TLV block utilities.
 *
 * @author  Wentao Shang <wentaoshang@gmail.com>
 */
#ifndef NDN_BLOCK_H_
#define NDN_BLOCK_H_

#include <inttypes.h>
#include <sys/types.h>

#include "net/ndn/ndn-constants.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Writes a non-negative integer into a buffer using
 *          variable-length encoding.
 *
 * @param[in] num       The number to encode.
 * @param[in] buf       Buffer to write @p num into.
 * @param[in] len       Size of the TLV block pointed by @p buf.
 *
 * @return  The number of bytes written into the buffer.
 * @return  -1, if there is not enough space to write @p num.
 * @return  -1, if @p num is invalid or @p buf is NULL.
 */
int ndn_block_put_var_number(unsigned int num, uint8_t* buf, int len);

/**
 * @brief   Reads the type field from the TLV block.
 *
 * @param[in] buf       Pointer to the TLV block.
 * @param[in] len       Size of the TLV block pointed by @p buf.
 *
 * @return  The value of the type field encoded in the TLV block.
 * @return  -1, if the type value is invalid.
 */
int ndn_block_get_type(const uint8_t* buf, int len);

/**
 * @brief   Reads the length field from the TLV block.
 *
 * @param[in] buf       Pointer to the TLV block.
 * @param[in] len       Size of the TLV block pointed by @p buf.
 *
 * @return  The value of the length field encoded in the TLV block.
 * @return  -1, if the length value is invalid.
 */
int ndn_block_get_length(const uint8_t* buf, int len);

/**
 * @brief   Gets the value field in the TLV block.
 *
 * @param[in] buf       Pointer to the TLV block.
 * @param[in] len       Size of the TLV block pointed by @p buf.
 *
 * @return  The pointer to the beginning of the value field encoded in the TLV block.
 * @return  NULL, if the TLV block does not contain a value field.
 */
const uint8_t* ndn_block_get_value(const uint8_t* buf, int len);

/**
 * @brief   Computes the length of the encoded non-negative 32-bit integer.
 *
 * @param[in] num       Non-negative integer to be encoded.
 *
 * @return  Length of the encoded non-negative integer.
 */
int ndn_block_integer_length(uint32_t num);

/**
 * @brief   Writes an non-negative integer into a caller-supplied buffer
 *          using NDN non-negative integer encoding format.
 *
 * @param[in] num       Non-negative integer to be encoded.
 * @param[in] buf       Buffer to write into.
 * @param[in] len       Size of the buffer
 *
 * @return  Number of bytes written, if success.
 * @return  -1, if @p buf is NULL or not big enough to hold the encoded integer.
 */
int ndn_block_put_integer(uint32_t num, uint8_t* buf, int len);

/**
 * @brief   Computes the total length of the TLV block.
 *
 * @param[in] type      Type value of the TLV block.
 * @param[in] length    Length value of the TLV block.
 *
 * @return  Total length of the TLV block.
 * @return  -1, if @p type or @p length is invalid or less than 0.
 */
int ndn_block_total_length(int type, int length);

#ifdef __cplusplus
}
#endif

#endif /* NDN_BLOCK_H_ */
/** @} */
