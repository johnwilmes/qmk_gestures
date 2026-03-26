GESTURES_MODULE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

SRC += $(GESTURES_MODULE_DIR)coordinator.c
SRC += $(GESTURES_MODULE_DIR)types/combo.c
SRC += $(GESTURES_MODULE_DIR)types/tapdance.c
SRC += $(GESTURES_MODULE_DIR)types/precog.c
SRC += $(GESTURES_MODULE_DIR)layer.c
