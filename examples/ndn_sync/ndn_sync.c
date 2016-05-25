#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "thread.h"
#include "random.h"
#include "ndn_sync.h"

extern ndn_name_component_t sync_pfx;

static int _send_interest(ndn_app_t* handler, ndn_name_component_t* pfx,
                          uint32_t rn, uint8_t* vv, size_t num_node)
{
    ndn_shared_block_t* pfx_rn = ndn_name_append_uint32(pfx, rn);
    ndn_shared_block_t* name = ndn_name_append(&(pfx_rn->block), vv, num_node);
    ndn_shared_block_release(pfx_rn);
    
    // Ack is ignored
    if (ndn_app_express_interest(handler, &(name->block), NULL, TIME_SEC, NULL, NULL) < 0)
        return EXIT_NOSPACE;
    return EXIT_SUCCESS;
}


static int _add_piggyback(const vn_t* last_vn, ndn_block_t* content)
{
    size_t pl = content->len + 10;  // 10 = 2 * max_var_number_size
    uint8_t* buf = (uint8_t*) malloc(pl);
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


ndn_shared_block_t* ndn_sync_publish_data (ndn_app_t* handler, ndn_sync_t* node,
                                           ndn_metainfo_t* metainfo,
                                           ndn_block_t* content)
{
    if (handler == NULL || node == NULL || metainfo == NULL || content == NULL)
        return NULL;

    // perform round change
    if (node->vv[node->idx] == MAX_SEQ_NUM) {
        node->rn += 1;
        memset(node->vv, 0, node->num_node);
    }
    
    // publish data
    node->vv[node->idx] += 1;
    
    ndn_shared_block_t* pfx_rn = ndn_name_append_uint32(&(node->pfx[node->idx]), node->rn);
    if (pfx_rn == NULL) return NULL;
    ndn_shared_block_t* name = ndn_name_append(&(pfx_rn->block), node->vv, node->num_node);
    ndn_shared_block_release(pfx_rn);
    if (name == NULL) return NULL;
    
    
    if (node->vv[node->idx] == FIRST_SEQ_NUM)  {
        if (_add_piggyback(node->ldi + node->idx, content) != EXIT_SUCCESS) {
            ndn_shared_block_release(name);
            return NULL;
        }
    }
    
    _send_interest(handler, &(sync_pfx), node->rn, node->vv, node->num_node);
    
    // update last packet
    node->ldi[node->idx].rn = node->rn;
    node->ldi[node->idx].sn = node->vv[node->idx];
    
    ndn_shared_block_t* d = ndn_data_create(&(name->block), metainfo, content,
                                            NDN_SIG_TYPE_DIGEST_SHA256,NULL,0);
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
        memcpy(rn, tmp.buf, tmp.len);
    }
    
    free((uint8_t*) tmp.buf);
    tmp.buf = NULL;
    
    if (vv != NULL) {
        if (ndn_name_get_component_from_block(name, 2, &tmp) < 0) return EXIT_BADFMT;
        
        if (tmp.len != (int)num_node) return EXIT_BADFMT;
        memcpy(vv, tmp.buf, tmp.len);
    }
    
    return EXIT_SUCCESS;
}


static int _check_missing_data(ndn_app_t* handler, ndn_name_component_t* pfx,
                               uint32_t rn, uint8_t old_sn, uint8_t sn)
{
    if (old_sn == MAX_SEQ_NUM) return EXIT_SUCCESS;
    // retrieve data in (old_sn, sn]
    for (old_sn++; old_sn <= sn; old_sn++) {
        if (_send_interest(handler, pfx, rn, &old_sn, 1) != EXIT_SUCCESS)
            return EXIT_NOSPACE;
    }
    
    return EXIT_SUCCESS;
}


int ndn_sync_process_sync_interest(ndn_app_t* handler, ndn_sync_t* node, ndn_block_t* interest)
{
    if (handler == NULL || node == NULL || interest == NULL) return EXIT_BADFMT;
    
    uint32_t rn;
    uint8_t vv[MAX_NODE_NUM], old_vv[MAX_NODE_NUM];
    ndn_block_t i_name;
    
    size_t i;
    
    if (ndn_interest_get_name(interest, &i_name) != EXIT_SUCCESS)
        return EXIT_BADFMT;
    
    if (_extract_fields(&i_name, NULL, &rn, vv, node->num_node) != EXIT_SUCCESS)
        return EXIT_BADFMT;
    
    if (rn > node->rn) {
        node->rn = rn;
        memset(node->vv, 0, node->num_node);
    }
    
    memcpy(old_vv, node->vv, node->num_node);
    _merge(node->vv, vv, node->vv, node->num_node);
    
    for (i = 0; i < node->num_node; i++) {
        if(_check_missing_data(handler, &(node->pfx[i]), rn, old_vv[i], vv[i]) != EXIT_SUCCESS)
            return EXIT_NOSPACE;
    }
    
    return EXIT_SUCCESS;
}


/******************************************************************/


static int _get_piggyback(ndn_block_t* content, vn_t* vn)
{
    uint32_t num;
    int l;
    l = ndn_block_get_var_number(content->buf, content->len, &(vn->rn));
    if (l < 0) return EXIT_BADFMT;
    
    l = ndn_block_get_var_number(content->buf + l, content->len - l, &num);
    if (l < 0) return EXIT_BADFMT;
    if (num > 255) return EXIT_BADFMT;
    vn->sn = (uint8_t) num;
    
    return EXIT_SUCCESS;
}

static int _get_node_index_by_pfx(ndn_sync_t* node, ndn_name_component_t* pfx)
{
    size_t i;
    for (i = 0; i < node->num_node; i++)
        if (ndn_name_component_compare(pfx, &(node->pfx[i])) == 0)
            break;
    return i;
}


int ndn_sync_process_data(ndn_app_t* handler, ndn_sync_t* node, ndn_block_t* data)
{
    ndn_name_component_t pfx;
    ndn_block_t d_name, d_content;
    uint8_t sn;
    uint32_t rn;
    vn_t pg_vn;
    int i;
    
    if (ndn_data_get_name(data, &d_name) < 0)
        return EXIT_BADFMT;
    
    if (_extract_fields(&d_name, &pfx, &rn, &sn, 1) != EXIT_SUCCESS)
        return EXIT_BADFMT;
    
    i = _get_node_index_by_pfx(node, &pfx);
    
    if (ndn_data_get_content(data, &d_content) < 0) return EXIT_BADFMT;
    
    if (sn == FIRST_SEQ_NUM) {
        if (_get_piggyback(&d_content, &pg_vn) != EXIT_SUCCESS)
            return EXIT_BADFMT;
            
        if (node->ldi[i].rn != pg_vn.rn)    // either out-of-date data (>) or missing too much data (<)
            return EXIT_NOSPACE;
            
        if (_check_missing_data(handler, &pfx, pg_vn.rn, node->ldi[i].sn, pg_vn.sn) != EXIT_SUCCESS)
            return EXIT_NOSPACE;
    }
    
    node->ldi[i].rn = rn;
    node->ldi[i].sn = sn;
    
    return EXIT_SUCCESS;
}

#ifdef __cplusplus
}
#endif

