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
 * @brief   Interface between NDN and NDN app (aka. client library).
 *
 * @author  Wentao Shang <wentaoshang@gmail.com>
 */
#ifndef NDN_APP_H_
#define NDN_APP_H_

#include "kernel_types.h"
#include "net/ndn/shared_block.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief  Return code for the callbacks.
 */
enum {
    NDN_APP_ERROR = -1,    /**< app should stop due to an error */
    NDN_APP_STOP = 0,      /**< app should stop after this callback */
    NDN_APP_CONTINUE = 1,  /**< app should continue after this callback */
};

/**
 * @brief  Type for the on_data consumer callback.
 */
typedef int (*ndn_app_data_cb_t)(ndn_block_t* interest, ndn_block_t* data);

/**
 * @brief  Type for the on_timeout consumer callback.
 */
typedef int (*ndn_app_timeout_cb_t)(ndn_block_t* interest);

/**
 * @brief  Type for the on_interest producer callback.
 */
typedef int (*ndn_app_interest_cb_t)(ndn_block_t* interest);

/**
 * @brief  Type for the error handler.
 */
typedef int (*ndn_app_error_cb_t)(int error);

/**
 * @brief  Type for the consumer callback table entry.
 */
typedef struct _consumer_cb_entry {
    struct _consumer_cb_entry *prev;
    struct _consumer_cb_entry *next;
    ndn_block_t interest;               /**< expressed interest */
    ndn_app_data_cb_t  on_data;         /**< handler for the on_data event */
    ndn_app_timeout_cb_t  on_timeout;   /**< handler for the on_timeout event */
    ndn_app_error_cb_t on_error;        /**< handler for error */
} _consumer_cb_entry_t;

/**
 * @brief  Type for the producer callback table entry.
 */
typedef struct _producer_cb_entry {
    struct _consumer_cb_entry *prev;
    struct _consumer_cb_entry *next;
    ndn_block_t prefix;                 /**< registered prefix */
    ndn_app_interest_cb_t  on_data;     /**< handler for the on_interest event */
    ndn_app_error_cb_t  on_error;       /**< handler for error */
} _producer_cb_entry_t;



#define NDN_APP_MSG_QUEUE_SIZE  (8)

/**
 * @brief   Type to represent an NDN app handle and its associated context.
 * @details This struct is not lock-protected and should only be accessed from
 *          a single thread.
 */
typedef struct ndn_app {
    kernel_pid_t id;    /**< pid of the app thread */
    msg_t _msg_queue[NDN_APP_MSG_QUEUE_SIZE];  /**< message queue of the app thread */
    _consumer_cb_entry_t *_ccb_table;   /**< consumer callback table */
    _producer_cb_entry_t *_pcb_table;   /**< producer callback table */
} ndn_app_t;

/**
 * @brief   Creates a handle for an NDN app and initialize the context.
 * @details This function is reentrant and can be called from multiple threads.
 *
 * @return  Pointer to the newly created @ref ndn_app_t struct, if success.
 * @return  NULL, if cannot allocate memory for the handle.
 */
ndn_app_t* ndn_app_create(void);

/**
 * @brief   Runs the event loop with the app handle.
 * @details This function is reentrant and can be called from multiple threads.
 *          However, the same handle cannot be used twice by this function at the
 *          same time.
 *
 * @param[in]  handle    Handle of the app to run.
 *
 * @return  One of the return codes for the callbacks.
 */
int ndn_app_run(ndn_app_t* handle);

/**
 * @brief   Releases the app handle and all associated memory.
 */
void ndn_app_destroy(ndn_app_t* handle);

#ifdef __cplusplus
}
#endif

#endif /* NDN_APP_H_ */
/** @} */
