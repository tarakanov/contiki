#include "contiki.h"
#include "ti-lib.h"
#include "sys/stimer.h"
#include "dev/leds.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "scimage/scif.h"
#include "net/netstack.h"

//sc files
#include  "aux-ctrl.h"
#define BV(x)   (1<< (x))


//coap files
#include "rest-engine.h"
#include "er-coap.h"

//led driver
#include "led.h"

#include "net/ipv6/sicslowpan.h"

extern resource_t res_leds, res_toggle, res_hello, res_event, res_event_sc, res_radio;

static aux_consumer_module_t sc_test_aux = {NULL, AUX_WUC_SMPH_CLOCK};


#define MAC_CAN_BE_TURNED_OFF  0
#define MAC_MUST_STAY_ON       1
static struct stimer st_min_mac_on_duration;
static struct stimer st_duration;
static struct stimer st_interval;
static struct etimer et;
static uint8_t state;

#define PERIODIC_INTERVAL         CLOCK_SECOND
#define INTERVAL    90 * 60 // Sleep time sec
#define DURATION    10 // Normal time sec
#define KEEP_MAC_ON_MIN_PERIOD 5 /* secs */

#define STATE_NORMAL           0
#define STATE_NOTIFY_OBSERVERS 1
#define STATE_VERY_SLEEPY      2

/*---------------------------------------------------------------------------*/
/*
 * If our preferred parent is not NBR_REACHABLE in the ND cache, NUD will send
 * a unicast NS and wait for NA. If NA fails then the neighbour will be removed
 * from the ND cache and the default route will be deleted. To prevent this,
 * keep the MAC on until the parent becomes NBR_REACHABLE. We also keep the MAC
 * on if we are about to do RPL probing.
 *
 * In all cases, the radio will be locked on for KEEP_MAC_ON_MIN_PERIOD secs
 */
static uint8_t
keep_mac_on(void)
{
  uip_ds6_nbr_t *nbr;
  uint8_t rv = MAC_CAN_BE_TURNED_OFF;

  if(!stimer_expired(&st_min_mac_on_duration)) {
    return MAC_MUST_STAY_ON;
    printf("MAC_MUST_STAY_ON\n");
  }

#if RPL_WITH_PROBING
  /* Determine if we are about to send a RPL probe */
  if(CLOCK_LT(etimer_expiration_time(
                &rpl_get_default_instance()->probing_timer.etimer),
              (clock_time() + PERIODIC_INTERVAL))) {
    rv = MAC_MUST_STAY_ON;
  }
#endif

  /* It's OK to pass a NULL pointer, the callee checks and returns NULL */
  nbr = uip_ds6_nbr_lookup(uip_ds6_defrt_choose());

  if(nbr == NULL) {
    /* We don't have a default route, or it's not reachable (NUD likely). */
    rv = MAC_MUST_STAY_ON;
    printf("MAC_MUST_STAY_ON\n");
  } else {
    if(nbr->state != NBR_REACHABLE) {
      rv = MAC_MUST_STAY_ON;
      printf("MAC_MUST_STAY_ON\n");
    }
  }

  if(rv == MAC_MUST_STAY_ON && stimer_expired(&st_min_mac_on_duration)) {
    stimer_set(&st_min_mac_on_duration, KEEP_MAC_ON_MIN_PERIOD);
    printf("Set keep mac on period timer\n");
  }

  return rv;
}
/*---------------------------------------------------------------------------*/
static void
switch_to_normal(void)
{
  state = STATE_NOTIFY_OBSERVERS;

  /*
   * Stay in normal mode for 'duration' secs.
   * Transition back to normal in 'interval' secs, _including_ 'duration'
   */
  stimer_set(&st_duration, DURATION);
  stimer_set(&st_interval, INTERVAL);
}
/*---------------------------------------------------------------------------*/
static void
switch_to_very_sleepy(void)
{
  state = STATE_VERY_SLEEPY;
}

PROCESS(coap_server_process, "coap server process");
AUTOSTART_PROCESSES(&coap_server_process);

/*****************************************************************************/

#define LOOP_INTERVAL       (CLOCK_SECOND * 1)

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(coap_server_process, ev, data)
{
  uint8_t mac_keep_on;

  PROCESS_BEGIN();

  printf("Long sleep test\n");

  etimer_set(&et, LOOP_INTERVAL);
  stimer_set(&st_duration, DURATION);
  stimer_set(&st_interval, INTERVAL);

  led_init();
  state = STATE_NORMAL;

  aux_ctrl_register_consumer(&sc_test_aux);
  scifInit(&scifDriverSetup);
  scifExecuteTasksOnceNbl(BV(SCIF_NEW_TASK_TASK_ID));
  leds_off(LEDS_GREEN);
  rest_init_engine();
  rest_activate_resource(&res_toggle, "actuators/toggle");
  rest_activate_resource(&res_leds, "actuators/leds");
  rest_activate_resource(&res_event, "test/event");
  rest_activate_resource(&res_radio, "test/rssi");
  rest_activate_resource(&res_hello, "test/hello");
  rest_activate_resource(&res_event_sc, "test/sc");

  switch_to_normal();

  while(1) {

    PROCESS_YIELD();
    printf("[%lu] periodic wakeup\n", clock_seconds());

    if (ev == PROCESS_EVENT_TIMER && (data == &et || data == &st_duration || data == &st_interval)) {
        printf("ET periodic\n");

        mac_keep_on = keep_mac_on();
        printf("mac_keep_on is %d\n", mac_keep_on);

        if(mac_keep_on == MAC_MUST_STAY_ON || state != STATE_VERY_SLEEPY) {
          leds_on(LEDS_GREEN);
          NETSTACK_MAC.on();
          printf("we need to be ON because we not in very sleepy - %d\n", state);
        }

        if(state == STATE_NOTIFY_OBSERVERS) {
          printf("Notify observers\n");
          res_event.trigger();
          state = STATE_NORMAL;
        }

          if(state == STATE_NORMAL) {
            if(stimer_expired(&st_duration)) {
              printf("st_duration expired, go to very sleepy - %d\n", state);
              stimer_set(&st_duration, DURATION);
              switch_to_very_sleepy();
            }
          } else if(state == STATE_VERY_SLEEPY) {
            if(stimer_expired(&st_interval)) {
                printf("st_interval expired, go to notify - %d\n", state);
                switch_to_normal();
            }
          }


        if(mac_keep_on == MAC_CAN_BE_TURNED_OFF && state == STATE_VERY_SLEEPY) {
          leds_off(LEDS_GREEN);
          NETSTACK_MAC.off(0);
          printf("MAC off - %d\n", state);
        } else {
          leds_on(LEDS_GREEN);
          NETSTACK_MAC.on();
          printf("MAC on - %d\n", state );
        }

        etimer_set(&et, LOOP_INTERVAL);
    }

  }
  PROCESS_END();
}
