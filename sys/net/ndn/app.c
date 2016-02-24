/*
 * Copyright (C) 2016 Wentao Shang
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     net_ndn
 * @{
 *
 * @file
 *
 * @author  Wentao Shang <wentaoshang@gmail.com>
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "msg.h"
#include "thread.h"
#include "utlist.h"
#include "net/gnrc/netapi.h"
#include "net/ndn/shared_block.h"
#include "net/ndn/encoding/interest.h"
#include "net/ndn/msg_type.h"

#include "net/ndn/app.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

ndn_app_t* ndn_app_create(void)
{
    ndn_app_t *handle = (ndn_app_t*)malloc(sizeof(ndn_app_t));
    if (handle == NULL) {
	DEBUG("ndn_app: cannot alloacte memory for app handle (pid=%"
	      PRIkernel_pid "\n", thread_getpid());
	return NULL;
    }

    handle->id = thread_getpid();  // set to caller pid
    handle->_ccb_table = NULL;
    handle->_pcb_table = NULL;

    if (msg_init_queue(handle->_msg_queue, NDN_APP_MSG_QUEUE_SIZE) != 0) {
	DEBUG("ndn_app: cannot init msg queue (pid=%" PRIkernel_pid ")\n", handle->id);
	free(handle);
	return NULL;
    }

    //TODO: add face id to face table

    return handle;
}

int ndn_app_run(ndn_app_t* handle)
{
    if (handle == NULL) return NDN_APP_ERROR;

    int ret = NDN_APP_STOP;

    msg_t msg, reply;
    reply.type = GNRC_NETAPI_MSG_TYPE_ACK;
    reply.content.value = (uint32_t)(-ENOTSUP);

    while (1) {
	msg_receive(&msg);

	switch (msg.type) {
	    case NDN_APP_MSG_TYPE_TIMEOUT:
		DEBUG("ndn_app: TIMEOUT msg received from thread %" PRIkernel_pid
		      " (pid=%" PRIkernel_pid ")\n", msg.sender_pid, handle->id);
		ndn_shared_block_t* ptr = (ndn_shared_block_t*)msg.content.ptr;
		DEBUG("ndn_app: release shared block pointer in received msg\n");
		ndn_shared_block_release(ptr);
		break;
	    case GNRC_NETAPI_MSG_TYPE_GET:
	    case GNRC_NETAPI_MSG_TYPE_SET:
		msg_reply(&msg, &reply);
		break;
	    default:
		DEBUG("ndn_app: unknown msg type %u (pid=%" PRIkernel_pid ")\n",
		      msg.type, handle->id);
		break;
	}

	break;
    }

    return ret;
}

void ndn_app_destroy(ndn_app_t* handle)
{
    free(handle);
}

/** @} */
