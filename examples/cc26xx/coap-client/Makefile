DEFINES+=PROJECT_CONF_H=\"project-conf.h\"
CONTIKI_PROJECT = minimal

all: $(CONTIKI_PROJECT)

CONTIKI_WITH_IPV6 = 1

PROJECTDIRS += scimage
PROJECT_SOURCEFILES += scif_framework.c scif_osal_none.c scif.c

#coap client
PROJECT_SOURCEFILES += coap-client.c
APPS += er-coap
APPS += rest-engine

CONTIKI = ../../..
include $(CONTIKI)/Makefile.include
