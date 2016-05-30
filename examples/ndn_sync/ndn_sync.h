#ifndef _NDN_SYNC_H_
#define _NDN_SYNC_H_

#include "net/ndn/app.h"
#include "net/ndn/ndn.h"
#include "net/ndn/encoding/name.h"
#include "net/ndn/encoding/interest.h"
#include "net/ndn/encoding/data.h"
#include "net/ndn/msg_type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NODE_NUM 16
#define FIRST_SEQ_NUM 1
#define MAX_SEQ_NUM 15
#define MAX_NAME_LEN 255
#define TIME_SEC 1000

#define EXIT_SUCCESS 0
#define EXIT_BADFMT 1
#define EXIT_NOSPACE 2

typedef struct {
    uint32_t rn;    // round number
    uint8_t sn;    // version number
} vn_t;

typedef struct {
    ndn_shared_block_t* sync_pfx;
    uint8_t idx;    // index of the node
    uint32_t rn;    // round number
    size_t num_node;    // number of nodes in the group
    uint8_t vv[MAX_NODE_NUM];    // version vector
    ndn_name_component_t pfx[MAX_NODE_NUM]; // list of data prefixes
    vn_t ldi[MAX_NODE_NUM]; // last received data
} ndn_sync_t;


/**
 * @brief   Initiate states of sync protocol, setting vv, rn, ldi to 0
 *
 * @details This function is reentrant and can be called from multiple threads.
 *
 * @param[in]  node       State machine of sync protocol.
 * @param[in]  idx        Index of current node in its group.
 * @param[in]  num_node   Number of registered nodes in the group.
 *
 * @return  0, always succeed.
 */
int ndn_sync_init_state(ndn_sync_t* node, uint8_t idx, size_t num_node);


ndn_shared_block_t* ndn_sync_get_sync_prefix(void);


ndn_shared_block_t* ndn_sync_get_data_prefix(ndn_sync_t* node, uint8_t idx);

/**
 * @brief   construct data packet for publishing and broadcast sync interests
 *          to notify others the change
 *
 * @details This function is reentrant and can be called from multiple threads.
 *
 * @param[in]  handler    Handler of the app that calls this function.
 * @param[in]  node       State machine of sync protocol.
 * @param[in]  metainfo   Metainfo of the new data packet.
 * @param[in]  content    Content of the new data packet.
 *
 * @return  @p data packet, if success.
 * @return  NULL, otherwise.
 */
ndn_shared_block_t* ndn_sync_publish_data(ndn_app_t* handler, ndn_sync_t* node,
                                          ndn_metainfo_t* metainfo,
                                          ndn_block_t* content);

/**
 * @brief   process received sync interest, update states and fetch missing data.
 *
 * @details This function is reentrant and can be called from multiple threads.
 *
 * @param[in]  handler    Handler of the app that calls this function.
 * @param[in]  node       State machine of sync protocol.
 * @param[in]  interest   Received interest.
 * @oaram[in]  on_data    Callback executed when data of missing data's interests are received. Can be NULL.
 *
 * @return  0, if success.
 * @return  1, if the interest is in a bad format.
 * @return  2, if fail to fetch missing data.
 */
int ndn_sync_process_sync_interest(ndn_app_t* handler, ndn_sync_t* node,
                                   ndn_block_t* interest, ndn_app_data_cb_t on_data);

/**
 * @brief   process received data, update states and fetch missing data storing
 *          in piggyback
 *
 * @details This function is reentrant and can be called from multiple threads.
 *
 * @param[in]  handler    Handler of the app that calls this function.
 * @param[in]  node       State machine of sync protocol.
 * @param[in]  data       Received data.
 * @param[out] content    Content of received data. Does not copy data.
 *
 * @return  0, if success.
 * @return  1, if the data is in a bad format.
 * @return  2, if fail to fetch missing data.
 */
int ndn_sync_process_data(ndn_app_t* handler, ndn_sync_t* node,
                          ndn_block_t* data, ndn_block_t* content);

#ifdef __cplusplus
}
#endif

#endif  /* _NDN_SYNC_H_ */
