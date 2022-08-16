#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#
PROJECT_NAME := pw240
PROJECT_VER  := 1.0.0
PROD_PATH ?= $(abspath $(shell pwd))

IDF_PATH := $(abspath $(PROD_PATH)/../esp-idf)

EXTRA_COMPONENT_DIRS += $(PROD_PATH)/core_components

include $(IDF_PATH)/make/project.mk
