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
#include "net/ndn/encoding/shared_block.h"
#include "net/ndn/encoding/name.h"
#include "net/ndn/encoding/interest.h"
#include "net/ndn/msg_type.h"
#include "net/ndn/ndn.h"

#include "net/ndn/app.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

ndn_app_t* ndn_app_create(void)
{
    if (ndn_pid == KERNEL_PID_UNDEF) {
	DEBUG("ndn_app: ndn thread not initialized (pid=%"
	      PRIkernel_pid ")\n", thread_getpid());
	return NULL;
    }

    ndn_app_t *handle = (ndn_app_t*)malloc(sizeof(ndn_app_t));
    if (handle == NULL) {
	DEBUG("ndn_app: cannot alloacte memory for app handle (pid=%"
	      PRIkernel_pid ")\n", thread_getpid());
	return NULL;
    }

    handle->id = thread_getpid();  // set to caller pid
    handle->_ccb_table = NULL;
    handle->_pcb_table = NULL;

    // add face id to face table
    msg_t add_face, reply;
    add_face.type = NDN_APP_MSG_TYPE_ADD_FACE;
    add_face.content.value = (uint32_t)handle->id;
    reply.content.value = 1;
    msg_send_receive(&add_face, &reply, ndn_pid);
    if (reply.content.value != 0) {
	DEBUG("ndn_app: cannot add app face (pid=%" PRIkernel_pid ")\n", handle->id);
	free(handle);
	return NULL;
    }

    // init msg queue to receive message
    if (msg_init_queue(handle->_msg_queue, NDN_APP_MSG_QUEUE_SIZE) != 0) {
	DEBUG("ndn_app: cannot init msg queue (pid=%" PRIkernel_pid ")\n", handle->id);
	free(handle);
	return NULL;
    }

    return handle;
}

