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

#include "utlist.h"
#include "net/ndn/encoding/interest.h"
#include "net/ndn/encoding/data.h"
#include "net/ndn/msg_type.h"
#include "net/ndn/face_table.h"
#include "net/ndn/netif.h"

#include "net/ndn/pit.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

static ndn_pit_entry_t *_pit;

static ndn_pit_entry_t* _pit_entry_add_face(ndn_pit_entry_t* entry,
					    kernel_pid_t id, int type)
{
    if (entry->face_list == NULL) {
	entry->face_list =
	    (_face_list_entry_t*)malloc(sizeof(_face_list_entry_t));
	if (entry->face_list == NULL) {
	    DEBUG("ndn: fail to allocate memory for face list\n");
	    return NULL;
	}
	entry->face_list_size = 1;
	entry->face_list[0].id = id;
	entry->face_list[0].type = type;
	return entry;
    } else {
	// check for existing face entry
	for (int i = 0; i < entry->face_list_size; ++i) {
	    if (entry->face_list[i].id == id) {
		DEBUG("ndn: same interest from same face exists\n");
		return entry;
	    }
	}

	// need to add a new entry to the face list
	_face_list_entry_t *list =
	    (_face_list_entry_t*)realloc(
		entry->face_list,
		(entry->face_list_size + 1) * sizeof(_face_list_entry_t));
	if (list == NULL) {
	    DEBUG("ndn: fail to reallocate memory for face list (size=%d)\n",
		  entry->face_list_size);
	    return NULL;
	}
	entry->face_list = list;
	entry->face_list[entry->face_list_size].id = id;
	entry->face_list[entry->face_list_size].type = type;
	++entry->face_list_size;
	return entry;
    }
}

ndn_pit_entry_t* ndn_pit_add(kernel_pid_t face_id, int face_type,
			     ndn_shared_block_t* si)
{
    assert(si != NULL);
    assert(si->block.buf != NULL);
    assert(si->block.len > 0);

    ndn_block_t name;
    if (0 != ndn_interest_get_name(&si->block, &name)) {
	DEBUG("ndn: cannot get interest name for pit insertion\n");
	return NULL;
    }

    // check for interests with the same name and selectors
    ndn_pit_entry_t *entry;
    DL_FOREACH(_pit, entry) {
	// get and compare name
	ndn_block_t pn;
	int r = ndn_interest_get_name(&entry->shared_pi->block, &pn);
	assert(r == 0);

	if (0 == memcmp(pn.buf, name.buf,
			(pn.len < name.len ? pn.len : name.len))) {
	    // Found pit entry with the same name
	    if (NULL ==  _pit_entry_add_face(entry, face_id, face_type))
		return NULL;
	    else {
		DEBUG("ndn: add to existing pit entry (face=%"
		      PRIkernel_pid ")\n", face_id);
		// caller need to reset timer after this function returns
		return entry;
	    }
	}
	//TODO: also check selectors
    }

    // no pending entry found, allocate new entry
    entry = (ndn_pit_entry_t*)malloc(sizeof(ndn_pit_entry_t));
    if (entry == NULL) {
	DEBUG("ndn: cannot allocate pit entry\n");
	return NULL;
    }

    entry->shared_pi = ndn_shared_block_copy(si);
    entry->prev = entry->next = NULL;
    entry->face_list = NULL;
    entry->face_list_size = 0;

    /* initialize the timer */
    entry->timer.target = entry->timer.long_target = 0;

    /* initialize the msg struct */
    entry->timer_msg.type = MSG_XTIMER;
    entry->timer_msg.content.ptr = (char*)(&entry->timer_msg);

    if (NULL == _pit_entry_add_face(entry, face_id, face_type)) {
	ndn_shared_block_release(entry->shared_pi);
	free(entry);
	return NULL;
    }

    DL_PREPEND(_pit, entry);
    DEBUG("ndn: add new pit entry (face=%" PRIkernel_pid ")\n", face_id);
    return entry;
}

