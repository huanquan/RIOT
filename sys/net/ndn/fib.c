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
#include "net/ndn/encoding/name.h"

#include "net/ndn/fib.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

static ndn_fib_entry_t *_fib;

static ndn_fib_entry_t* _fib_entry_add_face(ndn_fib_entry_t* entry,
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
		DEBUG("ndn: same face exists in the fib entry\n");
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

int ndn_fib_add(ndn_shared_block_t* prefix, kernel_pid_t face_id,
		int face_type)
{
    assert(prefix != NULL);

    ndn_fib_entry_t *entry;
    DL_FOREACH(_fib, entry) {
	int r =
	    ndn_name_compare_block(&prefix->block, &entry->prefix->block);
	if (r == 0) {
	    // found an entry with identical name
	    ndn_shared_block_release(prefix);
	    // add face to fib entry
	    if (_fib_entry_add_face(entry, face_id, face_type) == NULL) {
		DEBUG("ndn: cannot add face %" PRIkernel_pid
		      " (type=%d) to existing fib entry\n",
		      face_id, face_type);
		return -1;
	    }
	    // we're done
	    return 0;
	} else if (r == -2) {
	    // the prefix to add is a shorter prefix of an existing prefix
	    // the destination face should be added to the existing entry
	    // (aka. child inherit)
	    if (_fib_entry_add_face(entry, face_id, face_type) == NULL) {
		DEBUG("ndn: cannot add face %" PRIkernel_pid
		      " (type=%d) to existing fib entry\n",
		      face_id, face_type);
		return -1;
	    }
	    // continue to check other entries
	}
    }

    // allocate new entry
    entry = (ndn_fib_entry_t*)malloc(sizeof(ndn_fib_entry_t));
    if (entry == NULL) {
	DEBUG("ndn: cannot allocate fib entry\n");
	return -1;
    }

    entry->prefix = prefix;  // move semantics
    entry->plen = ndn_name_get_size_from_block(&prefix->block);
    entry->prev = entry->next = NULL;
    entry->face_list = NULL;
    entry->face_list_size = 0;

    if (NULL == _fib_entry_add_face(entry, face_id, face_type)) {
	ndn_shared_block_release(entry->prefix);
	free(entry);
	return -1;
    }

    DL_PREPEND(_fib, entry);
    DEBUG("ndn: add new fib entry (face=%" PRIkernel_pid ")\n", face_id);
    return 0;
}

ndn_fib_entry_t* ndn_fib_lookup(ndn_block_t* name)
{
    int max_plen = -1;
    ndn_fib_entry_t *entry, *max = NULL;
    DL_FOREACH(_fib, entry) {
	int r =
	    ndn_name_compare_block(&entry->prefix->block, name);
	if (r == 0 || r == -2) {
	    // prefix in this entry matches the name
	    if (entry->plen > max_plen) {
		max_plen = entry->plen;
		max = entry;
	    }
	}
    }
    return max;
}

void ndn_fib_init(void)
{
    _fib = NULL;
}

/** @} */
