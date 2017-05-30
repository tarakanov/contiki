all: $(CONTIKI_PROJECT)

CONTIKI = ../..

DEFINES+=PROJECT_CONF_H=\"project-conf.h\"
CONTIKI_PROJECT = userFile


# include sensorcontroller image

PROJECT_SOURCEFILES += scif_framework.c scif_osal_none.c scif.c led.c


# automatically build RESTful resources
REST_RESOURCES_DIR = ./resources
ifndef TARGET
REST_RESOURCES_FILES = $(notdir $(shell find $(REST_RESOURCES_DIR) -name '*.c'))
else
ifeq ($(TARGET), native)
REST_RESOURCES_FILES = $(notdir $(shell find $(REST_RESOURCES_DIR) -name '*.c'))
else
REST_RESOURCES_FILES = $(notdir $(shell find $(REST_RESOURCES_DIR) -name '*.c' ! -name 'res-plugtest*'))
endif
endif

PROJECTDIRS += $(REST_RESOURCES_DIR)
PROJECT_SOURCEFILES += $(REST_RESOURCES_FILES)

# REST Engine shall use Erbium CoAP implementation
APPS += er-coap
APPS += rest-engine

include $(CONTIKI)/Makefile.include
