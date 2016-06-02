#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "thread.h"
#include "random.h"
#include "ndn_sync.h"


int ndn_sync_init_state(ndn_sync_t* node, uint8_t idx, size_t num_node)
{
    node->sync_pfx = ndn_sync_get_sync_prefix();
    node->idx = idx;
    node->num_node = num_node;
    node->rn = 0;
    memset(node->vv, 0, num_node);
    memset(node->ldi, 0, num_node * sizeof(vn_t));
    node->log = NULL;
    return EXIT_SUCCESS;
}


/***********************************************************************************/

static int _send_interest(ndn_app_t* handler, ndn_block_t* pfx,
                          uint32_t rn, uint8_t* vv, size_t num_node,
                          ndn_app_data_cb_t on_data)
{
    ndn_shared_block_t* pfx_rn = ndn_name_append_uint32(pfx, rn);
    if (pfx_rn == NULL) return EXIT_BADFMT;
    ndn_shared_block_t* name = ndn_name_append(&(pfx_rn->block), vv, num_node);
    ndn_shared_block_release(pfx_rn);
    if (name == NULL) return EXIT_BADFMT;
    
    // Ack is ignored
    // Timeout is ignored
    if (ndn_app_express_interest(handler, &(name->block), NULL, TIME_SEC, on_data, NULL) < 0) {
        ndn_shared_block_release(name);
        return EXIT_NOSPACE;
    }
    ndn_shared_block_release(name);
    return EXIT_SUCCESS;
}


static int _add_piggyback(const vn_t* last_vn, ndn_block_t* content)
{
    size_t pl = content->len + 10;  // 10 = 2 * max_var_number_size
    uint8_t* buf = (uint8_t*) malloc(pl);
    if (buf == NULL) {
        return EXIT_NOSPACE;
    }
    uint8_t* ptr = buf;
    size_t rl = pl;
    int l;
    
    if ((l = ndn_block_put_var_number(last_vn->rn, ptr, rl)) < 0)
        return EXIT_NOSPACE;
    ptr += l;
    rl -= l;
    
    if ((l = ndn_block_put_var_number(last_vn->sn, ptr, rl)) < 0)
        return EXIT_NOSPACE;
    ptr += l;
    rl -= l;
    
    memcpy(ptr, content->buf, content->len);
    
    free((uint8_t*)(content->buf));
    content->buf = buf;
    content->len = (pl - rl) + content->len;
    
    return EXIT_SUCCESS;
}


ndn_shared_block_t* _name_from_component(const ndn_name_component_t* pfx)
{
    char* buf = (char*) malloc(pfx->len + 2);
    if (buf == NULL) {
        return NULL;
    }
    buf[0] = '/';
    memcpy(buf + 1, pfx->buf, pfx->len);
    buf[pfx->len + 1] = '\0';
    ndn_shared_block_t* dp = ndn_name_from_uri(buf, strlen(buf));
    free(buf);
    return dp;
}


ndn_shared_block_t* ndn_sync_get_sync_prefix(void)
{
    return ndn_name_from_uri("/vsync", 6);
}


ndn_shared_block_t* ndn_sync_get_data_prefix(ndn_sync_t* node, uint8_t idx)
{
    assert(idx < node->num_node);
    return _name_from_component(&(node->pfx[idx]));
}


ndn_shared_block_t* ndn_sync_publish_data (ndn_app_t* handler, ndn_sync_t* node,
                                           ndn_metainfo_t* metainfo,
                                           ndn_block_t* content)
{
    if (handler == NULL || node == NULL || metainfo == NULL || content == NULL || content->buf == NULL)
        return NULL;

    // perform round change
    if (node->vv[node->idx] == MAX_SEQ_NUM) {
        node->rn += 1;
        memset(node->vv, 0, node->num_node);
    }
    node->vv[node->idx] += 1;
    
    // publish data
    ndn_shared_block_t* pfx = _name_from_component(&(node->pfx[node->idx]));
    if (pfx == NULL) return NULL;
    ndn_shared_block_t* pfx_rn = ndn_name_append_uint32(&(pfx->block), node->rn);
    ndn_shared_block_release(pfx);
    if (pfx_rn == NULL) return NULL;
    ndn_shared_block_t* name = ndn_name_append_uint8(&(pfx_rn->block), node->vv[node->idx]);
    ndn_shared_block_release(pfx_rn);
    if (name == NULL) return NULL;
    
    ndn_block_t copy_content;
    uint8_t* tmp = malloc(content->len);
    memcpy(tmp, content->buf, content->len);
    copy_content.buf = (const uint8_t*) tmp;
    copy_content.len = content->len;
    if (node->vv[node->idx] == FIRST_SEQ_NUM) {
        if (_add_piggyback(&(node->ldi[node->idx]), &copy_content) != EXIT_SUCCESS) {
            ndn_shared_block_release(name);
            return NULL;
        }
    }
    
    _send_interest(handler, &(node->sync_pfx->block), node->rn, node->vv, node->num_node, NULL);
    
    // update last packet
    node->ldi[node->idx].rn = node->rn;
    node->ldi[node->idx].sn = node->vv[node->idx];
    
    ndn_shared_block_t* d = ndn_data_create(&(name->block), metainfo, &copy_content,
                                            NDN_SIG_TYPE_DIGEST_SHA256, NULL,
                                            NULL, 0);
    ndn_shared_block_release(name);
    
    return d;
}



