#include "config.h"
#include "ndn_sync.h"

#include <byteorder.h>
#include <unistd.h>

#define MIN(a,b) ((a) < (b)? (a): (b))
#define PUBLICATION_LIST_CAPACITY (200)
#define NUM_NODES (3)


/****** Storing vsync node state ******/ 
static ndn_app_t* handle = NULL;
static ndn_sync_t node;
static ndn_sync_log_t node_sync_log;
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


static int _on_data_interest_timeout(ndn_block_t* interest);

static int _on_data(ndn_block_t* interest, ndn_block_t* data)
{
    assert(interest != NULL);   // just to make compiler happy
    ndn_block_t content;
    int retval = ndn_sync_process_data(handle, &node, data, &content, _on_data, _on_data_interest_timeout);
    if (retval == 0) {
        ndn_block_t dn;
        ndn_data_get_name(data, &dn);
        printf("vsync (pid=%" PRIkernel_pid "): data received (",
               handle->id);
        ndn_name_print(&dn);
        printf(") -> \"%.*s\"\n", (int) content.len, content.buf);
        return NDN_APP_CONTINUE;
    } else {
        printf("vsync (pid=%" PRIkernel_pid "): process_data returns %d\n",
               handle->id, retval);
        return NDN_APP_ERROR;
    }
}


static int _on_data_interest_timeout(ndn_block_t* interest)
{
    ndn_block_t name;
    int r = ndn_interest_get_name(interest, &name);
    assert(r == 0);

    printf("vsync (pid=%" PRIkernel_pid "): interest timeout, name=",
           handle->id);
    ndn_name_print(&name);
    printf(", resending...\n");

    if (ndn_app_express_interest(handle, &name, NULL, 20 * TIME_SEC, _on_data, _on_data_interest_timeout) < 0) {
        printf("vsync (pid=%" PRIkernel_pid "): cannot resend.\n",
               handle->id);
        return NDN_APP_ERROR;
    }

    return NDN_APP_CONTINUE;
}


