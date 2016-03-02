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

#include "thread.h"
#include "net/ndn/app.h"
#include "net/ndn/ndn.h"
#include "net/ndn/encoding/name.h"
#include "net/ndn/encoding/interest.h"
#include "net/ndn/msg_type.h"
#include "timex.h"
#include "xtimer.h"


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
    printf("consumer: start (pid=%" PRIkernel_pid ")\n",
	   thread_getpid());

    ndn_app_t *handle = ndn_app_create();
    if (handle == NULL) {
	printf("consumer: cannot create app handle (pid=%"
	       PRIkernel_pid ")\n", thread_getpid());
	return;
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

static kernel_pid_t producer = KERNEL_PID_UNDEF;

static int on_interest(ndn_block_t* interest)
{
    (void)interest;
    printf("producer: interest callback received (pid=%"
	   PRIkernel_pid ")\n", thread_getpid());
    printf("producer: return to the app\n");
    return NDN_APP_CONTINUE;
}

static void run_producer(void)
{
    printf("producer: start (pid=%" PRIkernel_pid ")\n",
	   thread_getpid());

    ndn_app_t *handle = ndn_app_create();
    if (handle == NULL) {
	printf("producer: cannot create app handle (pid=%"
	       PRIkernel_pid ")\n", thread_getpid());
	return;
    }

    /* build interest packet */
    uint8_t buf[] = "ab";
    ndn_name_component_t comps[2] = {
	{ buf, 1 },
	{ buf + 1, 1 },
    };
    ndn_name_t prefix = { 2, comps };  // URI = /a/b

    printf("producer: register prefix /a/b (pid=%"
	   PRIkernel_pid ")\n", thread_getpid());
    if (ndn_app_register_prefix(handle, &prefix, on_interest) != 0) {
	printf("producer: failed to register prefix (pid=%"
	       PRIkernel_pid ")\n", thread_getpid());
	ndn_app_destroy(handle);
	return;
    }

    printf("producer: enter app run loop (pid=%"
	   PRIkernel_pid ")\n", thread_getpid());
    ndn_app_run(handle);
    printf("producer: returned from app run loop (pid=%"
	   PRIkernel_pid ")\n", thread_getpid());
    ndn_app_destroy(handle);
    producer = KERNEL_PID_UNDEF;
    return;
}

static void start_producer(void)
{
    /* check if producer is already running */
    if (producer != KERNEL_PID_UNDEF) {
        printf("producer: already running (pid=%"
	       PRIkernel_pid "\n", producer);
        return;
    }

    /* start producer */
    producer = thread_getpid();
    run_producer();
}

static void stop_producer(void)
{
    /* check if producer is running at all */
    if (producer == KERNEL_PID_UNDEF) {
        printf("producer: not running\n");
        return;
    }

    // send signal to terminate app
    msg_t stop;
    stop.type = NDN_APP_MSG_TYPE_TERMINATE;
    stop.content.value = 0;
    msg_send(&stop, producer);
    printf("producer: stop signal sent to pid %" PRIkernel_pid "\n",
	   producer);
}

int ndn_test(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s [consumer|producer]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "consumer") == 0) {
	run_consumer();
    }
    else if (strcmp(argv[1], "producer") == 0) {
        if (argc < 3) {
            printf("usage: %s producer [start|stop]\n", argv[0]);
            return 1;
        }
        if (strcmp(argv[2], "start") == 0) {
            start_producer();
        }
        else if (strcmp(argv[2], "stop") == 0) {
            stop_producer();
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
