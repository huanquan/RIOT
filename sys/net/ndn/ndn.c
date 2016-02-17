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
#include "net/gnrc/netif/hdr.h"
#include "net/gnrc/netreg.h"

#include "net/ndn/ndn.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

#if ENABLE_DEBUG
static char _stack[GNRC_NDN_STACK_SIZE + THREAD_EXTRA_STACKSIZE_PRINTF];
#else
static char _stack[GNRC_NDN_STACK_SIZE];
#endif


kernel_pid_t ndn_pid = KERNEL_PID_UNDEF;

/* handles GNRC_NETAPI_MSG_TYPE_RCV commands */
static void _receive(gnrc_pktsnip_t *pkt);
/* sends packet over the appropriate interface(s) */
static void _send(gnrc_pktsnip_t *pkt);
/* Main event loop for NDN */
static void *_event_loop(void *args);

kernel_pid_t ndn_init(void)
{
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
    reply.content.value = -ENOTSUP;
    reply.type = GNRC_NETAPI_MSG_TYPE_ACK;

    /* start event loop */
    while (1) {
        DEBUG("ndn: waiting for incoming message.\n");
        msg_receive(&msg);

        switch (msg.type) {
            case GNRC_NETAPI_MSG_TYPE_RCV:
                DEBUG("ndn: GNRC_NETAPI_MSG_TYPE_RCV received\n");
                _receive((gnrc_pktsnip_t *)msg.content.ptr);
                break;

            case GNRC_NETAPI_MSG_TYPE_SND:
                DEBUG("ndn: GNRC_NETAPI_MSG_TYPE_SND received\n");
                _send((gnrc_pktsnip_t *)msg.content.ptr);
                break;

            case GNRC_NETAPI_MSG_TYPE_GET:
            case GNRC_NETAPI_MSG_TYPE_SET:
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


static void _send(gnrc_pktsnip_t *pkt)
{
    if (pkt == NULL) return;

    /* ignore any non-NDN packet snip */
    if (pkt->type != GNRC_NETTYPE_NDN) {
	DEBUG("ndn: SND command with unknown packet type\n");
	gnrc_pktbuf_release(pkt);
	return;
    }

    /* get list of interfaces */
    kernel_pid_t ifs[GNRC_NETIF_NUMOF];
    size_t ifnum = gnrc_netif_get(ifs);

    /* throw away packet if no one is interested */
    if (ifnum == 0) {
	DEBUG("ndn: no interfaces registered, dropping packet\n");
	gnrc_pktbuf_release(pkt);
	return;
    }

    /* send to the first available interface */
    gnrc_pktsnip_t *netif;
    kernel_pid_t iface = ifs[0];

    /* allocate interface header */
    netif = gnrc_netif_hdr_build(NULL, 0, NULL, 0);

    if (netif == NULL) {
	DEBUG("ndn: error on interface header allocation, "
	      "dropping packet\n");
	gnrc_pktbuf_release(pkt);
	return;
    }

    /* add interface header to packet */
    LL_PREPEND(pkt, netif);

    /* mark as broadcast */
    ((gnrc_netif_hdr_t *)pkt->data)->flags |= GNRC_NETIF_HDR_FLAGS_BROADCAST;
    ((gnrc_netif_hdr_t *)pkt->data)->if_pid = iface;

    /* check MTU */
    uint16_t mtu;
    if ((gnrc_netapi_get(iface, NETOPT_MAX_PACKET_SIZE, 0, &mtu,
			 sizeof(uint16_t)) >= 0)) {
	if (gnrc_pkt_len(pkt->next) > mtu) {
	    DEBUG("ndn: packet too big\n");
	    gnrc_pktbuf_release(pkt);
	    return;
	}
    }

    /* send to interface */
    if (gnrc_netapi_send(iface, pkt) < 1) {
        DEBUG("ndn: unable to send packet\n");
        gnrc_pktbuf_release(pkt);
    }

    DEBUG("ndn: successfully sent packet\n");
    return;
}

/** @} */