static int _on_sync_interest(ndn_block_t* interest)
{
    int retval = ndn_sync_process_sync_interest(handle, &node, interest, _on_data, _on_data_interest_timeout);
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
    uint32_t network_rn;
    memcpy(&network_rn, tmp.buf, tmp.len);
    *rn = NTOHL(network_rn);
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
        return NDN_APP_CONTINUE;
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
    size_t retval = _publication_list_insert(node.rn, node.vv[node.idx], d);
    if (retval == PUBLICATION_LIST_CAPACITY) {
        printf("vsync (pid=%" PRIkernel_pid "): local buffer overflows. "
               "INCREASE IT NOW!!!\n", handle->id);
        return NDN_APP_ERROR;
    }
    pcontext->current += data_size;

    printf("vsync (pid=%" PRIkernel_pid "): publish data (%u, %u)\n",
           handle->id, node.rn, (uint32_t) node.vv[node.idx]);

    // Schedule next publishing
    if (ndn_app_schedule(handle, _publish_data, context, 500000) != 0) {
        printf("vsync (pid=%" PRIkernel_pid "): cannot schedule next publishing\n",
               handle->id);
        return NDN_APP_ERROR;
    }
    printf("vsync (pid=%" PRIkernel_pid "): schedule next publishing in 0.5 sec\n",
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
    if (ndn_app_register_prefix(handle, dp, _on_wtf_interest) != 0) {
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
    if (ndn_app_register_prefix(handle, sp, _on_sync_interest) != 0) {
        printf("vsync (pid=%" PRIkernel_pid "): failed to register prefix\n",
               handle->id);
        ndn_app_destroy(handle);
        return;
    }
    sp = NULL;

    // 3. Schedule publication of data (only alice has this step)
    if (ndn_app_schedule(handle, _publish_data, publish_context, 1000000) != 0) {
        printf("vsync (pid=%" PRIkernel_pid "): cannot schedule first "
               "interest\n", handle->id);
        ndn_app_destroy(handle);
        return;
    }
    printf("vsync (pid=%" PRIkernel_pid "): schedule first interest in 1 sec\n",
           handle->id);
    printf("vsync (pid=%" PRIkernel_pid "): enter app run loop\n",
           handle->id);

    ndn_app_run(handle);

    printf("vsync (pid=%" PRIkernel_pid "): returned from app run loop\n",
           handle->id);

    ndn_app_destroy(handle);
}


int vsync(int argc, char **argv)
{
    static const char* data_pfx[NUM_NODES] = {"alice", "bob", "chen"};
    static const char* s[NUM_NODES] = {
        "Soldiers. Scientists. Adventurers. Oddities. In a time of "
        "global crisis, an international task force of heroes banded "
        "together to restore peace to a war-torn world: OVERWATCH. "
        "Overwatch ended the crisis, and helped maintain peace in "
        "the decades that followed, inspiring an era of exploration, "
        "innovation, and discovery. But, after many years, Overwatch"
        "'s influence waned, and it was eventually disbanded. Now, "
        "conflict is rising across the world again, and the call has "
        "gone out to heroes old and new. Are you with us?",
        "Overwatch is set in a futuristic Earth, sixty years into "
        "the future. Thirty years ago, robots took over the world, "
        "leading to the formation of an international taskforce "
        "called Overwatch, which ended the war in mankind's favor. "
        "Overwatch was disbanded thirty years after its formation, "
        "and in the five years since, the world has become a darker "
        "place. However, a new threat is looming, and heroes will "
        "have to band together to defeat it. Multiple factions "
        "exist in the world, operating in various shades of gray.",
        "Once a frontline combatant in the devastating Omnic Crisis, "
        "this curious Bastion unit now explores the world, fascinated "
        "by nature but wary of a fearful humanity. Originally created "
        "for peacekeeping purposes, Bastion robot units possessed the "
        "unique ability to rapidly reconfigure themselves into an "
        "assault-cannon mode. But during the Omnic Crisis, they were "
        "turned against their human makers, forming the bulk of the "
        "omnics' rebel army. Following the resolution of the crisis, "
        "nearly all of them were destroyed or disassembled. To this "
        "day, Bastion units still symbolize the horrors of the "
        "conflict. One unique Bastion unit, severely damaged in the "
        "final battles of the war, was left forgotten for over a "
        "decade. It lay dormant, exposed to the elements and rusting "
        "while nature slowly reclaimed it. Overgrown with vines and "
        "roots and nested upon by small animals, the robot sat inert, "
        "seemingly unaware of the passing of time. That was until one "
        "fateful day, when it unexpectedly reactivated. With its "
        "combat programming all but lost, it instead displayed an "
        "intense curiosity about the natural world and its "
        "inhabitants. This inquisitive Bastion unit set out to explore "
        "its surroundings and discover its purpose on a war-ravaged "
        "planet. Though \"Bastion\" appears to be gentle -- even "
        "harmless, at times -- its core combat programming takes over "
        "when the unit senses danger, utilizing its entire arsenal to "
        "eliminate anything it perceives as a threat. This has led to "
        "instances of conflict with the few humans it has encountered, "
        "and has driven it to avoid populated areas in favor of the "
        "wild, uncharted regions of the world."
    };

    if (argc >= 2) {
        printf("usage: %s\n", argv[0]);
        return 1;
    }

    uint32_t node_idx = (uint32_t) sysconfig.id;
    printf("Node idx=%d\n", node_idx);
    assert(node_idx < NUM_NODES);

    ndn_sync_init_state(&node, node_idx, NUM_NODES);
    ndn_sync_set_init_log(&node, &node_sync_log);
    for (size_t i = 0; i < NUM_NODES; i++) {
        node.pfx[i].buf = (uint8_t*) data_pfx[i];
        node.pfx[i].len = strlen(data_pfx[i]);
    }
    _publication_list_init();

    article.buf = (uint8_t*) s[node_idx];
    article.len = strlen(s[node_idx]);
    article.current = 0;
    article.per_pkt = 10;

    if (node_idx == 2) {
        sleep(20);
    }

    run_vsync((void*) &article);

    return 0;
}
