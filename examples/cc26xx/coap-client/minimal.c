#include "contiki.h"
#include "ti-lib.h"
#include "sys/stimer.h"
#include "dev/leds.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "coap-client.h"

//sc files
#include  "aux-ctrl.h"
#include "scimage/scif.h"
#define BV(x)   (1<< (x))


static aux_consumer_module_t sc_test_aux = {NULL, AUX_WUC_SMPH_CLOCK};

PROCESS(sc_test_process, "sc_test process");
AUTOSTART_PROCESSES(&sc_test_process);
static struct stimer st_duration;

/*****************************************************************************/

#define LOOP_INTERVAL       (CLOCK_SECOND * 6)
static struct etimer et;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sc_test_process, ev, data)
{

  PROCESS_BEGIN();


  printf("SC test\n");
  etimer_set(&et, LOOP_INTERVAL);

  aux_ctrl_register_consumer(&sc_test_aux);
  scifInit(&scifDriverSetup);
  scifExecuteTasksOnceNbl(BV(SCIF_NEW_TASK_TASK_ID));


  process_start(&coap_client_process, NULL); //launch coap client process


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

          if(stimer_expired(&st_duration)) {
              printf("Clearing error and counter\n");
              stimer_set(&st_duration,  18);
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
