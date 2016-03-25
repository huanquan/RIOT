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

    ndn_block_t name;
    int r = ndn_data_get_name(data, &name);
    assert(r == 0);
    printf("client (pid=%" PRIkernel_pid "): data received, name=",
	   handle->id);
    ndn_name_print(&name);
    putchar('\n');

    ndn_block_t content;
    r = ndn_data_get_content(data, &content);
    assert(r == 0);
    assert(content.len == 6);

    printf("client (pid=%" PRIkernel_pid "): content=%02X%02X%02X%02X\n",
	   handle->id, *(content.buf + 2), *(content.buf + 3),
	   *(content.buf + 4), *(content.buf + 5));

    r = ndn_data_verify_signature(data, key, sizeof(key));
    if (r != 0)
	printf("client (pid=%" PRIkernel_pid "): fail to verify signature\n",
	       handle->id);

    return NDN_APP_CONTINUE;
}

static int on_timeout(ndn_block_t* interest)
{
    ndn_block_t name;
    int r = ndn_interest_get_name(interest, &name);
    assert(r == 0);

    printf("client (pid=%" PRIkernel_pid "): interest timeout, name=",
	   handle->id);
    ndn_name_print(&name);
    putchar('\n');

    return NDN_APP_CONTINUE;
}

static uint16_t count = 0;

static int send_interest(void* context)
{
    const char* uri = (const char*)context;

    // stop the app after sending 10 interests
    printf("client (pid=%" PRIkernel_pid "): in sched callback, count=%d\n",
	   handle->id, ++count);
    if (count == 1000) {
	printf("client (pid=%" PRIkernel_pid "): stop the app\n", handle->id);
	return NDN_APP_STOP;
    }

    ndn_shared_block_t* sn = ndn_name_from_uri(uri, strlen(uri));
    if (sn == NULL) {
	printf("client (pid=%" PRIkernel_pid "): cannot create name from uri "
	       "\"%s\"\n", handle->id, uri);
	return NDN_APP_ERROR;
    }

    uint32_t rand = random_uint32();
    ndn_shared_block_t* sin = ndn_name_append_uint32(&sn->block, rand);
    ndn_shared_block_release(sn);
    if (sin == NULL) {
	printf("client (pid=%" PRIkernel_pid "): cannot append component to "
	       "name \"%s\"\n", handle->id, uri);
	return NDN_APP_ERROR;
    }

    uint32_t lifetime = 1000;  // 1 sec

    printf("client (pid=%" PRIkernel_pid "): express interest, name=",
	   handle->id);
    ndn_name_print(&sin->block);
    putchar('\n');

    if (ndn_app_express_interest(handle, &sin->block, NULL, lifetime,
				 on_data, on_timeout) != 0) {
	printf("client (pid=%" PRIkernel_pid "): failed to express interest\n",
	       handle->id);
	ndn_shared_block_release(sin);
	return NDN_APP_ERROR;
    }
    ndn_shared_block_release(sin);

    if (ndn_app_schedule(handle, send_interest, context, 2000000) != 0) {
	printf("client (pid=%" PRIkernel_pid "): cannot schedule next interest"
	       "\n", handle->id);
	return NDN_APP_ERROR;
    }
    printf("client (pid=%" PRIkernel_pid "): schedule next interest in 2 sec"
	   "\n", handle->id);

    return NDN_APP_CONTINUE;
}

static void run_client(const char* uri)
{
    printf("client (pid=%" PRIkernel_pid "): start\n", thread_getpid());

    handle = ndn_app_create();
    if (handle == NULL) {
	printf("client (pid=%" PRIkernel_pid "): cannot create app handle\n",
	       thread_getpid());
	return;
    }

    count = 0;

    if (ndn_app_schedule(handle, send_interest, (void*)uri, 1000000) != 0) {
	printf("client (pid=%" PRIkernel_pid "): cannot schedule first "
	       "interest\n", handle->id);
	ndn_app_destroy(handle);
	return;
    }
    printf("client (pid=%" PRIkernel_pid "): schedule first interest in 1 sec"
	   "\n", handle->id);

    printf("client (pid=%" PRIkernel_pid "): enter app run loop\n",
	   handle->id);

    ndn_app_run(handle);

    printf("client (pid=%" PRIkernel_pid "): returned from app run loop\n",
	   handle->id);

    ndn_app_destroy(handle);
}



static int on_interest(ndn_block_t* interest)
{
    ndn_block_t in;
    if (ndn_interest_get_name(interest, &in) != 0) {
	printf("server (pid=%" PRIkernel_pid "): cannot get name from interest"
	       "\n", handle->id);
	return NDN_APP_ERROR;
    }

    printf("server (pid=%" PRIkernel_pid "): interest received, name=",
	   handle->id);
    ndn_name_print(&in);
    putchar('\n');

    ndn_shared_block_t* sdn = ndn_name_append_uint8(&in, 0);
    if (sdn == NULL) {
	printf("server (pid=%" PRIkernel_pid "): cannot append component to "
	       "name\n", handle->id);
	return NDN_APP_ERROR;
    }

    ndn_metainfo_t meta = { NDN_CONTENT_TYPE_BLOB, -1 };

    uint32_t rand = random_uint32();
    uint8_t* buf = (uint8_t*)(&rand);
    ndn_block_t content = { buf, sizeof(rand) };

    ndn_shared_block_t* sd = ndn_data_create(&sdn->block, &meta, &content,
					     key, sizeof(key));
    if (sd == NULL) {
	printf("server (pid=%" PRIkernel_pid "): cannot create data block\n",
	       handle->id);
	ndn_shared_block_release(sdn);
	return NDN_APP_ERROR;
    }

    printf("server (pid=%" PRIkernel_pid "): send data to NDN thread, name=",
	   handle->id);
    ndn_name_print(&sdn->block);
    putchar('\n');
    ndn_shared_block_release(sdn);

    // pass ownership of "sd" to the API
    if (ndn_app_put_data(handle, sd) != 0) {
	printf("server (pid=%" PRIkernel_pid "): cannot put data\n",
	       handle->id);
	return NDN_APP_ERROR;
    }

    printf("server (pid=%" PRIkernel_pid "): return to the app\n", handle->id);
    return NDN_APP_CONTINUE;
}

static void run_server(const char* prefix)
{
    printf("server (pid=%" PRIkernel_pid "): start\n", thread_getpid());

    handle = ndn_app_create();
    if (handle == NULL) {
	printf("server (pid=%" PRIkernel_pid "): cannot create app handle\n",
	       thread_getpid());
	return;
    }

    ndn_shared_block_t* sp = ndn_name_from_uri(prefix, strlen(prefix));
    if (sp == NULL) {
	printf("server (pid=%" PRIkernel_pid "): cannot create name from uri "
	       "\"%s\"\n", handle->id, prefix);
	return;
    }

    printf("server (pid=%" PRIkernel_pid "): register prefix \"%s\"\n",
	   handle->id, prefix);
    // pass ownership of "sp" to the API
    if (ndn_app_register_prefix(handle, sp, on_interest) != 0) {
	printf("server (pid=%" PRIkernel_pid "): failed to register prefix\n",
	       handle->id);
	ndn_app_destroy(handle);
	return;
    }

    printf("server (pid=%" PRIkernel_pid "): enter app run loop\n",
	   handle->id);

    ndn_app_run(handle);

    printf("server (pid=%" PRIkernel_pid "): returned from app run loop\n",
	   handle->id);

    ndn_app_destroy(handle);
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

	run_server(argv[2]);
    }
    else {
        puts("error: invalid command");
    }
    return 0;
}
