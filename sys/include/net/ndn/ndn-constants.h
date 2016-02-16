/*
 * Copyright (C) 2016 Wentao Shang
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    net_ndn    NDN
 * @ingroup     net
 * @brief       NDN implementation for RIOT-OS.
 * @{
 *
 * @file
 * @brief   NDN TLV-related utilities.
 *
 * @author  Wentao Shang <wentaoshang@gmail.com>
 */
#ifndef NDN_TLV_CONSTANTS_H_
#define NDN_TLV_CONSTANTS_H_

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* Basic TLVs */
    NDN_TLV_INTEREST         = 5,
    NDN_TLV_DATA             = 6,
    NDN_TLV_NAME             = 7,
    NDN_TLV_NAME_COMPONENT   = 8,

    /* Interest-related TLVs */
    NDN_TLV_SELECTORS        = 9,
    NDN_TLV_NONCE            = 10,
    NDN_TLV_INTERESTLIFETIME = 11,

    /* Data-related TLVs */
    NDN_TLV_METAINFO         = 20,
    NDN_TLV_CONTENT          = 21,
    NDN_TLV_SIGNATUREINFO    = 22,
    NDN_TLV_SIGNATUREVALUE   = 23,

    /* Metainfo-related TLVs */
    NDN_TLV_CONTENT_TYPE     = 24,
    NDN_TLV_FRESHNESS_PERIOD = 25,

    /* Signature-related TLVs */
    NDN_TLV_SIGNATURE_TYPE   = 27,
};


#ifdef __cplusplus
}
#endif

#endif /* NDN_TLV_CONSTANTS_H_ */
/** @} */
