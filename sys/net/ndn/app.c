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
#include "net/gnrc/netreg.h"
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
	      PRIkernel_pid ")\n", thread_getpid());
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

static int _notify_consumer_timeout(ndn_app_t* handle, ndn_block_t* pi)
{
    ndn_block_t pn;
    if (ndn_interest_get_name(pi, &pn) != 0) {
	DEBUG("ndn_app: cannot parse name from pending interest (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	return NDN_APP_CONTINUE;
    }

    _consumer_cb_entry_t *entry, *tmp;
    DL_FOREACH_SAFE(handle->_ccb_table, entry, tmp) {
	ndn_block_t n;
	if (ndn_interest_get_name(&entry->pi, &n) != 0) {
	    DEBUG("ndn_app: cannot parse name from interest in cb table (pid=%"
		  PRIkernel_pid ")\n", handle->id);
	    goto clean;
	}

	if (0 != memcmp(pn.buf, n.buf, pn.len < n.len ? pn.len : n.len)) {
	    // not the same interest name
	    //TODO: check selectors
	    continue;
	}

	// raise timeout callback
	int r = NDN_APP_CONTINUE;
	if (entry->on_timeout != NULL) {
	    DEBUG("ndn_app: call consumer timeout cb (pid=%"
		  PRIkernel_pid ")\n", handle->id);
	    r = entry->on_timeout(&entry->pi);
	}

    clean:
	DL_DELETE(handle->_ccb_table, entry);
	free((void*)entry->pi.buf);
	free(entry);

	// stop the app now if the callback returns error or stop
	if (r != NDN_APP_CONTINUE) return r;
	// otherwise continue
    }

    return NDN_APP_CONTINUE;
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

		ret = _notify_consumer_timeout(handle, &ptr->block);

		DEBUG("ndn_app: release shared block pointer in received msg (pid=%"
		      PRIkernel_pid ")\n", handle->id);
		ndn_shared_block_release(ptr);

		if (ret != NDN_APP_CONTINUE) {
		    DEBUG("ndn_app: stop app because timeout callback returned %s (pid=%"
			  PRIkernel_pid ")\n", ret == NDN_APP_STOP ? "STOP" : "ERROR",
			  handle->id);
		    return ret;
		}
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

static inline void _release_consumer_cb_table(ndn_app_t* handle)
{
    _consumer_cb_entry_t *entry, *tmp;
    DL_FOREACH_SAFE(handle->_ccb_table, entry, tmp) {
	DEBUG("ndn_app: remove consumer cb entry (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	DL_DELETE(handle->_ccb_table, entry);
	free((void*)entry->pi.buf);
	free(entry);
    }
}

static int _add_consumer_cb_entry(ndn_app_t* handle, ndn_block_t* pi,
				  ndn_app_data_cb_t on_data,
				  ndn_app_timeout_cb_t on_timeout)
{
    _consumer_cb_entry_t *entry =
	(_consumer_cb_entry_t*)malloc(sizeof(_consumer_cb_entry_t));
    if (entry == NULL) {
	DEBUG("ndn_app: cannot allocate memory for consumer cb entry (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	return -1;
    }

    entry->on_data = on_data;
    entry->on_timeout = on_timeout;

    // "Move" pending interest block into the entry
    entry->pi.buf = pi->buf;
    entry->pi.len = pi->len;
    pi->buf = NULL;
    pi->len = 0;

    DL_PREPEND(handle->_ccb_table, entry);
    DEBUG("ndn_app: add consumer cb entry (pid=%" PRIkernel_pid ")\n", handle->id);
    return 0;
}

static inline void _release_producer_cb_table(ndn_app_t* handle)
{
    _producer_cb_entry_t *entry, *tmp;
    DL_FOREACH_SAFE(handle->_pcb_table, entry, tmp) {
	DEBUG("ndn_app: remove producer cb entry (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	DL_DELETE(handle->_pcb_table, entry);
	free((void*)entry->prefix.buf);
	free(entry);
    }
}

void ndn_app_destroy(ndn_app_t* handle)
{
    _release_consumer_cb_table(handle);
    _release_producer_cb_table(handle);
    //TODO: clear msg queue
    free(handle);
}

int ndn_app_express_interest(ndn_app_t* handle, ndn_name_t* name,
			     void* selectors, uint32_t lifetime,
			     ndn_app_data_cb_t on_data,
			     ndn_app_timeout_cb_t on_timeout)
{
    if (handle == NULL) return -1;

    // create encoded TLV block
    ndn_block_t pi;
    if (ndn_interest_create(name, selectors, lifetime, &pi) != 0) {
	DEBUG("ndn_app: cannot create interest block (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	return -1;
    }

    // create interest packet snip
    gnrc_pktsnip_t* inst = ndn_interest_create_packet(&pi);
    if (inst == NULL) {
	DEBUG("ndn_app: cannot create interest packet snip (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	return -1;
    }

    // add entry to consumer callback table
    if (0 != _add_consumer_cb_entry(handle, &pi, on_data, on_timeout)) {
	DEBUG("ndn_app: cannot add consumer cb entry (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	gnrc_pktbuf_release(inst);
	return -1;
    }
    // "pi" will be useless after this point

    // send packet to NDN thread
    if (!gnrc_netapi_dispatch_send(GNRC_NETTYPE_NDN,
				   GNRC_NETREG_DEMUX_CTX_ALL, inst)) {
	DEBUG("ndn_test: cannot send interest to NDN thread (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	gnrc_pktbuf_release(inst);
	return -1;
    }

    return 0;
}

/** @} */
