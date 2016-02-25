/*
 * Copyright (C) 2016 Wentao Shang
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       Demonstrating the sending and receiving of NDN packets.
 *
 * @author      Wentao Shang <wentaoshang@gmail.com>
 *
 * @}
 */

#include <stdio.h>
#include <inttypes.h>

#include "kernel.h"
#include "thread.h"
#include "net/ndn/app.h"
#include "net/ndn/ndn.h"
#include "net/ndn/encoding/name.h"
#include "net/ndn/encoding/interest.h"
#include "net/gnrc/netapi.h"
#include "net/gnrc/netreg.h"
#include "net/gnrc/pktdump.h"
#include "timex.h"
#include "xtimer.h"

static gnrc_netreg_entry_t server = {
    NULL, GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF
};

static int on_timeout(ndn_block_t* interest)
{
    (void)interest;
    printf("consumer: timeout callback received (pid=%"
	   PRIkernel_pid ")\n", thread_getpid());
    printf("consumer: stop the app\n");
    return NDN_APP_STOP;
}

static void run_consumer(void)
{
    ndn_app_t *handle = ndn_app_create();
    if (handle == NULL) {
	printf("consumer: cannot create app handle (pid=%"
	       PRIkernel_pid ")\n", thread_getpid());
    }

    /* build interest packet */
    uint8_t buf[6] = "abcdef";
    ndn_name_component_t comps[4] = {
	{ buf, 1 },
	{ buf + 1, 1 },
	{ buf + 2, 2 },
	{ buf + 4, 2 }
    };
    ndn_name_t name = { 4, comps };  // URI = /a/b/cd/ef
    uint32_t lifetime = 4000;  // 4 sec

    printf("consumer: express interest (pid=%"
	   PRIkernel_pid ")\n", thread_getpid());
    if (ndn_app_express_interest(handle, &name, NULL, lifetime,
				 NULL, on_timeout) != 0) {
	printf("consumer: failed to express interest (pid=%"
	       PRIkernel_pid ")\n", thread_getpid());
	ndn_app_destroy(handle);
	return;
    }
    printf("consumer: interest sent (pid=%"
	   PRIkernel_pid ")\n", thread_getpid());

    printf("consumer: enter app run loop (pid=%"
	   PRIkernel_pid ")\n", thread_getpid());
    ndn_app_run(handle);
    printf("consumer: returned from app run loop (pid=%"
	   PRIkernel_pid ")\n", thread_getpid());
    ndn_app_destroy(handle);
}

static void start_dump(void)
{
    /* check if producer is already running */
    if (server.pid != KERNEL_PID_UNDEF) {
        puts("Error: server already running\n");
        return;
    }

    /* start server (which means registering pktdump for the chosen port) */
    server.pid = gnrc_pktdump_getpid();
    gnrc_netreg_register(GNRC_NETTYPE_NDNAPP, &server);
    puts("Success: started packet dump server");
}

static void stop_dump(void)
{
    /* check if server is running at all */
    if (server.pid == KERNEL_PID_UNDEF) {
        printf("Error: server was not running\n");
        return;
    }
    /* stop server */
    gnrc_netreg_unregister(GNRC_NETTYPE_NDNAPP, &server);
    server.pid = KERNEL_PID_UNDEF;
    puts("Success: stopped packet dump server");
}

int ndn_test(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s [consumer|dump]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "consumer") == 0) {
	run_consumer();
    }
    else if (strcmp(argv[1], "dump") == 0) {
        if (argc < 3) {
            printf("usage: %s dump [start|stop]\n", argv[0]);
            return 1;
        }
        if (strcmp(argv[2], "start") == 0) {
            start_dump();
        }
        else if (strcmp(argv[2], "stop") == 0) {
            stop_dump();
        }
        else {
            puts("error: invalid command");
        }
    }
    else {
        puts("error: invalid command");
    }
    return 0;
}
