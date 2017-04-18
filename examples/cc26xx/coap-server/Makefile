DEFINES+=PROJECT_CONF_H=\"project-conf.h\"
CONTIKI_PROJECT = coap-server

all: $(CONTIKI_PROJECT)

CONTIKI_WITH_IPV6 = 1

REST_RESOURCES_DIR = ./resources
REST_RESOURCES_FILES = $(notdir $(shell find $(REST_RESOURCES_DIR) -name '*.c' ! -name 'res-plugtest*'))
PROJECTDIRS += $(REST_RESOURCES_DIR)
PROJECT_SOURCEFILES += $(REST_RESOURCES_FILES)

PROJECTDIRS += scimage
PROJECT_SOURCEFILES += scif_framework.c scif_osal_none.c scif.c

APPS += er-coap
APPS += rest-engine

CONTIKI = ../../..
include $(CONTIKI)/Makefile.include
