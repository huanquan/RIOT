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
 * @brief       NDN ping client and server implemetation
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
#include "net/ndn/encoding/data.h"
#include "net/ndn/msg_type.h"
#include "random.h"

static ndn_app_t* handle = NULL;

static const unsigned char key[] = { 'd', 'u', 'm', 'm', 'y', 'k', 'e', 'y' };

static int on_data(ndn_block_t* interest, ndn_block_t* data)
{
    (void)interest;

    printf("client: in data callback (pid=%"
	   PRIkernel_pid ")\n", handle->id);

    ndn_block_t content;
    int r = ndn_data_get_content(data, &content);
    assert(r == 0);
    assert(content.len == 6);

    printf("client: content = %u\n", *((uint32_t*)(content.buf + 2)));

    r = ndn_data_verify_signature(data, key, sizeof(key));
    if (r != 0)
	printf("client: failed to verify signature\n");

    return NDN_APP_CONTINUE;
}

static int on_timeout(ndn_block_t* interest)
{
    (void)interest;
    printf("client: in timeout callback (pid=%"
	   PRIkernel_pid ")\n", handle->id);
    return NDN_APP_CONTINUE;
}

static int count = 0;

static int send_interest(void* context)
{
    const char* uri = (const char*)context;
    printf("client: in sched callback (pid=%"
	   PRIkernel_pid ")\n", handle->id);

    // stop the app after sending 10 interests
    printf("client: count=%d (pid=%" PRIkernel_pid ")\n",
	   ++count, handle->id);
    if (count == 10) {
	printf("client: stop the app (pid=%"
	       PRIkernel_pid ")\n", handle->id);
	return NDN_APP_STOP;
    }

    ndn_shared_block_t* sn = ndn_name_from_uri(uri, strlen(uri));
    if (sn == NULL) {
	printf("client: cannot create name from uri \"%s\" (pid=%"
	       PRIkernel_pid ")\n", uri, thread_getpid());
	return NDN_APP_ERROR;
    }

    ndn_shared_block_t* sin = ndn_name_append(&sn->block, (uint8_t*)(&count),
					      sizeof(count));
    if (sin == NULL) {
	printf("client: cannot append component to name \"%s\" (pid=%"
	       PRIkernel_pid ")\n", uri, thread_getpid());
	ndn_shared_block_release(sn);
	return NDN_APP_ERROR;
    }
    ndn_shared_block_release(sn);

    uint32_t lifetime = 1000;  // 1 sec

    printf("client: express interest (pid=%"
	   PRIkernel_pid ")\n", handle->id);
    if (ndn_app_express_interest(handle, &sin->block, NULL, lifetime,
				 on_data, on_timeout) != 0) {
	printf("client: failed to express interest (pid=%"
	       PRIkernel_pid ")\n", handle->id);
	ndn_shared_block_release(sn);
	return NDN_APP_ERROR;
    }

    if (ndn_app_schedule(handle, send_interest, context, 2000000) != 0) {
	printf("client: cannot schedule next interest (pid=%"
	       PRIkernel_pid ")\n", handle->id);
	ndn_shared_block_release(sn);
	return NDN_APP_ERROR;
    }
    printf("client: schedule next interest in 2 sec (pid=%"
	   PRIkernel_pid ")\n", handle->id);

    ndn_shared_block_release(sin);
    return NDN_APP_CONTINUE;
}

static void run_client(const char* uri)
{
    printf("client: start (pid=%" PRIkernel_pid ")\n", thread_getpid());

    handle = ndn_app_create();
    if (handle == NULL) {
	printf("client: cannot create app handle (pid=%"
	       PRIkernel_pid ")\n", thread_getpid());
	return;
    }

    if (ndn_app_schedule(handle, send_interest, (void*)uri, 1000000) != 0) {
	printf("client: cannot schedule first interest (pid=%"
	       PRIkernel_pid ")\n", handle->id);
	ndn_app_destroy(handle);
	return;
    }
    printf("client: schedule first interest in 1 sec (pid=%"
	   PRIkernel_pid ")\n", handle->id);

    printf("client: enter app run loop (pid=%"
	   PRIkernel_pid ")\n", handle->id);

    ndn_app_run(handle);

    printf("client: returned from app run loop (pid=%"
	   PRIkernel_pid ")\n", handle->id);

    ndn_app_destroy(handle);
}



