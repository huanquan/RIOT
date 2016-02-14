/*
 * Copyright (C) 2016 Wentao Shang <wentaoshang@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @{
 *
 * @file
 */
#include <inttypes.h>
#include <stdlib.h>

#include "embUnit.h"

#include "net/ndn/ndn-constants.h"
#include "net/ndn/encoding/block.h"

#include "unittests-constants.h"
#include "tests-ndn_encoding.h"

static void test_ndn_block_get_type__buf_NULL__len_not_0(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ndn_block_get_type(NULL, 16));
}

static void test_ndn_block_get_type__buf_not_NULL__len_invalid(void)
{
    uint8_t buf[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(-1, ndn_block_get_type(buf, 0));
    TEST_ASSERT_EQUAL_INT(-1, ndn_block_get_type(buf, -1));
}

static void test_ndn_block_get_type__type_1(void)
{
    uint8_t buf[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(1, ndn_block_get_type(buf, 4));
}

static void test_ndn_block_get_type__type_255(void)
{
    uint8_t buf[4] = {255, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(-1, ndn_block_get_type(buf, 4));
}


static void test_ndn_block_get_length__buf_NULL__len_not_0(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ndn_block_get_length(NULL, 16));
}

static void test_ndn_block_get_length__buf_not_NULL__len_invalid(void)
{
    uint8_t buf[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(-1, ndn_block_get_length(buf, 0));
    TEST_ASSERT_EQUAL_INT(-1, ndn_block_get_length(buf, -1));
}

static void test_ndn_block_get_length__type_1__length_2(void)
{
    uint8_t buf[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(2, ndn_block_get_length(buf, 4));
}

static void test_ndn_block_get_length__type_1__length_255(void)
{
    uint8_t buf[4] = {1, 255, 3, 4};
    TEST_ASSERT_EQUAL_INT(-1, ndn_block_get_length(buf, 4));
}

static void test_ndn_block_get_length__type_255__length_2(void)
{
    uint8_t buf[4] = {255, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(-1, ndn_block_get_length(buf, 4));
}


static void test_ndn_block_get_value__buf_NULL__len_not_0(void)
{
    TEST_ASSERT_NULL(ndn_block_get_value(NULL, 16));
}

static void test_ndn_block_get_value__buf_not_NULL__len_invalid(void)
{
    uint8_t buf[4] = {1, 2, 3, 4};
    TEST_ASSERT_NULL(ndn_block_get_value(buf, 0));
    TEST_ASSERT_NULL(ndn_block_get_value(buf, -1));
}

static void test_ndn_block_get_value__type_1__length_2(void)
{
    uint8_t buf[4] = {1, 2, 3, 4};
    TEST_ASSERT((buf + 2) == ndn_block_get_value(buf, 4));
}

static void test_ndn_block_get_value__type_1__length_255(void)
{
    uint8_t buf[4] = {1, 255, 3, 4};
    TEST_ASSERT_NULL(ndn_block_get_value(buf, 4));
}

static void test_ndn_block_get_value__type_255__length_2(void)
{
    uint8_t buf[4] = {255, 2, 3, 4};
    TEST_ASSERT_NULL(ndn_block_get_value(buf, 4));
}


static void test_ndn_block_integer_length__num_1(void)
{
    TEST_ASSERT_EQUAL_INT(1, ndn_block_integer_length(1));
}

static void test_ndn_block_integer_length__num_256(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ndn_block_integer_length(0x100));
}


static void test_ndn_block_total_length__type_1__length_2(void)
{
    TEST_ASSERT_EQUAL_INT(4, ndn_block_total_length(1, 2));
}

static void test_ndn_block_total_length__type_256__length_2(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ndn_block_total_length(256, 2));
}


Test *tests_ndn_encoding_block_tests(void)
{
    EMB_UNIT_TESTFIXTURES(fixtures) {
	new_TestFixture(test_ndn_block_get_type__buf_NULL__len_not_0),
	new_TestFixture(test_ndn_block_get_type__buf_not_NULL__len_invalid),
        new_TestFixture(test_ndn_block_get_type__type_1),
        new_TestFixture(test_ndn_block_get_type__type_255),
        new_TestFixture(test_ndn_block_get_length__buf_NULL__len_not_0),
        new_TestFixture(test_ndn_block_get_length__buf_not_NULL__len_invalid),
        new_TestFixture(test_ndn_block_get_length__type_1__length_2),
        new_TestFixture(test_ndn_block_get_length__type_1__length_255),
        new_TestFixture(test_ndn_block_get_length__type_255__length_2),
        new_TestFixture(test_ndn_block_get_value__buf_NULL__len_not_0),
        new_TestFixture(test_ndn_block_get_value__buf_not_NULL__len_invalid),
        new_TestFixture(test_ndn_block_get_value__type_1__length_2),
        new_TestFixture(test_ndn_block_get_value__type_1__length_255),
        new_TestFixture(test_ndn_block_get_value__type_255__length_2),
        new_TestFixture(test_ndn_block_integer_length__num_1),
        new_TestFixture(test_ndn_block_integer_length__num_256),
        new_TestFixture(test_ndn_block_total_length__type_1__length_2),
        new_TestFixture(test_ndn_block_total_length__type_256__length_2),
    };

    EMB_UNIT_TESTCALLER(ndn_encoding_block_tests, NULL, NULL, fixtures);

    return (Test *)&ndn_encoding_block_tests;
}

void tests_ndn_encoding(void)
{
    TESTS_RUN(tests_ndn_encoding_block_tests());
}
/** @} */