/****************************************************************/

static void _merge(uint8_t* lhs, uint8_t* rhs1, uint8_t* rhs2, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        lhs[i] = rhs1[i] > rhs2[i] ? rhs1[i] : rhs2[i];
    }
}


// Extract fields from a sync interest
static int _extract_fields(ndn_block_t* name, ndn_name_component_t* pfx,
                           uint32_t* rn, uint8_t* vv, size_t num_node)
{
    ndn_name_component_t tmp;

    if (pfx != NULL) {
        if (ndn_name_get_component_from_block(name, 0, pfx) < 0) return EXIT_BADFMT;
    }
    
    if (rn != NULL) {
        if (ndn_name_get_component_from_block(name, 1, &tmp) < 0) return EXIT_BADFMT;
        
        if (tmp.len != sizeof(uint32_t)) return EXIT_BADFMT;
        uint32_t network_rn;
        memcpy(&network_rn, tmp.buf, tmp.len);
        *rn = NTOHL(network_rn);
    }
    tmp.buf = NULL;
    
    if (vv != NULL) {
        if (ndn_name_get_component_from_block(name, 2, &tmp) < 0) return EXIT_BADFMT;
        
        if (tmp.len != (int)num_node) return EXIT_BADFMT;
        memcpy(vv, tmp.buf, tmp.len);
    }
    
    return EXIT_SUCCESS;
}


static int _check_missing_data(ndn_app_t* handler, ndn_name_component_t* pfx,
                               uint32_t rn, uint8_t old_sn, uint8_t sn,
                               ndn_app_data_cb_t on_data)
{
    // encode data prefix
    ndn_shared_block_t* dp = _name_from_component(pfx);
    // TODO: error handling
    assert(dp != NULL);

    if (old_sn == MAX_SEQ_NUM) return EXIT_SUCCESS; // avoid math overflow
    // retrieve data in (old_sn, sn]
    for (old_sn++; old_sn <= sn; old_sn++) {
        if (_send_interest(handler, &(dp->block), rn, &old_sn, 1, on_data) != EXIT_SUCCESS) {
            ndn_shared_block_release(dp);
            return EXIT_NOSPACE;
        }
    }
    ndn_shared_block_release(dp);
    
    return EXIT_SUCCESS;
}


static int _recover_round(ndn_app_t* handler, ndn_sync_t* node, uint32_t rn, ndn_app_data_cb_t on_data)
{
    size_t i;
    uint8_t sn = FIRST_SEQ_NUM;
    for (i = 0; i < node->num_node; i++) {
        if (_send_interest(handler, &(node->pfx[i]), rn + 1, &sn, 1, on_data) != EXIT_SUCCESS)
            return EXIT_NOSPACE;
    }
    return EXIT_SUCCESS;
}


int ndn_sync_process_sync_interest(ndn_app_t* handler, ndn_sync_t* node, ndn_block_t* interest, ndn_app_data_cb_t on_data)
{
    if (handler == NULL || node == NULL || interest == NULL) return EXIT_BADFMT;
    
    uint32_t rn, rn_i;
    uint8_t vv[MAX_NODE_NUM], old_vv[MAX_NODE_NUM];
    ndn_block_t i_name;
    
    size_t i;
    
    if (ndn_interest_get_name(interest, &i_name) < 0)
        return EXIT_BADFMT;
    
    if (_extract_fields(&i_name, NULL, &rn, vv, node->num_node) != EXIT_SUCCESS)
        return EXIT_BADFMT;
    
    if (rn > node->rn) {
        if (rn > node->rn + 1) {    // if the round gap is more than 1
            for (rn_i = node->rn; rn_i < rn - 1; rn_i++) {
                if (_recover_round(handler, node, rn_i, on_data) != EXIT_SUCCESS)
                    return EXIT_NOSPACE;
            }
        }
        
        node->rn = rn;
        memset(node->vv, 0, node->num_node);
    }
    
    memcpy(old_vv, node->vv, node->num_node);
    _merge(node->vv, vv, node->vv, node->num_node);
    
    for (i = 0; i < node->num_node; i++) {
        if(_check_missing_data(handler, &(node->pfx[i]), rn, old_vv[i], vv[i], on_data) != EXIT_SUCCESS)
            return EXIT_NOSPACE;
    }
    
    return EXIT_SUCCESS;
}


