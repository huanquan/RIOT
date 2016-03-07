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
#include "net/netopt.h"
#include "net/netdev2.h"
#include "net/gnrc/netapi.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/netif/hdr.h"
#include "net/gnrc/netreg.h"
#include "net/ndn/face_table.h"
#include "net/ndn/fib.h"

#include "net/ndn/netif.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

static ndn_netif_t _netif_table[GNRC_NETIF_NUMOF];

void ndn_netif_auto_add(void)
{
    /* initialize the netif table entry */
    for (int i = 0; i < GNRC_NETIF_NUMOF; ++i) {
	_netif_table[i].iface = KERNEL_PID_UNDEF;
    }

    /* get list of interfaces */
    kernel_pid_t ifs[GNRC_NETIF_NUMOF];
    size_t ifnum = gnrc_netif_get(ifs);

    if (ifnum == 0) {
	DEBUG("ndn: no interfaces registered, cannot add netif\n");
	return;
    }

    for (int i = 0; i < GNRC_NETIF_NUMOF; ++i) {
	kernel_pid_t iface = ifs[i];

	/* get device type */
	if ((gnrc_netapi_get(iface, NETOPT_DEVICE_TYPE, 0,
			     &_netif_table[i].dev_type,
			     sizeof(uint16_t)) < 0)) {
	    DEBUG("ndn: cannot get device type (pid=%"
		  PRIkernel_pid ")\n", iface);
	    continue;
	}

	/* get device mtu */
	if ((gnrc_netapi_get(iface, NETOPT_MAX_PACKET_SIZE, 0,
			     &_netif_table[i].mtu,
			     sizeof(uint16_t)) < 0)) {
	    DEBUG("ndn: cannot get device mtu (pid=%"
		  PRIkernel_pid ")\n", iface);
	    continue;
	}
	
	if (_netif_table[i].dev_type == NETDEV2_TYPE_ETHERNET) {
	    _netif_table[i].iface = iface;
	    if (ndn_face_table_add(iface, NDN_FACE_ETH) == 0) {
		DEBUG("ndn: add ethernet device (pid=%"
		      PRIkernel_pid ") into face table\n", iface);
		// add default route for this face
		uint8_t buf[] = { NDN_TLV_NAME, 0 };
		ndn_block_t empty = { buf, sizeof(buf) }; // URI = /
		ndn_shared_block_t* shared = ndn_shared_block_create(&empty);
		if (shared != NULL
		    && ndn_fib_add(shared, iface, NDN_FACE_ETH) == 0) {
		    DEBUG("ndn: default route added for ethernet device\n");
		}
	    }
	    else {
		DEBUG("ndn: failed to add ethernet device (pid=%"
		      PRIkernel_pid ") into face table\n", iface);
	    }
	} /* ignore other types of devices for now */
    }
}

/* helper function to find the netif entry by pid */
static ndn_netif_t* _ndn_netif_find(kernel_pid_t iface)
{
    if (iface == KERNEL_PID_UNDEF) return NULL;

    for (int i = 0; i < GNRC_NETIF_NUMOF; ++i) {
	if (_netif_table[i].iface == iface)
	    return &_netif_table[i];
    }
    return NULL;
}

int ndn_netif_send(kernel_pid_t iface, ndn_block_t* block)
{
    assert(block != NULL);
    assert(block->buf != NULL);
    assert(block->len > 0);

    ndn_netif_t* netif = _ndn_netif_find(iface);
    if (netif == NULL) {
	DEBUG("ndn: no such network device (iface=%" PRIkernel_pid ")", iface);
	return -1;
    }

    /* check mtu */
    if (block->len > netif->mtu) {
	DEBUG("ndn: packet size (%u) exceeds device mtu (iface=%"
	      PRIkernel_pid ")\n", block->len, iface);
	return -1;
    }

    gnrc_pktsnip_t* pkt = ndn_block_create_packet(block);
    if (pkt == NULL) {
	DEBUG("ndn: cannot create packet during sending (iface=%"
	      PRIkernel_pid ")\n", iface);
	return -1;
    }

    /* allocate interface header */
    gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, NULL, 0);

    if (netif_hdr == NULL) {
	DEBUG("ndn: error on interface header allocation, dropping packet\n");
	gnrc_pktbuf_release(pkt);
	return -1;
    }

    /* add interface header to packet */
    LL_PREPEND(pkt, netif_hdr);

    /* mark as broadcast */
    ((gnrc_netif_hdr_t *)pkt->data)->flags |= GNRC_NETIF_HDR_FLAGS_BROADCAST;
    ((gnrc_netif_hdr_t *)pkt->data)->if_pid = iface;

    /* send to interface */
    if (gnrc_netapi_send(iface, pkt) < 1) {
        DEBUG("ndn: failed to send packet (iface=%" PRIkernel_pid ")\n", iface);
        gnrc_pktbuf_release(pkt);
	return -1;
    }

    DEBUG("ndn: successfully sent packet (iface=%" PRIkernel_pid ")\n", iface);
    return 0;
}

/** @} */
