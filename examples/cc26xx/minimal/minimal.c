#include "contiki.h"
#include "ti-lib.h"
#include "sys/stimer.h"
#include "dev/leds.h"
#include "net/netstack.h"


PROCESS(rpl_test_process, "rpl_test process");
AUTOSTART_PROCESSES(&rpl_test_process);
static struct stimer st_duration;

static void wakeup_handler(void);
static void shutdown_handler(uint8_t mode);
#include "lpm.h"

LPM_MODULE(lpm_module, NULL, shutdown_handler, wakeup_handler, LPM_DOMAIN_PERIPH);

static void wakeup_handler(void)
{
    printf("Woke up\n");
    leds_on(LEDS_RED);
}
static void shutdown_handler(uint8_t mode)
{
    printf("Shutdown with %d\n", mode);
    leds_off(LEDS_RED);
}
/*****************************************************************************/

#define LOOP_INTERVAL       (CLOCK_SECOND * 6)
static struct etimer et;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rpl_test_process, ev, data)
{

  PROCESS_BEGIN();
  leds_on(LEDS_GREEN);

  printf("test RPL\n");
  etimer_set(&et, LOOP_INTERVAL);
  lpm_register_module(&lpm_module);
  leds_off(LEDS_GREEN);
  NETSTACK_MAC.off(0);
  while(1) {

    PROCESS_YIELD();

    if (ev == PROCESS_EVENT_TIMER && (data == &et || data == &st_duration)) {

          printf("Loop interval\n");

          if(stimer_expired(&st_duration)) {
              printf("st_duration interval\n");
              stimer_set(&st_duration,  10);
          }

        etimer_set(&et, LOOP_INTERVAL);
    }

  }
  PROCESS_END();
}