/******************************************************************/


static int _skip_type_len(ndn_block_t* block)
{
    uint32_t num;
    int l, len = block->len, skip_len = 0;
    const uint8_t* buf = block->buf;
    
    // read content type and ignore
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    skip_len += l;
    buf += l;
    len -= l;
    
    // read content len and ignore
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    skip_len += l;
    buf += l;
    len -= l;
    
    return skip_len;
}



// Return number of bytes consumed by the piggybacked metadata
static int _get_piggyback(ndn_block_t* content, vn_t* vn)
{
    uint32_t num;
    int l, skip_len = 0, len = content->len;
    const uint8_t* buf = content->buf;
    
    // read round number in piggyback
    l = ndn_block_get_var_number(buf, len, &(vn->rn));
    if (l < 0) return -1;
    skip_len += l;
    buf += l;
    len -= l;
    
    // read sequence number in piggyback
    l = ndn_block_get_var_number(buf, len, &num);
    if (l < 0) return -1;
    if (num > 255) return -1;
    vn->sn = (uint8_t) num;
    
    return skip_len + l;
}

static int _get_node_index_by_pfx(ndn_sync_t* node, ndn_name_component_t* pfx)
{
    size_t i;
    for (i = 0; i < node->num_node; i++)
        if (ndn_name_component_compare(pfx, &(node->pfx[i])) == 0)
            break;
    return i;
}


int ndn_sync_process_data(ndn_app_t* handler, ndn_sync_t* node, ndn_block_t* data, ndn_block_t* content)
{
    if (handler == NULL || node == NULL || data == NULL || data->buf == NULL) return EXIT_BADFMT;
    
    ndn_name_component_t pfx;
    ndn_block_t d_name, d_content;
    uint8_t sn;
    uint32_t rn;
    vn_t pg_vn;
    int i, l;
    
    if (ndn_data_get_name(data, &d_name) < 0)
        return EXIT_BADFMT;
    
    if (_extract_fields(&d_name, &pfx, &rn, &sn, 1) != EXIT_SUCCESS)
        return EXIT_BADFMT;
    
    i = _get_node_index_by_pfx(node, &pfx);
    if (i >= (int)node->num_node) return EXIT_BADFMT;    // if none of the prefixes matches the one in the name
    
    if (ndn_data_get_content(data, &d_content) < 0) return EXIT_BADFMT;
    
    l = _skip_type_len(&d_content);
    if (l < 0) return EXIT_BADFMT;
    d_content.buf += l;
    d_content.len -= l;
    
    if (sn == FIRST_SEQ_NUM) {
        if ((l = _get_piggyback(&d_content, &pg_vn)) < 0)
            return EXIT_BADFMT;
        
        // check whether there is missing data need fetching    
        if (node->log != NULL && pg_vn.rn <= node->ldi[i].rn) {
            if (_check_missing_data(handler, &pfx, pg_vn.rn, node->log->rec_vvs[pg_vn.rn % MAX_ROUND_GAP][i], pg_vn.sn, NULL) != EXIT_SUCCESS)
                return EXIT_NOSPACE;
        }
            
        d_content.buf += l;
        d_content.len -= l;
    }
    
    if (node->ldi[i].rn < rn || (node->ldi[i].rn == rn && node->ldi[i].sn < sn)) {  // when updating states, ignore long delayed packets
        node->ldi[i].rn = rn;
        node->ldi[i].sn = sn;
    }
    
    if (node->log != NULL && node->log->rec_vvs[rn % MAX_ROUND_GAP][i] < sn) {   // update local log
        node->log->rec_vvs[rn % MAX_ROUND_GAP][i] = sn;
    }
    
    content->buf = d_content.buf;
    content->len = d_content.len;
    
    return EXIT_SUCCESS;
}

/**************************************************************************/
