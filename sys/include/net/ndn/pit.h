/*
 * Copyright (C) 2016 Wentao Shang
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    net_ndn    NDN packet processing
 * @ingroup     net
 * @brief       NDN packet sending and receiving.
 * @{
 *
 * @file
 * @brief   NDN PIT implementation. Mostly a wrapper around utlist.
 *
 * @author  Wentao Shang <wentaoshang@gmail.com>
 */
#ifndef NDN_PIT_H_
#define NDN_PIT_H_

#include "kernel_types.h"
#include "xtimer.h"

#include "net/ndn/encoding/block.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Type to represent the PIT entry.
 */
typedef struct ndn_pit_entry {
    struct ndn_pit_entry *prev;
    struct ndn_pit_entry *next;
    kernel_pid_t face_id;
    int face_type;
    ndn_block_t interest;   /**< TLV block of the pending interest */
    uint32_t expire;        /**< expiration time in us */
    xtimer_t timer;         /**< xtimer struct */
    msg_t timer_msg;        /**< special message to indicate timeout event */
} ndn_pit_entry_t;

/**
 * @brief      Adds an entry to PIT.
 * 
 * @param[in]  face_id    ID of the incoming face.
 * @param[in]  face_type  Type of the incoming face.
 * @param[in]  block      TLV block of the Interest packet to add.
 * @param[in]  timeout    Timeout value for the new entry in ns.
 *
 * @return     Pointer to the new PIT entry, if success.
 * @retrun     NULL, if out of memory.
 */
ndn_pit_entry_t* ndn_pit_add(kernel_pid_t face_id, int face_type,
			     ndn_block_t* block, uint32_t timeout);

/**
 * @brief  Remove the expired entry from PIT based on the @p msg pointer.
 */
void ndn_pit_remove(msg_t *msg);

/**
 * @brief    Initializes the pending interest table.
 */
void ndn_pit_init(void);


#ifdef __cplusplus
}
#endif

#endif /* NDN_PIT_H_ */
/** @} */
