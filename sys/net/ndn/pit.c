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

#include "net/ndn/pit.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

static ndn_pit_entry_t *_pit;

ndn_pit_entry_t* ndn_pit_add(kernel_pid_t face_id, int face_type,
			     ndn_block_t* block, uint32_t timeout)
{
    assert(block != NULL);
    assert(block->buf != NULL);
    assert(block->len > 0);

    ndn_pit_entry_t *entry = (ndn_pit_entry_t*)malloc(sizeof(ndn_pit_entry_t));
    if (entry == NULL) {
	DEBUG("ndn: cannot allocate pit entry\n");
	return NULL;
    }

    uint8_t *buf = (uint8_t*)malloc(block->len);
    if (buf == NULL) {
	free((void*)entry);
	DEBUG("ndn: cannot allocate buffer for interest block\n");
	return NULL;
    }

    entry->prev = entry->next = NULL;
    entry->face_id = face_id;
    entry->face_type = face_type;
    entry->interest.buf = buf;
    entry->interest.len = block->len;
    entry->expire = xtimer_now() + timeout;

    DL_PREPEND(_pit, entry);
    DEBUG("ndn: add new pit entry (face=%u, expire=%u)\n",
	  entry->face_id, entry->expire);
    return entry;
}

void _ndn_pit_release(ndn_pit_entry_t *entry)
{
    assert(_pit != NULL);
    DL_DELETE(_pit, entry);
    xtimer_remove(&entry->timer);
    free((void*)entry->interest.buf);
    free((void*)entry);
}

void ndn_pit_remove(msg_t *msg)
{
    if (_pit == NULL) {
	DEBUG("ndn: pit is empty, skip remove\n");
	return;
    }

    ndn_pit_entry_t *elem, *tmp;
    DL_FOREACH_SAFE(_pit, elem, tmp) {
	if (&elem->timer_msg == msg) {
	    DEBUG("ndn: remove pit entry (face=%u, expire=%u)\n",
		  elem->face_id, elem->expire);
	    _ndn_pit_release(elem);
	}
    }
}

void ndn_pit_init(void)
{
    _pit = NULL;    
}


/** @} */