void _ndn_pit_release(ndn_pit_entry_t *entry)
{
    assert(_pit != NULL);
    DL_DELETE(_pit, entry);
    xtimer_remove(&entry->timer);
    ndn_shared_block_release(entry->shared_pi);
    free(entry->face_list);
    free(entry);
}

void ndn_pit_timeout(msg_t *msg)
{
    assert(_pit != NULL);

    ndn_pit_entry_t *elem, *tmp;
    DL_FOREACH_SAFE(_pit, elem, tmp) {
	if (&elem->timer_msg == msg) {
	    DEBUG("ndn: remove pit entry due to timeout (face_list_size=%d)\n",
		  elem->face_list_size);
	    // notify app face, if any
	    msg_t timeout;
	    timeout.type = NDN_APP_MSG_TYPE_TIMEOUT;
	    for (int i = 0; i < elem->face_list_size; ++i) {
		if (elem->face_list[i].type == NDN_FACE_APP) {
		    DEBUG("ndn: try to send timeout message to pid %"
			  PRIkernel_pid "\n", elem->face_list[i].id);
		    timeout.content.ptr =
			(void*)ndn_shared_block_copy(elem->shared_pi);
		    if (msg_try_send(&timeout, elem->face_list[i].id) < 1) {
			DEBUG("ndn: cannot send timeout message to pid %"
			      PRIkernel_pid "\n", elem->face_list[i].id);
			// release the shared ptr here
			ndn_shared_block_release(
			    (ndn_shared_block_t*)timeout.content.ptr);
		    }
		    // message delivered to app thread, which is responsible
		    // for releasing the shared ptr
		}
	    }
	    _ndn_pit_release(elem);
	}
    }
}

static void _send_data_to_app(kernel_pid_t id, ndn_shared_block_t* data)
{
    msg_t m;
    m.type = NDN_APP_MSG_TYPE_DATA;
    m.content.ptr = (void*)data;
    if (msg_try_send(&m, id) < 1) {
	DEBUG("ndn: cannot send data to pid %"
	      PRIkernel_pid "\n", id);
	// release the shared ptr here
	ndn_shared_block_release(data);
    }
    DEBUG("ndn: data sent to pid %" PRIkernel_pid "\n", id);
}

int ndn_pit_match_data(ndn_shared_block_t* sd)
{
    assert(_pit != NULL);
    assert(sd != NULL);

    ndn_block_t name;
    if (0 != ndn_data_get_name(&sd->block, &name)) {
	DEBUG("ndn: cannot get data name for pit matching\n");
	return -1;
    }

    int found = -1;
    ndn_pit_entry_t *entry, *tmp;
    DL_FOREACH_SAFE(_pit, entry, tmp) {
	ndn_block_t pn;
	int r = ndn_interest_get_name(&entry->shared_pi->block, &pn);
	assert(r == 0);

	r = ndn_name_compare_block(&pn, &name);
	if (r == -2 || r == 0) {
	    // either pn is a prefix of name, or they are the same
	    found = 0;

	    DL_DELETE(_pit, entry);
	    xtimer_remove(&entry->timer);

	    for (int i = 0; i < entry->face_list_size; ++i) {
		kernel_pid_t iface = entry->face_list[i].id;
		switch (entry->face_list[i].type) {
		    case NDN_FACE_ETH:
			DEBUG("ndn: send to eth face %"
			      PRIkernel_pid "\n", iface);
			ndn_netif_send(iface, &sd->block);
			break;

		    case NDN_FACE_APP:
			DEBUG("ndn: send to app face %"
			      PRIkernel_pid "\n", iface);
			ndn_shared_block_t* ssd = ndn_shared_block_copy(sd);
			_send_data_to_app(iface, ssd);
			break;

		    default:
			break;
		}
	    }

	    ndn_shared_block_release(entry->shared_pi);
	    free(entry->face_list);
	    free(entry);
	}
    }
    return found;
}

void ndn_pit_init(void)
{
    _pit = NULL;    
}


/** @} */
