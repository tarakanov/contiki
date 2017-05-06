
/*---------------------------------------------------------------------------*/
#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

#define PLATFORM_HAS_RADIO 1
/*---------------------------------------------------------------------------*/
/* Change to match your configuration */
#define IEEE802154_CONF_PANID            0xABCD
#define RF_CORE_CONF_CHANNEL                 25
/*---------------------------------------------------------------------------*/
/* Enable the ROM bootloader */
#define ROM_BOOTLOADER_ENABLE                 1
/*---------------------------------------------------------------------------*/
/* For very sleepy operation */
#define RF_BLE_CONF_ENABLED                   0
#define UIP_DS6_CONF_PERIOD        CLOCK_SECOND
#define UIP_CONF_TCP                          0
#define RPL_CONF_LEAF_ONLY                    1

#define RPL_CONF_WITH_PROBING                 1
#define CC26XX_UART_CONF_BAUD_RATE    460800

#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC     csma_driver
#undef NETSTACK_CONF_RDC
//#define NETSTACK_CONF_RDC     contikimac_driver
#define NETSTACK_CONF_RDC     nullrdc_driver

/*---------------------------------------------------------------------------*/
#endif /* PROJECT_CONF_H_ */
/*---------------------------------------------------------------------------*/
