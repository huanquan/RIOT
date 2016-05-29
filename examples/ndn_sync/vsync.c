#include "config.h"
#include "ndn_sync.h"

#define MIN(a,b) ((a) < (b)? (a): (b))
#define PUBLICATION_LIST_CAPACITY (20)

/****** Storing vsync node state ******/ 
static const char* data_pfx[2] = {"alice", "bob"};
static ndn_app_t* handle = NULL;
static ndn_sync_t node;
typedef struct {
    const uint8_t* buf;
    size_t len;
    size_t current;
    size_t per_pkt;
} publish_context_t;
static publish_context_t article;
// Cache of published data
static struct {
    vn_t idx;
    ndn_shared_block_t* data;
} publication_list[PUBLICATION_LIST_CAPACITY];

static void _publication_list_init(void)
{
    for (size_t i = 0; i < PUBLICATION_LIST_CAPACITY; i++) {
        publication_list[i].data = NULL;
    }
}

static size_t _publication_list_search(uint32_t rn, uint8_t sn)
{
    for (size_t i = 0; i < PUBLICATION_LIST_CAPACITY; i++) {
        if (publication_list[i].data != NULL) {
            if (publication_list[i].idx.rn == rn && publication_list[i].idx.sn == sn) {
                return i;
            }
        }
    }
    return PUBLICATION_LIST_CAPACITY;   // failure
}

static size_t _publication_list_insert(uint32_t rn, uint8_t sn,
                                       ndn_shared_block_t* data)
{
    for (size_t i = 0; i < PUBLICATION_LIST_CAPACITY; i++) {
        if (publication_list[i].data == NULL) {
            publication_list[i].idx.rn = rn;
            publication_list[i].idx.sn = sn;
            publication_list[i].data = data;
            return i;
        } else {
            if (publication_list[i].idx.rn == rn && publication_list[i].idx.sn == sn) {
                assert(publication_list[i].data == data);
                return i;
            }
        }
    }
    return PUBLICATION_LIST_CAPACITY;
}

static int _on_sync_interest(ndn_block_t* interest)
{
    int retval = ndn_sync_process_sync_interest(handle, &node, interest);
    if (retval == 0) {
        ndn_block_t in;
        ndn_interest_get_name(interest, &in);
        printf("vsync (pid=%" PRIkernel_pid "): interest received, name=",
               handle->id);
        ndn_name_print(&in);
        putchar('\n');
        return NDN_APP_CONTINUE;
    } else {
        printf("vsync (pid=%" PRIkernel_pid "): process_sync_interest returns %d\n",
               handle->id, retval);
        return NDN_APP_ERROR;
    }
}

static int _parse_wtf_interest(ndn_block_t* interest, ndn_block_t* name,
                               uint32_t* rn, uint8_t* sn)
{
    // get name
    assert(name != NULL && rn != NULL && sn != NULL);
    if (ndn_interest_get_name(interest, name) != 0) {
        return 1;
    }

    // get round number
    ndn_name_component_t tmp;
    if (ndn_name_get_component_from_block(name, 1, &tmp) < 0) {
        return 2;
    }
    if (tmp.len != sizeof(uint32_t)) {
        return 3;
    }
    memcpy(rn, tmp.buf, tmp.len);
    tmp.buf = NULL; // we dont own it, so it's okay to reset it
    
    // get seq number
    if (ndn_name_get_component_from_block(name, 2, &tmp) < 0) {
        return 4;
    }
    if (tmp.len != sizeof(uint8_t)) {
        return 5;
    }
    memcpy(sn, tmp.buf, tmp.len);

    return 0;
}

static int _on_wtf_interest(ndn_block_t* interest)
{
    ndn_block_t name;
    uint32_t rn;
    uint8_t sn;
    int retval = _parse_wtf_interest(interest, &name, &rn, &sn);
    if (retval != 0) {
        printf("vsync (pid=%" PRIkernel_pid "): parse wtf interest failed with %d\n",
               handle->id, retval);
        return NDN_APP_ERROR;
    }

    printf("vsync (pid=%" PRIkernel_pid "): interest received, name=",
           handle->id);
    ndn_name_print(&name);
    printf(", rn=%u, sn=%u\n", rn, (uint32_t) sn);

    size_t k = _publication_list_search(rn, sn);
    if (k == PUBLICATION_LIST_CAPACITY) {
        printf("vsync (pid=%" PRIkernel_pid "): corresponding data pkt not found\n",
               handle->id);
        return NDN_APP_ERROR;
    }

    ndn_shared_block_t* d = ndn_shared_block_copy(publication_list[k].data);
    // pass ownership of "d" to the API
    if (ndn_app_put_data(handle, d) != 0) {
        printf("vsync (pid=%" PRIkernel_pid "): cannot put data\n",
               handle->id);
        return NDN_APP_ERROR;
    }

    return NDN_APP_CONTINUE;
}

