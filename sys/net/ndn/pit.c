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

static ndn_pit_entry_t* _pit_entry_add_face(ndn_pit_entry_t* entry,
					    kernel_pid_t id, int type)
{
    if (entry->face_list == NULL) {
	entry->face_list = (_face_list_entry_t*)malloc(sizeof(_face_list_entry_t));
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

ndn_pit_entry_t* ndn_pit_add(kernel_pid_t face_id, int face_type, ndn_block_t* block)
{
    assert(block != NULL);
    assert(block->buf != NULL);
    assert(block->len > 0);

    ndn_block_t name;
    if (0 != ndn_interest_get_name(block, &name)) {
	DEBUG("ndn: cannot get interest name for pit insertion\n");
	return NULL;
    }

    // check for interests with the same name and selectors
    ndn_pit_entry_t *entry;
    DL_FOREACH(_pit, entry) {
	if (0 == memcmp(entry->name.buf, name.buf,
			(entry->name.len < name.len ?
			 entry->name.len : name.len))) {
	    // Found pit entry with the same name
	    //TODO: also check selectors
	    if (NULL ==  _pit_entry_add_face(entry, face_id, face_type))
		return NULL;
	    else {
		DEBUG("ndn: add to existing pit entry (face=%u)\n", face_id);
		// caller need to reset timer after this function returns
		return entry;
	    }
	}
    }

    // no pending entry found, allocate new entry
    entry = (ndn_pit_entry_t*)malloc(sizeof(ndn_pit_entry_t));
    if (entry == NULL) {
	DEBUG("ndn: cannot allocate pit entry\n");
	return NULL;
    }

    uint8_t *buf = (uint8_t*)malloc(name.len);
    if (buf == NULL) {
	free(entry);
	DEBUG("ndn: cannot allocate buffer for name block in pit\n");
	return NULL;
    }
    memcpy(buf, name.buf, name.len);

    entry->prev = entry->next = NULL;
    entry->face_list = NULL;
    entry->face_list_size = 0;
    entry->name.buf = buf;
    entry->name.len = name.len;

    /* initialize the timer */
    entry->timer.target = entry->timer.long_target = 0;

    /* initialize the msg struct */
    entry->timer_msg.type = MSG_XTIMER;
    entry->timer_msg.content.ptr = (char*)(&entry->timer_msg);

    if (NULL == _pit_entry_add_face(entry, face_id, face_type)) {
	free((void*)entry->name.buf);
	free(entry);
	return NULL;
    }

    DL_PREPEND(_pit, entry);
    DEBUG("ndn: add new pit entry (face=%u)\n", face_id);
    return entry;
}

void _ndn_pit_release(ndn_pit_entry_t *entry)
{
    assert(_pit != NULL);
    DL_DELETE(_pit, entry);
    xtimer_remove(&entry->timer);
    free((void*)entry->name.buf);
    free(entry->face_list);
    free(entry);
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
	    DEBUG("ndn: remove pit entry (face_list_size=%d)\n",
		  elem->face_list_size);
	    _ndn_pit_release(elem);
	}
    }
}

void ndn_pit_init(void)
{
    _pit = NULL;    
}


/** @} */
