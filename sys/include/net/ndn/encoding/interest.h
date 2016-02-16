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
 * @brief   NDN Interest interface.
 *
 * @author  Wentao Shang <wentaoshang@gmail.com>
 */
#ifndef NDN_INTEREST_H_
#define NDN_INTEREST_H_

#include <inttypes.h>
#include <sys/types.h>

#include "net/gnrc/pktbuf.h"
#include "net/ndn/ndn-constants.h"
#include "net/ndn/encoding/name.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Creates a packet snip that contains the encoded Interest packet.
 * 
 * @details An encoded Interest packet contains two or three packet snips.
 *          The first snip contains the Interest TLV header and Name field.
 *          The second (and optional) snip contains the Selectors field, which
 *          can be omitted. The third snip contains the Nonce field and
 *          InterestLifetime field.
 *
 *          Example:
 *
 *  @code
 * +------------+      +--> +------------+      +--> +------------+
 * | type = NDN |      |    | type = NDN |      |    | type = NDN |
 * |    next    |------+    |    next    |------+    |    next    |------> NULL
 * |    data    |---+       |    data    |---+       |    data    |---+
 * +------------+   |       +------------+   |       +------------+   |
 *                  |                        |                        |
 *       +------+ <-+             +------+ <-+             +------+ <-+
 *       | 0x05 | INTEREST        | 0x09 | SELECTORS       | 0x0A | NONCE
 *       | 0xxx | len             | 0xxx | len             | 0x04 | len
 *       | 0x07 | NAME            | ...  |                 | 0xxx |--+
 *       | 0xyy | len                                      | 0xyy |  |--> nonce
 *       | 0x08 | NAME_COMP                                | 0xzz |  |    value
 *       | ...  |                                          | 0xww |--+
 *                                                         | 0x0B | LIFETIME
 *                                                         | ...  |
 *
 *  @endcode
 *
 * @param[in]  name       Name of the Interest.
 * @param[in]  selectors  Selectors of the Interest. Can be NULL if omitted.
 * @param[in]  lifetime   Lifetime of the Interest.
 *
 * @return  The head of the packet snip for the Interest packet, if success.
 * @return  NULL, if @p name is NULL or invalid.
 * @return  NULL, if out of memory for packet buffers.
 */
gnrc_pktsnip_t* ndn_interest_create(ndn_name_t* name, void* selectors, unsigned int lifetime);

#ifdef __cplusplus
}
#endif

#endif /* NDN_INTEREST_H_ */
/** @} */
