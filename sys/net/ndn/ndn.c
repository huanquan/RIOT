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
#include "net/gnrc/netapi.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/netreg.h"
#include "timex.h"
#include "xtimer.h"

#include "net/ndn/face_table.h"
#include "net/ndn/netif.h"
#include "net/ndn/pit.h"
#include "net/ndn/fib.h"
#include "net/ndn/encoding/interest.h"
#include "net/ndn/msg_type.h"

#include "net/ndn/ndn.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

#if ENABLE_DEBUG
static char _stack[GNRC_NDN_STACK_SIZE + THREAD_EXTRA_STACKSIZE_PRINTF];
#else
static char _stack[GNRC_NDN_STACK_SIZE];
#endif

kernel_pid_t ndn_pid = KERNEL_PID_UNDEF;

/* helper to setup a timer that interrupts the event loop */
void _set_timeout(ndn_pit_entry_t* entry, uint32_t us)
{
    /* set a timer to send a message to ndn thread */
    xtimer_set_msg(&entry->timer, us, &entry->timer_msg, thread_getpid());
}

static void _process_packet(kernel_pid_t face_id, int face_type,
			    gnrc_pktsnip_t *pkt);

/* Main event loop for NDN */
static void *_event_loop(void *args);

kernel_pid_t ndn_init(void)
{
    ndn_face_table_init();
    ndn_fib_init();
    ndn_netif_auto_add();

    ndn_pit_init();
    
    /* check if thread is already running */
    if (ndn_pid == KERNEL_PID_UNDEF) {
        /* start UDP thread */
        ndn_pid = thread_create(
	    _stack, sizeof(_stack), GNRC_NDN_PRIO,
	    THREAD_CREATE_STACKTEST, _event_loop, NULL, "ndn");
    }
    return ndn_pid;
}

static void *_event_loop(void *args)
{
    msg_t msg, reply, msg_q[GNRC_NDN_MSG_QUEUE_SIZE];
    gnrc_netreg_entry_t me_reg;

    (void)args;
    msg_init_queue(msg_q, GNRC_NDN_MSG_QUEUE_SIZE);

    me_reg.demux_ctx = GNRC_NETREG_DEMUX_CTX_ALL;
    me_reg.pid = thread_getpid();

    /* register interest in all NDN packets */
    gnrc_netreg_register(GNRC_NETTYPE_NDN, &me_reg);

    /* preinitialize ACK to GET/SET commands*/
    reply.type = GNRC_NETAPI_MSG_TYPE_ACK;

    /* start event loop */
    while (1) {
        msg_receive(&msg);

        switch (msg.type) {
	    case MSG_XTIMER:
		DEBUG("ndn: XTIMER message received from pid %"
		      PRIkernel_pid "\n", msg.sender_pid);
		ndn_pit_timeout((msg_t*)msg.content.ptr);
		break;

	    case NDN_APP_MSG_TYPE_ADD_FACE:
		DEBUG("ndn: ADD_FACE message received from pid %"
		      PRIkernel_pid "\n", msg.sender_pid);
		if (ndn_face_table_add(
			(kernel_pid_t)msg.content.value, NDN_FACE_APP) != 0) {
		    DEBUG("ndn: failed to add face id %u\n",
			  msg.content.value);
		    reply.content.value = 1;
		} else {
		    reply.content.value = 0;  // indicate success
		}
		msg_reply(&msg, &reply);
		break;

	    case NDN_APP_MSG_TYPE_REMOVE_FACE:
		DEBUG("ndn: REMOVE_FACE message received from pid %"
		      PRIkernel_pid "\n", msg.sender_pid);
		if (ndn_face_table_remove(
			(kernel_pid_t)msg.content.value) != 0) {
		    DEBUG("ndn: failed to remove face id %u\n",
			  msg.content.value);
		    reply.content.value = 1;
		} else {
		    reply.content.value = 0;  // indicate success
		}
		msg_reply(&msg, &reply);
		break;

	    case NDN_APP_MSG_TYPE_ADD_FIB:
		DEBUG("ndn: ADD_FIB message received from pid %"
		      PRIkernel_pid "\n", msg.sender_pid);
		if (ndn_fib_add((ndn_shared_block_t*)msg.content.ptr,
				msg.sender_pid,
				NDN_FACE_APP) != 0) {
		    DEBUG("ndn: failed to add fib entry\n");
		    ndn_shared_block_release(
			(ndn_shared_block_t*)msg.content.ptr);
		    reply.content.value = 1;
		} else {
		    reply.content.value = 0;  // indicate success
		}
		msg_reply(&msg, &reply);
		break;

            case GNRC_NETAPI_MSG_TYPE_RCV:
                DEBUG("ndn: RCV message received from pid %"
		      PRIkernel_pid "\n", msg.sender_pid);
                _process_packet(msg.sender_pid, NDN_FACE_ETH,
				(gnrc_pktsnip_t *)msg.content.ptr);
                break;

            case GNRC_NETAPI_MSG_TYPE_SND:
                DEBUG("ndn: SND message received from pid %"
		      PRIkernel_pid "\n", msg.sender_pid);
                _process_packet(msg.sender_pid, NDN_FACE_APP,
				(gnrc_pktsnip_t *)msg.content.ptr);
                break;

            case GNRC_NETAPI_MSG_TYPE_GET:
            case GNRC_NETAPI_MSG_TYPE_SET:
		reply.content.value = -ENOTSUP;
                msg_reply(&msg, &reply);
                break;
            default:
                break;
        }
    }

    return NULL;
}


