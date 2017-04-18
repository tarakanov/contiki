#include "contiki.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

//coap-client files
#include "er-coap-engine.h"
#include "coap-client.h"

uip_ipaddr_t server_ipaddr;
//change to coap server ip address
#define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0xa, 0xb, 0xc, 0xd, 0x0212, 0x4b00, 0x0c4a, 0x5b81)
#define LOCAL_PORT      UIP_HTONS(COAP_DEFAULT_PORT + 1)
#define REMOTE_PORT     UIP_HTONS(COAP_DEFAULT_PORT)
#define NUMBER_OF_URLS 1
char *service_urls[NUMBER_OF_URLS] = { "/actuators/toggle" };

/* This function is will be passed to COAP_BLOCKING_REQUEST() to handle responses. */
void
client_chunk_handler(void *response)
{
  const uint8_t *chunk;

  int len = coap_get_payload(response, &chunk);

  printf("|%.*s", len, (char *)chunk);
}

PROCESS(coap_client_process, "coap client process");

/*****************************************************************************/

#define LOOP_INTERVAL       (CLOCK_SECOND * 6)
static struct etimer et;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(coap_client_process, ev, data)
{

  PROCESS_BEGIN();

  printf("coap client test\n");
  etimer_set(&et, LOOP_INTERVAL);
  static coap_packet_t request[1];      /* This way the packet can be treated as pointer as usual. */
  SERVER_NODE(&server_ipaddr);
  coap_init_engine();

  while(1) {

    PROCESS_YIELD();

    if (ev == PROCESS_EVENT_TIMER && (data == &et)) {

          printf("************\n");
          printf("--Toggle timer--\n");

          /* prepare request, TID is set by COAP_BLOCKING_REQUEST() */
          coap_init_message(request, COAP_TYPE_CON, COAP_POST, 0);
          coap_set_header_uri_path(request, service_urls[0]);

          const char msg[] = "Toggle!";

          coap_set_payload(request, (uint8_t *)msg, sizeof(msg) - 1);

          //PRINT6ADDR(&server_ipaddr);
          printf(" : %u\n", UIP_HTONS(REMOTE_PORT));

          COAP_BLOCKING_REQUEST(&server_ipaddr, REMOTE_PORT, request,
                                client_chunk_handler);

          printf("\n--Done--\n");

          etimer_set(&et, LOOP_INTERVAL);
    }

  }
  PROCESS_END();
}
