#include "ti-lib.h"

#define LED_MAIN_IOID IOID_20
static int inited = 0;

void
led_init(void)
{
  if(inited) {
    return;
  }
  inited = 1;

  ti_lib_rom_ioc_pin_type_gpio_output(LED_MAIN_IOID);
}

void
led_set(void)
{

    ti_lib_gpio_write_dio(LED_MAIN_IOID, 1);
}

void
led_clear(void)
{

    ti_lib_gpio_clear_dio(LED_MAIN_IOID);
}

void
led_toggle(void)
{

    ti_lib_gpio_toggle_dio(LED_MAIN_IOID);
}