static void _send_interest_to_app(kernel_pid_t id,
				  ndn_shared_block_t* interest)
{
    msg_t m;
    m.type = NDN_APP_MSG_TYPE_INTEREST;
    m.content.ptr = (void*)interest;
    if (msg_try_send(&m, id) < 1) {
	DEBUG("ndn: cannot send interest to pid %"
	      PRIkernel_pid "\n", id);
	// release the shared ptr here
	ndn_shared_block_release(interest);
    }
    DEBUG("ndn: interest sent to pid %" PRIkernel_pid "\n", id);
}

static void _process_interest(kernel_pid_t face_id, int face_type,
			      gnrc_pktsnip_t *pkt)
{
    ndn_block_t block;
    if (ndn_block_from_packet(pkt, &block) < 0) {
	DEBUG("ndn: cannot get block from packet\n");
	gnrc_pktbuf_release(pkt);
	return;
    }

    uint32_t lifetime;
    if (0 != ndn_interest_get_lifetime(&block, &lifetime)) {
	DEBUG("ndn: cannot get lifetime from Interest block\n");
	gnrc_pktbuf_release(pkt);
	return;
    }

    if (lifetime > 0x400000) {
	DEBUG("ndn: interest lifetime in us exceeds 32-bit\n");
	gnrc_pktbuf_release(pkt);
	return;
    }

    /* convert lifetime to us */
    lifetime *= MS_IN_USEC;

    /* add to pit table */
    ndn_pit_entry_t *pit_entry = ndn_pit_add(face_id, face_type, &block);
    if (pit_entry == NULL) {
	DEBUG("ndn: cannot add new pit entry\n");
	gnrc_pktbuf_release(pkt);
	return;
    }	

    assert(pit_entry->face_list_size > 0);
    /* set (or reset) the timer */
    _set_timeout(pit_entry, lifetime);

    /* check fib */
    ndn_block_t name;
    if (ndn_interest_get_name(&block, &name) < 0) {
	DEBUG("ndn: cannot get name from interest block\n");
	gnrc_pktbuf_release(pkt);
	return;
    }

    ndn_fib_entry_t* fib_entry = ndn_fib_lookup(&name);
    if (fib_entry == NULL) {
	DEBUG("ndn: no route for interest name, drop packet\n");
	gnrc_pktbuf_release(pkt);
	return;
    }

    /* send to the first available interface */
    //TODO: differet forwarding strategies
    assert(fib_entry->face_list_size > 0);
    assert(fib_entry->face_list != NULL);

    int index;
    for (index = 0; index < fib_entry->face_list_size; ++index) {
	// find the first face that is different from the incoming face
	if (fib_entry->face_list[index].id != face_id)
	    break;
    }
    if (index == fib_entry->face_list_size) {
	DEBUG("ndn: no face available for forwarding\n");
	gnrc_pktbuf_release(pkt);
	return;
    }

    kernel_pid_t iface = fib_entry->face_list[index].id;
    switch (fib_entry->face_list[index].type) {
	case NDN_FACE_ETH:
	    DEBUG("ndn: send to eth face %" PRIkernel_pid "\n", iface);
	    ndn_netif_send(iface, pkt);
	    break;

	case NDN_FACE_APP:
	    DEBUG("ndn: send to app face %" PRIkernel_pid "\n", iface);
	    gnrc_pktbuf_release(pkt);
	    ndn_shared_block_t* si =
		ndn_shared_block_copy(pit_entry->shared_pi);
	    _send_interest_to_app(iface, si);
	    break;

	default:
	    break;
    }

    return;
}

static void _process_data(kernel_pid_t face_id, int face_type,
			  gnrc_pktsnip_t *pkt)
{
    (void)face_id;
    (void)face_type;

    // match data against pit
    ndn_shared_block_t *sd = ndn_pit_match_data(pkt);
    if (sd == NULL) {
	DEBUG("ndn: cannot match data against pit entry\n");
	gnrc_pktbuf_release(pkt);
	return;
    }	
    ndn_shared_block_release(sd);
    gnrc_pktbuf_release(pkt);
}

static void _process_packet(kernel_pid_t face_id, int face_type,
			    gnrc_pktsnip_t *pkt)
{
    if (pkt == NULL) return;

    /* ignore any non-NDN packet snip */
    if (pkt->type != GNRC_NETTYPE_NDN) {
	DEBUG("ndn: SND command with unknown packet type\n");
	gnrc_pktbuf_release(pkt);
	return;
    }

    const uint8_t* buf = (uint8_t*)pkt->data;
    int len = pkt->size;
    uint32_t num;

    if (ndn_block_get_var_number(buf, len, &num) < 0) {
	DEBUG("ndn: cannot read packet type\n");
	gnrc_pktbuf_release(pkt);
	return;
    }

    switch (num) {
        case NDN_TLV_INTEREST:
	    _process_interest(face_id, face_type, pkt);
	    break;
        case NDN_TLV_DATA:
	    _process_data(face_id, face_type, pkt);
	    break;
        default:
	    DEBUG("ndn: unknown packet type\n");
	    gnrc_pktbuf_release(pkt);
	    break;
    }
    return;
}

/** @} */
