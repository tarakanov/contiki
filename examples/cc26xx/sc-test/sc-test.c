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

PROCESS(sc_test_process, "sc_test process");
AUTOSTART_PROCESSES(&sc_test_process);
static struct stimer st_duration;

/*****************************************************************************/

#define LOOP_INTERVAL       (CLOCK_SECOND * 1)
static struct etimer et;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sc_test_process, ev, data)
{

  PROCESS_BEGIN();

  printf("SC test\n");
  etimer_set(&et, LOOP_INTERVAL);
  stimer_set(&st_duration,  3600);

  aux_ctrl_register_consumer(&sc_test_aux);

  scifInit(&scifDriverSetup);
  scifTaskData.newTask.cfg.pollTime = 300;
  scifExecuteTasksOnceNbl(BV(SCIF_NEW_TASK_TASK_ID));
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
          printf("Levels: nh - %d, nl - %d, nt - %d, rt - %d)\n", scifTaskData.newTask.cfg.namurHigh,
                 scifTaskData.newTask.cfg.namurLow, scifTaskData.newTask.cfg.namurLevel, scifTaskData.newTask.cfg.reedLevel);
          printf("Counters Ch1: nc - %d, nr - %d\n", scifTaskData.newTask.output.counterNamurCh1, scifTaskData.newTask.output.counterReedCh1);
          printf("Counters Ch2: nc - %d, nr - %d\n", scifTaskData.newTask.output.counterNamurCh2, scifTaskData.newTask.output.counterReedCh2);
          printf("State Ch1: error - %d, type - %d\n", scifTaskData.newTask.output.channelErrorCh1, scifTaskData.newTask.output.typeCh1);
          printf("State Ch2: error - %d, type - %d\n", scifTaskData.newTask.output.channelErrorCh2, scifTaskData.newTask.output.typeCh2);
          printf("Clear flags Ch1: ce - %d, cc - %d\n", scifTaskData.newTask.state.clearErrorCh1, scifTaskData.newTask.state.clearCounterCh1);
          printf("Clear flags Ch2: ce - %d, cc - %d\n", scifTaskData.newTask.state.clearErrorCh2, scifTaskData.newTask.state.clearCounterCh2);
          printf("Loop counter - %lu, pooltime  - %d\n", scifTaskData.newTask.state.loopcounter, scifTaskData.newTask.cfg.pollTime);
          if(stimer_expired(&st_duration)) {
              printf("Clearing error and counter\n");
              stimer_set(&st_duration, 3600);
              scifTaskData.newTask.state.clearErrorCh1 = 1;
              scifTaskData.newTask.state.clearCounterCh1 = 1;
              scifTaskData.newTask.state.clearErrorCh2 = 1;
              scifTaskData.newTask.state.clearCounterCh2 = 1;
          }

        etimer_set(&et, LOOP_INTERVAL);
    }

  }
  PROCESS_END();
}
