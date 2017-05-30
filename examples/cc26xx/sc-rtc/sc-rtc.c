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


extern resource_t res_leds, res_toggle, res_hello;



static aux_consumer_module_t sc_test_aux = {NULL, AUX_WUC_SMPH_CLOCK};

PROCESS(scrtc_test_process, "sc rtc_test process");
AUTOSTART_PROCESSES(&scrtc_test_process);
static struct stimer st_duration;

/*****************************************************************************/

#define LOOP_INTERVAL       (CLOCK_SECOND * 1)
static struct etimer et;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(scrtc_test_process, ev, data)
{

  PROCESS_BEGIN();

  printf("SC test\n");
  etimer_set(&et, LOOP_INTERVAL);
  stimer_set(&st_duration,  3600);

  aux_ctrl_register_consumer(&sc_test_aux);

  scifInit(&scifDriverSetup);
  uint32_t rtc_Hz = 10;
  scifStartRtcTicksNow(0x00010000 / rtc_Hz);

  scifStartTasksNbl(BV(SCIF_NEW_TASK_TASK_ID));
  leds_off(LEDS_GREEN);
  //NETSTACK_MAC.off(0);
  rest_init_engine();
  rest_activate_resource(&res_toggle, "actuators/toggle");
  rest_activate_resource(&res_leds, "actuators/leds");
  //rest_activate_resource(&res_parent_rssi, "rssi");
  rest_activate_resource(&res_hello, "hello");
  while(1) {

    PROCESS_YIELD();

    if (ev == PROCESS_EVENT_TIMER && (data == &et || data == &st_duration)) {

          printf("************\n");
          printf("Info from SC:\n");
          printf("Loop counter:  %d, %lx, %d, %d, %lx, %lx\n", scifTaskData.newTask.state.loopcount,
                 ti_lib_aon_rtc_compare_value_get(AON_RTC_CH2), ti_lib_aon_rtc_active(),
                 ti_lib_aon_rtc_channel_active(AON_RTC_CH2), ti_lib_aon_rtc_mode_ch2_get(),
                 ti_lib_aon_rtc_current_compare_value_get());
          if(stimer_expired(&st_duration)) {
              printf("Periodic event\n");
              stimer_set(&st_duration, 10);
          }

        etimer_set(&et, LOOP_INTERVAL);
    }

  }
  PROCESS_END();
}
