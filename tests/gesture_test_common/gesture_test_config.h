/* Shared config for gesture module tests.
 *
 * Include from each test's config.h after test_common.h:
 *   #include "test_common.h"
 *   #include "gesture_test_config.h"
 */

#pragma once

/* Stub for community module API version check (auto-generated in real builds) */
#define COMMUNITY_MODULES_API_VERSION_BUILDER(maj,min,pat) \
    (((((uint32_t)(maj))&0xFF) << 24) | ((((uint32_t)(min))&0xFF) << 16) | (((uint32_t)(pat))&0xFF))
#define COMMUNITY_MODULES_API_VERSION COMMUNITY_MODULES_API_VERSION_BUILDER(1,0,0)
#define ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(maj,min,pat) \
    _Static_assert(COMMUNITY_MODULES_API_VERSION_BUILDER(maj,min,pat) <= COMMUNITY_MODULES_API_VERSION, \
    "Community module requires a newer version of QMK modules API")
