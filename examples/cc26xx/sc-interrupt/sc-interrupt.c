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

#include "lpm.h"

extern resource_t res_leds, res_toggle, res_hello;


/*****************************************************************************/
static void shutdown_handler(uint8_t mode)
{
    //    printf("Woke up\n");
        leds_off(LEDS_GREEN);
}
/*****************************************************************************/
static void wakeup_handler(void)
{
    //    printf("Shutdown with %d\n", mode);
        leds_on(LEDS_GREEN);
}
LPM_MODULE(lpm_module, NULL, shutdown_handler, wakeup_handler, LPM_DOMAIN_PERIPH);

static aux_consumer_module_t sc_test_aux = {NULL, AUX_WUC_SMPH_CLOCK};

PROCESS(scinterrupt_test_process, "sc interrupt_test process");
AUTOSTART_PROCESSES(&scinterrupt_test_process);
static struct stimer st_duration;


process_event_t sc_event;


void scTaskAlertCallback(void)
{
    scifClearAlertIntSource();
    printf("ALERT ISR\n");
    process_post(&scinterrupt_test_process, sc_event, NULL);
    scifAckAlertEvents();
} // scTaskAlertCallback



/*****************************************************************************/

#define LOOP_INTERVAL       (CLOCK_SECOND * 10)
static struct etimer et;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(scinterrupt_test_process, ev, data)
{
  PROCESS_BEGIN();

  printf("SC test\n");
  etimer_set(&et, LOOP_INTERVAL);
  stimer_set(&st_duration,  3600);

  sc_event = process_alloc_event();
  aux_ctrl_register_consumer(&sc_test_aux);
  lpm_register_module(&lpm_module);

  scifOsalRegisterTaskAlertCallback(scTaskAlertCallback);
  scifInit(&scifDriverSetup);
  uint32_t rtc_Hz = 10;
  scifStartRtcTicksNow(0x00010000 / rtc_Hz);

  scifStartTasksNbl(BV(SCIF_NEW_TASK_TASK_ID));
  NETSTACK_MAC.off(0);
  rest_init_engine();
  rest_activate_resource(&res_toggle, "actuators/toggle");
  rest_activate_resource(&res_leds, "actuators/leds");
  //rest_activate_resource(&res_parent_rssi, "rssi");
  rest_activate_resource(&res_hello, "hello");
  while(1) {

    PROCESS_YIELD();

    if (ev == PROCESS_EVENT_TIMER && (data == &et || data == &st_duration))  {

          printf("************\n");
          printf("Info from SC:\n");
          printf("Loop counter:  %d, %lx, %d, %d, %lx, %lx\n", scifTaskData.newTask.state.loopcount,
                 ti_lib_aon_rtc_compare_value_get(AON_RTC_CH2), ti_lib_aon_rtc_active(),
                 ti_lib_aon_rtc_channel_active(AON_RTC_CH2), ti_lib_aon_rtc_mode_ch2_get(),
                 ti_lib_aon_rtc_current_compare_value_get());
          if(stimer_expired(&st_duration)) {
              printf("Periodic event\n");
              stimer_set(&st_duration, 60);
          }

        etimer_set(&et, LOOP_INTERVAL);
    } else if (ev == sc_event) {
        printf("Hello from SC\n");
    }

  }
  PROCESS_END();
}