static int _publish_data(void* context)
{
    publish_context_t* pcontext = (publish_context_t*) context;
    if (pcontext->current >= pcontext->len) {
        printf("vsync (pid=%" PRIkernel_pid "): all data published\n",
               handle->id);
        return NDN_APP_CONTINUE;
    }

    size_t data_size = MIN(pcontext->per_pkt, pcontext->len - pcontext->current);
    ndn_block_t content = { pcontext->buf + pcontext->current, data_size };
    ndn_metainfo_t meta = { NDN_CONTENT_TYPE_BLOB, -1 };
    ndn_shared_block_t* d = ndn_sync_publish_data(handle, &node, &meta, 
                                                  &content);
    if (d == NULL) {
        printf("vsync (pid=%" PRIkernel_pid "): publish_data returns NULL\n",
               handle->id);
        return NDN_APP_ERROR;
    }
    _publication_list_insert(node.rn, node.vv[node.idx], d);
    pcontext->current += data_size;

    // Schedule next publish
    if (ndn_app_schedule(handle, _publish_data, context, 2000000) != 0) {
        printf("vsync (pid=%" PRIkernel_pid "): cannot schedule next interest\n",
               handle->id);
        return NDN_APP_ERROR;
    }
    printf("vsync (pid=%" PRIkernel_pid "): schedule next interest in 2 sec\n",
           handle->id);
    return NDN_APP_CONTINUE;
}

static void run_vsync(void* publish_context)
{
    printf("vsync (pid=%" PRIkernel_pid "): start\n", thread_getpid());

    handle = ndn_app_create();
    if (handle == NULL) {
        printf("vsync (pid=%" PRIkernel_pid "): cannot create app handle\n",
               thread_getpid());
        return;
    }

    // 1. Register data prefix
    ndn_shared_block_t* dp = ndn_sync_get_data_prefix(&node, node.idx);

    printf("vsync (pid=%" PRIkernel_pid "): register prefix \"", handle->id);
    ndn_name_print(&(dp->block));
    printf("\"\n");
    // pass ownership of "dp" to the API
    if (ndn_app_register_prefix(handle, dp, _on_sync_interest) != 0) {
        printf("vsync (pid=%" PRIkernel_pid "): failed to register prefix\n",
               handle->id);
        ndn_app_destroy(handle);
        return;
    }
    dp = NULL;

    // 2. Register sync prefix
    ndn_shared_block_t* sp = ndn_sync_get_sync_prefix();
    if (sp == NULL) {
        printf("vsync (pid=%" PRIkernel_pid "): cannot get sync prefix\n",
               handle->id);
        return;
    }

    printf("vsync (pid=%" PRIkernel_pid "): register prefix \"", handle->id);
    ndn_name_print(&(sp->block));
    printf("\"\n");
    // pass ownership of "sp" to the API
    if (ndn_app_register_prefix(handle, sp, _on_wtf_interest) != 0) {
        printf("vsync (pid=%" PRIkernel_pid "): failed to register prefix\n",
               handle->id);
        ndn_app_destroy(handle);
        return;
    }
    sp = NULL;

    // 3. Schedule publication of data (only alice has this step)
    if (node.idx == 0) {
        if (ndn_app_schedule(handle, _publish_data, publish_context, 1000000) != 0) {
            printf("vsync (pid=%" PRIkernel_pid "): cannot schedule first "
                   "interest\n", handle->id);
            ndn_app_destroy(handle);
            return;
        }
        printf("vsync (pid=%" PRIkernel_pid "): schedule first interest in 1 sec\n",
               handle->id);
    }

    printf("vsync (pid=%" PRIkernel_pid "): enter app run loop\n",
           handle->id);

    ndn_app_run(handle);

    printf("vsync (pid=%" PRIkernel_pid "): returned from app run loop\n",
           handle->id);

    ndn_app_destroy(handle);
}

int vsync(int argc, char **argv)
{
    if (argc >= 2) {
        printf("usage: %s\n", argv[0]);
        return 1;
    }

    printf("Node idx=%d\n", sysconfig.id);
    assert(sysconfig.id < 2);

    ndn_sync_init_state(&node, sysconfig.id, 2);
    for (size_t i = 0; i < 2; i++) {
        node.pfx[i].buf = (uint8_t*) data_pfx[i];
        node.pfx[i].len = strlen(data_pfx[i]);
    }
    _publication_list_init();

    const char* s = "Soldiers. Scientists. Adventurers. Oddities. In a time of "
                  "global crisis, an international task force of heroes banded "
                  "together to restore peace to a war-torn world: OVERWATCH."
                  "Overwatch ended the crisis, and helped maintain peace in "
                  "the decades that followed, inspiring an era of exploration, "
                  "innovation, and discovery. But, after many years, Overwatch"
                  "'s influence waned, and it was eventually disbanded. Now, "
                  "conflict is rising across the world again, and the call has "
                  "gone out to heroes old and new. Are you with us?";
    article.buf = (uint8_t*) s;
    article.len = strlen(s);
    article.current = 0;
    article.per_pkt = 60;

    run_vsync((void*) &article);

    return 0;
}
