# Shared build configuration for gesture module tests.
# Include from each test's test.mk:
#   include tests/gesture_test_common/gestures.mk
#   INTROSPECTION_KEYMAP_C = test_gestures.c

GESTURES_DIR := modules/johnwilmes/gestures
GESTURES_TEST_COMMON := tests/gesture_test_common

SRC += $(GESTURES_DIR)/gestures.c
SRC += $(GESTURES_DIR)/coordinator.c
SRC += $(GESTURES_DIR)/layer.c
SRC += $(GESTURES_DIR)/combo.c
SRC += $(GESTURES_DIR)/tapdance.c
SRC += $(GESTURES_DIR)/precog.c
SRC += $(GESTURES_TEST_COMMON)/test_hooks.c

VPATH += $(GESTURES_DIR)
VPATH += $(GESTURES_TEST_COMMON)

REPEAT_KEY_ENABLE = yes
