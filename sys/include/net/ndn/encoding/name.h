/*
 * Copyright (C) 2016 Wentao Shang
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    net_ndn_encoding    NDN packet encoding
 * @ingroup     net_ndn
 * @brief       NDN TLV packet encoding and decoding.
 * @{
 *
 * @file
 * @brief   NDN name and name component interface.
 *
 * @author  Wentao Shang <wentaoshang@gmail.com>
 */
#ifndef NDN_NAME_H_
#define NDN_NAME_H_

#include <inttypes.h>
#include <sys/types.h>

#include "net/ndn/ndn-constants.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Type to represent a name component.
 * @details This structure does not own the memory pointed by @p buf.
 *          The user must make sure the memory pointed by @p buf is still valid
 *          as long as this structure is in use.
 */
typedef struct ndn_name_component {
    const uint8_t* buf;      /**< pointer to the memory buffer of the component */
    int len;                 /**< size of the buffer */
} ndn_name_component_t;


/**
 * @brief   Compares two name components based on the canonical order.
 *
 * @param[in]  lhs    Left-hand-side component.
 * @param[in]  rhs    Right-hand-side component.
 *
 * @return  0 if @p lhs == @p rhs.
 * @return  1 if @p lhs > @p rhs.
 * @return  -1 if @p lhs < @p rhs.
 * @return  -2 if @p lhs or @p rhs is NULL or invalid.
 */
int ndn_name_component_compare(ndn_name_component_t* lhs, ndn_name_component_t* rhs);

/**
 * @brief   Encodes a name component into caller-supplied buffer
 *          following the TLV wire format.
 *
 * @param[in]  comp      Name component to be encoded.
 * @param[in]  buf       Pointer to the caller-supplied memory buffer.
 * @param[in]  len       Size of the buffer.
 *
 * @return  Number of bytes written to the buffer, if success.
 * @return  -1 if the buffer is not big enough to store the encoded name.
 * @return  -1 if @p comp is invalid.
 * @return  -1 if @p comp or @p buf is NULL or @p len <= 0.
 */
int ndn_name_component_wire_encode(ndn_name_component_t* comp, uint8_t* buf, int len);


/**
 * @brief   Type to represent a name.
 * @details The owner of this structure owns the memory pointed to by @p comps,
 *          and is responsible for freeing the memory after use.
 */
typedef struct ndn_name {
    int size;                       /**< number of the components */
    ndn_name_component_t* comps;    /**< pointer to the array of components */
} ndn_name_t;


/**
 * @brief   Compares two names based on the canonical order.
 *
 * @param[in]  lhs    Left-hand-side name.
 * @param[in]  rhs    Right-hand-side name.
 *
 * @return  0 if @p lhs == @p rhs.
 * @return  1 if @p lhs > @p rhs.
 * @return  -1 if @p lhs < @p rhs.
 * @return  -2 if @p lhs or @p rhs is NULL or invalid.
 */
int ndn_name_compare(ndn_name_t* lhs, ndn_name_t* rhs);

/**
 * @brief   Gets the n-th component from the name. This function does not make a copy
 *          of the content of the name component.
 *
 * @param[in]  name      Name where the component is retrieved.
 * @param[in]  pos       Position of the component (zero-indexed). If negative, @p pos
 *                       represents the offset from the end of the name (i.e., -1 means 
 *                       last component).
 * @param[in]  comp      Caller-supplied structure for storing the retrieved component.
 *                       This structure is invalidated once @p name is released. If
 *                       @p comp.buf is not NULL, the old memory is released first.
 *
 * @return  0 if success.
 * @return  -1 if @p pos >= @p name.size or @p pos < @p -(name.size).
 * @return  -1 if @p name or @p comp is NULL.
 */
int ndn_name_get_component(ndn_name_t* name, int pos, ndn_name_component_t* comp);

/**
 * @brief   Encodes a name into caller-supplied buffer following the TLV wire format.
 *          Does nothing if the name is empty.
 *
 * @param[in]  name      Name to be encoded.
 * @param[in]  buf       Pointer to the caller-supplied memory buffer.
 * @param[in]  len       Size of the buffer.
 *
 * @return  Number of bytes written to the buffer, if success.
 * @return  -1 if the buffer is not big enough to store the encoded name.
 * @return  -1 if @p name is invalid.
 * @return  -1 if @p name or @p buf is NULL.
 */
int ndn_name_wire_encode(ndn_name_t* name, uint8_t* buf, int len);

/**
 * @brief   Releases the memory that contains the array of name components.
 *          Does not release the memory for holding the content of the components.
 *
 * @param[in]  name   Name to be released. This function does nothing
 *                    if @p name is NULL.
 *
 */
void ndn_name_release(ndn_name_t* name);



#ifdef __cplusplus
}
#endif

#endif /* NDN_NAME_H_ */
/** @} */