static int _notify_consumer_timeout(ndn_app_t* handle, ndn_block_t* pi)
{
    ndn_block_t pn;
    if (ndn_interest_get_name(pi, &pn) != 0) {
	DEBUG("ndn_app: cannot parse name from pending interest (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	return NDN_APP_ERROR;
    }

    _consumer_cb_entry_t *entry, *tmp;
    DL_FOREACH_SAFE(handle->_ccb_table, entry, tmp) {
	ndn_block_t n;
	if (ndn_interest_get_name(&entry->pi->block, &n) != 0) {
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
	    r = entry->on_timeout(&entry->pi->block);
	}

    clean:
	DL_DELETE(handle->_ccb_table, entry);
	ndn_shared_block_release(entry->pi);
	free(entry);

	// stop the app now if the callback returns error or stop
	if (r != NDN_APP_CONTINUE) return r;
	// otherwise continue
    }

    return NDN_APP_CONTINUE;
}

static int _notify_producer_interest(ndn_app_t* handle, ndn_block_t* interest)
{
    ndn_block_t name;
    if (ndn_interest_get_name(interest, &name) != 0) {
	DEBUG("ndn_app: cannot parse name from received interest (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	return NDN_APP_ERROR;
    }
    
    _producer_cb_entry_t *entry;
    DL_FOREACH(handle->_pcb_table, entry) {
	if (-2 != ndn_name_compare_block(&entry->prefix->block, &name)) {
	    continue;
	}

	// raise interest callback
	int r = NDN_APP_CONTINUE;
	if (entry->on_interest != NULL) {
	    DEBUG("ndn_app: call producer interest cb (pid=%"
		  PRIkernel_pid ")\n", handle->id);
	    r = entry->on_interest(interest);
	}

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
    ndn_shared_block_t* ptr;
    msg_t msg, reply;
    reply.type = GNRC_NETAPI_MSG_TYPE_ACK;
    reply.content.value = (uint32_t)(-ENOTSUP);

    while (1) {
	msg_receive(&msg);

	switch (msg.type) {
	    case NDN_APP_MSG_TYPE_TERMINATE:
		DEBUG("ndn_app: TERMINATE msg received from thread %" PRIkernel_pid
		      " (pid=%" PRIkernel_pid ")\n", msg.sender_pid, handle->id);
		return NDN_APP_STOP;

	    case NDN_APP_MSG_TYPE_TIMEOUT:
		DEBUG("ndn_app: TIMEOUT msg received from thread %" PRIkernel_pid
		      " (pid=%" PRIkernel_pid ")\n", msg.sender_pid, handle->id);
		ptr = (ndn_shared_block_t*)msg.content.ptr;

		ret = _notify_consumer_timeout(handle, &ptr->block);

		ndn_shared_block_release(ptr);

		if (ret != NDN_APP_CONTINUE) {
		    DEBUG("ndn_app: stop app because timeout callback returned %s (pid=%"
			  PRIkernel_pid ")\n", ret == NDN_APP_STOP ? "STOP" : "ERROR",
			  handle->id);
		    return ret;
		}
		break;

	    case NDN_APP_MSG_TYPE_INTEREST:
		DEBUG("ndn_app: INTEREST msg received from thread %" PRIkernel_pid
		      " (pid=%" PRIkernel_pid ")\n", msg.sender_pid, handle->id);
		ptr = (ndn_shared_block_t*)msg.content.ptr;

		ret = _notify_producer_interest(handle, &ptr->block);

		ndn_shared_block_release(ptr);

		if (ret != NDN_APP_CONTINUE) {
		    DEBUG("ndn_app: stop app because interest callback returned %s (pid=%"
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
	ndn_shared_block_release(entry->pi);
	free(entry);
    }
}

static inline void _release_producer_cb_table(ndn_app_t* handle)
{
    _producer_cb_entry_t *entry, *tmp;
    DL_FOREACH_SAFE(handle->_pcb_table, entry, tmp) {
	DEBUG("ndn_app: remove producer cb entry (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	DL_DELETE(handle->_pcb_table, entry);
	ndn_shared_block_release(entry->prefix);
	free(entry);
    }
}

void ndn_app_destroy(ndn_app_t* handle)
{
    _release_consumer_cb_table(handle);
    _release_producer_cb_table(handle);

    // remove face id to face table
    msg_t add_face, reply;
    add_face.type = NDN_APP_MSG_TYPE_REMOVE_FACE;
    add_face.content.value = (uint32_t)handle->id;
    reply.content.value = 1;
    msg_send_receive(&add_face, &reply, ndn_pid);
    if (reply.content.value != 0) {
	DEBUG("ndn_app: error removing app face (pid=%" PRIkernel_pid ")\n", handle->id);
	// ignore the error anyway...
    }

    //TODO: clear msg queue
    free(handle);
}

static int _add_consumer_cb_entry(ndn_app_t* handle, ndn_shared_block_t* si,
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

    entry->pi = si;  // move semantics

    DL_PREPEND(handle->_ccb_table, entry);
    DEBUG("ndn_app: add consumer cb entry (pid=%" PRIkernel_pid ")\n", handle->id);
    return 0;
}

int ndn_app_express_interest(ndn_app_t* handle, ndn_name_t* name,
			     void* selectors, uint32_t lifetime,
			     ndn_app_data_cb_t on_data,
			     ndn_app_timeout_cb_t on_timeout)
{
    if (handle == NULL) return -1;

    // create encoded TLV block
    ndn_shared_block_t* si = ndn_interest_create(name, selectors, lifetime);
    if (si == NULL) {
	DEBUG("ndn_app: cannot create interest block (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	return -1;
    }

    // create interest packet snip
    gnrc_pktsnip_t* inst = ndn_interest_create_packet(&si->block);
    if (inst == NULL) {
	DEBUG("ndn_app: cannot create interest packet snip (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	return -1;
    }

    // add entry to consumer callback table
    if (0 != _add_consumer_cb_entry(handle, si, on_data, on_timeout)) {
	DEBUG("ndn_app: cannot add consumer cb entry (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	ndn_shared_block_release(si);
	gnrc_pktbuf_release(inst);
	return -1;
    }
    // "si" is useless after this point

    // send packet to NDN thread
    if (!gnrc_netapi_dispatch_send(GNRC_NETTYPE_NDN,
				   GNRC_NETREG_DEMUX_CTX_ALL, inst)) {
	DEBUG("ndn_test: cannot send interest to NDN thread (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	//TODO: remove consumer cb entry
	gnrc_pktbuf_release(inst);
	return -1;
    }

    return 0;
}

static _producer_cb_entry_t*
_add_producer_cb_entry(ndn_app_t* handle, ndn_shared_block_t* n,
		       ndn_app_interest_cb_t on_interest)
{
    _producer_cb_entry_t *entry =
	(_producer_cb_entry_t*)malloc(sizeof(_producer_cb_entry_t));
    if (entry == NULL) {
	DEBUG("ndn_app: cannot allocate memory for producer cb entry (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	return NULL;
    }

    entry->prefix = ndn_shared_block_copy(n);
    entry->on_interest = on_interest;

    DL_PREPEND(handle->_pcb_table, entry);
    DEBUG("ndn_app: add producer cb entry (pid=%" PRIkernel_pid ")\n", handle->id);
    return entry;
}

int ndn_app_register_prefix(ndn_app_t* handle, ndn_name_t* name,
			    ndn_app_interest_cb_t on_interest)
{
    if (handle == NULL) return -1;

    ndn_block_t n;
    n.len = ndn_name_total_length(name);
    if (n.len <= 0) return -1;
    n.buf = (const uint8_t*)malloc(n.len);
    if (ndn_name_wire_encode(name, (uint8_t*)n.buf, n.len) <= 0) return -1;

    ndn_shared_block_t* sn = ndn_shared_block_create_by_move(&n);
    if (sn == NULL) {
	DEBUG("ndn_app: cannot create shared block for prefix (pid=%"
	      PRIkernel_pid ")", handle->id);
	free((void*)n.buf);
	return -1;
    }

    _producer_cb_entry_t* entry = _add_producer_cb_entry(handle, sn, on_interest);
    if (entry == NULL) {
	DEBUG("ndn_app: failed to add producer cb entry (pid=%"
	      PRIkernel_pid ")", handle->id);
	ndn_shared_block_release(sn);
	return -1;
    }

    // notify ndn thread to add fib entry
    msg_t add_fib, reply;
    add_fib.type = NDN_APP_MSG_TYPE_ADD_FIB;

    // once received, this pointer will be released by the ndn thread
    add_fib.content.ptr = (void*)sn;

    reply.content.value = 1;
    msg_send_receive(&add_fib, &reply, ndn_pid);
    if (reply.content.value != 0) {
	DEBUG("ndn_app: cannot add fib entry (pid=%"
	      PRIkernel_pid ")\n", handle->id);
	DL_DELETE(handle->_pcb_table, entry);
	ndn_shared_block_release(entry->prefix);
	free(entry);
	return -1;
    }

    return 0;
}

/** @} */