static kernel_pid_t server = KERNEL_PID_UNDEF;

static int on_interest(ndn_block_t* interest)
{
    printf("server: interest callback received (pid=%"
	   PRIkernel_pid ")\n", handle->id);

    ndn_block_t in;
    if (ndn_interest_get_name(interest, &in) != 0) {
	printf("server: cannot get name from interest (pid=%"
	       PRIkernel_pid ")\n", handle->id);
	return NDN_APP_ERROR;
    }

    uint32_t rand = random_uint32();
    uint8_t* buf = (uint8_t*)(&rand);
    ndn_shared_block_t* sdn = ndn_name_append(&in, buf, sizeof(rand));
    if (sdn == NULL) {
	printf("server: cannot append component to name (pid=%"
	       PRIkernel_pid ")\n", handle->id);
	return NDN_APP_ERROR;
    }

    ndn_metainfo_t meta = { NDN_CONTENT_TYPE_BLOB, -1 };

    ndn_block_t content = { buf, sizeof(rand) };

    ndn_shared_block_t* sd = ndn_data_create(&sdn->block, &meta, &content,
					     key, sizeof(key));
    if (sd == NULL) {
	printf("server: failed to create data block (pid=%"
	       PRIkernel_pid ")\n", handle->id);
	return NDN_APP_ERROR;
    }

    printf("server: send data to NDN thread (pid=%"
	   PRIkernel_pid ")\n", handle->id);
    if (ndn_app_put_data(handle, sd) != 0) {
	printf("server: failed to put data (pid=%"
	   PRIkernel_pid ")\n", handle->id);
	return NDN_APP_ERROR;
    }

    printf("server: return to the app\n");
    return NDN_APP_CONTINUE;
}

static void run_server(const char* prefix)
{
    printf("server: start (pid=%" PRIkernel_pid ")\n", thread_getpid());

    handle = ndn_app_create();
    if (handle == NULL) {
	printf("server: cannot create app handle (pid=%"
	       PRIkernel_pid ")\n", thread_getpid());
	return;
    }

    ndn_shared_block_t* sp = ndn_name_from_uri(prefix, strlen(prefix));
    if (sp == NULL) {
	printf("server: cannot create name from uri \"%s\" (pid=%"
	       PRIkernel_pid ")\n", prefix, thread_getpid());
	return;
    }

    printf("server: register prefix \"%s\" (pid=%"
	   PRIkernel_pid ")\n", prefix, handle->id);
    // pass ownership of "sp" to the API
    if (ndn_app_register_prefix(handle, sp, on_interest) != 0) {
	printf("server: failed to register prefix (pid=%"
	       PRIkernel_pid ")\n", handle->id);
	ndn_app_destroy(handle);
	return;
    }

    printf("server: enter app run loop (pid=%"
	   PRIkernel_pid ")\n", handle->id);

    ndn_app_run(handle);

    printf("server: returned from app run loop (pid=%"
	   PRIkernel_pid ")\n", handle->id);

    ndn_app_destroy(handle);
    server = KERNEL_PID_UNDEF;
    return;
}

static void start_server(const char* prefix)
{
    /* check if server is already running */
    if (server != KERNEL_PID_UNDEF) {
        printf("server: already running (pid=%"
	       PRIkernel_pid "\n", server);
        return;
    }

    /* start server */
    server = thread_getpid();
    run_server(prefix);
}

int ndn_ping(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s [client|server]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "client") == 0) {
	if (argc < 3) {
            printf("usage: %s client _name_uri_\n", argv[0]);
            return 1;
        }

	run_client(argv[2]);
    }
    else if (strcmp(argv[1], "server") == 0) {
        if (argc < 3) {
            printf("usage: %s server _prefix_\n", argv[0]);
            return 1;
        }

	start_server(argv[2]);
    }
    else {
        puts("error: invalid command");
    }
    return 0;
}
