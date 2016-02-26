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

/* handles GNRC_NETAPI_MSG_TYPE_RCV commands */
static void _receive(gnrc_pktsnip_t *pkt);
/* sends packet over the appropriate interface(s) */
static void _send(kernel_pid_t face_id, int face_type, gnrc_pktsnip_t *pkt);
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
        ndn_pid = thread_create(_stack, sizeof(_stack), GNRC_NDN_PRIO,
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
        DEBUG("ndn: waiting for incoming message\n");
        msg_receive(&msg);

        switch (msg.type) {
	    case MSG_XTIMER:
		DEBUG("ndn: XTIMER message received from pid %" PRIkernel_pid "\n",
		      msg.sender_pid);
		ndn_pit_timeout((msg_t*)msg.content.ptr);
		break;

	    case NDN_APP_MSG_TYPE_ADD_FACE:
		DEBUG("ndn: ADD_FACE message received from pid %" PRIkernel_pid "\n",
		      msg.sender_pid);;
		if (ndn_face_table_add((kernel_pid_t)msg.content.value, NDN_FACE_APP) != 0) {
		    DEBUG("ndn: failed to add face id %u\n", msg.content.value);
		    reply.content.value = 1;
		} else {
		    reply.content.value = 0;  // indicate success
		}
		msg_reply(&msg, &reply);
		break;

	    case NDN_APP_MSG_TYPE_REMOVE_FACE:
		DEBUG("ndn: REMOVE_FACE message received from pid %" PRIkernel_pid "\n",
		      msg.sender_pid);;
		if (ndn_face_table_remove((kernel_pid_t)msg.content.value) != 0) {
		    DEBUG("ndn: failed to remove face id %u\n", msg.content.value);
		    reply.content.value = 1;
		} else {
		    reply.content.value = 0;  // indicate success
		}
		msg_reply(&msg, &reply);
		break;

            case GNRC_NETAPI_MSG_TYPE_RCV:
                DEBUG("ndn: RCV message received from pid %" PRIkernel_pid "\n",
		      msg.sender_pid);
                _receive((gnrc_pktsnip_t *)msg.content.ptr);
                break;

            case GNRC_NETAPI_MSG_TYPE_SND:
                DEBUG("ndn: SND message received from pid %" PRIkernel_pid "\n",
		      msg.sender_pid);
                _send(msg.sender_pid, NDN_FACE_APP,
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


static void _receive(gnrc_pktsnip_t *pkt)
{
    if (pkt == NULL) return;

    /* remove L2 information */
    gnrc_pktsnip_t* netif = gnrc_pktsnip_search_type(pkt, GNRC_NETTYPE_NETIF);
    if (netif != NULL)
	gnrc_pktbuf_remove_snip(pkt, netif);

    if (pkt->type != GNRC_NETTYPE_NDN)
	DEBUG("ndn: incorrect packet type\n");

    DEBUG("ndn: received NDN packet\n");
    /* send payload to receivers */
    if (!gnrc_netapi_dispatch_receive(GNRC_NETTYPE_NDNAPP,
				      GNRC_NETREG_DEMUX_CTX_ALL, pkt)) {
        DEBUG("ndn: unable to forward packet as no one is interested in it\n");
        gnrc_pktbuf_release(pkt);
    }

    return;
}

static void _process_interest(kernel_pid_t face_id, int face_type,
			      gnrc_pktsnip_t *pkt)
{
    ndn_block_t block;
    if (ndn_interest_get_block(pkt, &block) < 0) {
	DEBUG("ndn: cannot get block from interest packet\n");
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
    DEBUG("ndn: found matching fib\n");

    assert(fib_entry->face_list_size > 0);
    assert(fib_entry->face_list != NULL);

    /* send to the first available interface */
    kernel_pid_t iface = fib_entry->face_list[0].id;
    DEBUG("ndn: send to face %" PRIkernel_pid "\n", iface);
    ndn_netif_send(iface, pkt);
    return;
}

static void _send(kernel_pid_t face_id, int face_type, gnrc_pktsnip_t *pkt)
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
        default:
	    DEBUG("ndn: unknown packet type\n");
	    gnrc_pktbuf_release(pkt);
	    break;
    }
    return;
}

/** @} */
