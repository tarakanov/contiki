#include "reed-relay.h"
#include "dev/leds.h"

/* CC2650 Specific Headers */
#include "lpm.h"
#include "ti-lib.h"
//#include "dev/watchdog.h"
#include "dev/battery-sensor.h"
#include "sensortag/button-sensor.h"
#include "batmon-sensor.h"

#include "ext-flash.h"

#include "contiki-net.h"
#include "rest-engine.h"
#include "net/ipv6/sicslowpan.h"

//sensor controller
#include  "aux-ctrl.h"
#include "scif.h"
#define BV(x)   (1<< (x))
static aux_consumer_module_t sc_aux = {NULL, AUX_WUC_SMPH_CLOCK};

//led driver
#include "led.h"

#define DREBEZG_TIME 			CLOCK_SECOND/100
#define COUNT_DEVICE  			3							// Максимум подключаемых устройств
#define TEST_PROGRAM 			false


/******************************************************************************
*                              FUNCTION PROTOTYPES                            *
******************************************************************************/
static void shutdown_handler(uint8_t mode);
static void wakeup_handler(void);
static process_event_t lpm_shutdown_event;
static process_event_t lpm_wake_event;
/*****************************************************************************/

/******************************************************************************
*                                    PASSWORDS                                *
******************************************************************************/

static short int PIN_Reboot = 1234;

/******************************************************************************
*                                    GLOBALS                                  *
******************************************************************************/
static struct etimer delay_timer;
static struct etimer sct;
static struct etimer network_on_timer, button_timer;

static int speed = 1;

//static short int leds_state = 1;

static short int startYear = 2014;

static short int countPressed = 0;

static bool isSleeping = false; //current state
static bool cantSleep = true;   //Этот параметр нужен для регулирования сна во время отправки всех данных при нажатии на сервисную кнопку. Если кнопка нажата во время сна, то БК должен проснуться отправить данные и снова заснуть. Иначе же режим сна меняться не должен.
static int globalSleepAllowed = 1;   //Глобальный параметр, (не)позволяющий сон
static bool system_started = false;
static bool first_start = true;
static bool ignore_settings = false;

unsigned long cntImp[COUNT_DEVICE];

//blink variable
static bool isIndicateGreen = false;
static bool isIndicateRed = false;
static int countBlinksRed = 5000;
static int countBlinksGreen = 5;
static int currentBlinks = 0;
static int blinkDelayRed = CLOCK_SECOND/2;
static int blinkDelayGreen = CLOCK_SECOND/20;
static struct etimer blink_timer_green, blink_timer_red;
enum blink_mode{
	transfer, findNetwork
};

/*-------Custom time object-----------*/

typedef struct Customtm_s{
	short int Year;
	short int Month;
	short int Day;
	short int Hour;
	short int Minute;
	short int Second;
} Customtm;

static unsigned long addTime = 0;
static Customtm LastTime = {2014,1,1,0,0,0};						// 

//Дней в месяце
static short int DaysInMonths[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
//Число дней с начала года до начала месяца
static short int DaysToMonths[12] = {0,31,59,90,120,151,181,212,243,273,304,334};

static bool IsLeapYear(int year){
	return year % 4 == 0;
}

static short int DaysInMonth(int month, int year){
	printf("DaysInMonth. month = %i,year = %i, DaysInMonths[month-1] = %i, IsLeapYear(year) = %i \r\n", month, year, DaysInMonths[month-1], IsLeapYear(year));
	return DaysInMonths[month-1]+(IsLeapYear(year) && month==2);
}

static short int DaysToMonth(int month, int year){
	return DaysToMonths[month-1]+(IsLeapYear(year) && month>2);
}

//Количество дней от года отчета startYear до текущего года
static int DaysToYear(int year){
	if (year == startYear) return 0;
	int y = year - startYear;
	return y*365 + (y+1)/4;
}

//Получение количества секунд указанной даты начиная со стартовой даты
static unsigned long DateToSeconds(Customtm tm)
{
	unsigned long countDays = DaysToYear(tm.Year) + DaysToMonth(tm.Month,tm.Year) + tm.Day - 1;

	return countDays*86400+tm.Hour*3600+tm.Minute*60+tm.Second;
}

static Customtm SecondsToDate(unsigned long seconds)
{
	Customtm customtm;
	unsigned long  minutes = seconds/60;
	unsigned long  hours = minutes/60;
	unsigned long days = hours/24;
	//Грубо прикинем год
	int Year = days/365 + startYear;
	//Количество дней в истекших годах
	unsigned long  days_to_year = DaysToYear(Year);
	//Уточним не лишний ли год?
	if (days<days_to_year)
	{
		Year--;
		days_to_year = DaysToYear(Year);
	}
	//Определим день года
	short int  dayOfYear = days - days_to_year + 1;
	//Грубо прикинем месяц
	short int  Month = dayOfYear/29 + 1;
 	if (Month>12){
		Month = 12;
	}
	//Количество дней в истекших месяцах
	unsigned long days_to_month = DaysToMonth(Month,Year);
	//Уточним не лишний ли месяц
	if (dayOfYear<=days_to_month)
	{
		Month--;
		days_to_month = DaysToMonth(Month,Year);
	}
	unsigned long Day = dayOfYear - days_to_month;
	customtm.Year = Year;
	customtm.Month = Month;
	customtm.Day = Day;
	unsigned long dayscommon = days_to_year + days_to_month + Day -1;
	unsigned long secondsCommon = dayscommon*24*60*60;
	unsigned long secondForTime = seconds - secondsCommon;
	customtm.Hour = secondForTime / 3600;
	unsigned long ost = secondForTime - customtm.Hour*3600;
	customtm.Minute = ost/60;
	customtm.Second = ost - customtm.Minute*60;
	return customtm;
}

char currentTimeString[19]={};

static unsigned long getCurrentTimeSec()
{
  unsigned long current_time = clock_seconds();
  return current_time + addTime;
}

static char*
getTimeString(){
  Customtm tm = SecondsToDate(getCurrentTimeSec());
  //unsigned long current_time_after = DateToSeconds(tm);
  //char *str = (char *) malloc(sizeof(char) * 19);
  sprintf(currentTimeString, "%d-%d-%d %d:%d:%d", tm.Year, tm.Month, tm.Day, tm.Hour, tm.Minute, tm.Second);
  //printf("%s\r\n", currentTimeString);
  return currentTimeString;
}

static Customtm
get_time_from_string(char str[19])
{
  printf(str);
  Customtm tm;
  char * pch;
  short int i=0;
  pch = strtok(str, " -:");
  short int in = atoi(pch);
  tm.Year = in;
  while (pch != NULL)
  {
    i++;
    pch = strtok(NULL, " -:");
    in = atoi(pch);
    switch(i)
    {
      case 1:
			tm.Month = in;
			break;
      case 2:
			tm.Day = in;
			break;
      case 3:
			tm.Hour = in;
			break;
      case 4:
			tm.Minute = in;
			break;
      case 5:
			tm.Second = in;
			break;
    }
  }
  return tm;
}

/*------------------------------------------------------------Custom time object----------------------------------------------------------------------*/

//Coap:
//Уровень сигнала
//static void rssi_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
//Ресурс скорости
static void res_get_speed(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_post_speed(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
//Ресурс серийных номеров
static void res_get_serial(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_post_serial(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
//Ресурс времени
static void res_get_time(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_put_time(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
//Ресурс общего количества импульсов
static void res_get_commonImp(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
//battery
static void res_get_battery(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_get_danger_battery(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_get_danger_shortcircuit(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_get_danger_opencircuit(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
//opening
static void res_get_danger_opening(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
//Подписка на часовой архив
static void hour_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);

//Подписка на дневной архив
static void day_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);

//Подписка на месячный архив
static void month_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);

//Доп. параметры
static void sendCommonImpPeriod_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void sendCommonImpPeriod_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void setOnlinePeriod_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void setOnlinePeriod_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void silentMode_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void silentMode_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void batteryPeriod_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void batteryPeriod_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void batteryLowBorder_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void batteryLowBorder_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void sendImpPeriod_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void sendImpPeriod_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
//Перезагрузка
static void reboot_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
//Перезагрузка
static void allowSleeping_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void allowSleeping_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);


//static void res_event_handler(void);
//static void rssi_event_handler(void);
static void speed_event_handler(void);
static void time_event_handler(void);
static void commonImp_event_handler(void);
static void battery_event_handler(void);
static void battery_event_danger_handler(void);
static void opencircuit_event_danger_handler(void);
static void shortcircuit_event_danger_handler(void);
static void opening_event_danger_handler(void);
static void hour_event_handler(void);
static void day_event_handler(void);

static void month_event_handler(void);
static void sendCommonImpPeriod_event_handler(void);
static void setOnlinePeriod_event_handler(void);
static void silentMode_event_handler(void);
static void batteryPeriod_event_handler(void);
static void batteryLowBorder_event_handler(void);
//static void reboot_event_handler(void);
static void sendImpPeriod_event_handler(void);

/*EVENT_RESOURCE(res_rssi,
         "title=\"rssi\";obs",
         rssi_get_handler,
         NULL,
         NULL,
	 NULL,
         rssi_event_handler);*/

EVENT_RESOURCE(res_time,
         "title=\"current time\";obs",
         res_get_time,
         NULL,
         res_put_time,
	 NULL,
	 time_event_handler);

EVENT_RESOURCE(res_speed,
         "title=\"current time\";obs",
         res_get_speed,
         res_post_speed,
         NULL,
	 NULL,
	 speed_event_handler);

RESOURCE(res_serial,
         "title=\"serials\";obs",
         res_get_serial,
         res_post_serial,
         NULL,
	 	 NULL);

EVENT_RESOURCE(res_commonImp,
         "title=\"Common impulse red count\";obs",
         res_get_commonImp,
         NULL,
         NULL,
	 NULL,
	 commonImp_event_handler);

EVENT_RESOURCE(res_battery,
         "title=\"battery voltage\";obs",
         res_get_battery,
         NULL,
         NULL,
	 NULL,
	 battery_event_handler);

EVENT_RESOURCE(res_danger_battery,
         "title=\"Danger battery message\";obs",
         res_get_danger_battery,
         NULL,
         NULL,
	 NULL,
	 battery_event_danger_handler);

EVENT_RESOURCE(res_shortcircuit,
         "title=\"Danger battery message\";obs",
         res_get_danger_shortcircuit,
         NULL,
         NULL,
	 NULL,
	 shortcircuit_event_danger_handler);

EVENT_RESOURCE(res_opencircuit,
         "title=\"Danger battery message\";obs",
         res_get_danger_opencircuit,
         NULL,
         NULL,
	 NULL,
	 opencircuit_event_danger_handler);

EVENT_RESOURCE(res_danger_opening,
         "title=\"Danger controlleropening\";obs",
         res_get_danger_opening,
         NULL,
         NULL,
	 NULL,
	 opening_event_danger_handler);

EVENT_RESOURCE(res_hour,
         "title=\"hours Shedule\";obs",
         hour_get_handler,
         NULL,
         NULL,
	 NULL,
         hour_event_handler);

EVENT_RESOURCE(res_day,
         "title=\"day Shedule\";obs",
         day_get_handler,
         NULL,
         NULL,
	 NULL,
         day_event_handler);

EVENT_RESOURCE(res_month,
         "title=\"month Shedule\";obs",
         month_get_handler,
         NULL,
         NULL,
	 NULL,
         month_event_handler);

EVENT_RESOURCE(res_sendCommonImpPeriod,
         "title=\"commonImp period to send data\";obs",
         sendCommonImpPeriod_get_handler,
         sendCommonImpPeriod_post_handler,
         NULL,
	 NULL,
         sendCommonImpPeriod_event_handler);

EVENT_RESOURCE(res_setOnlinePeriod,
         "title=\"set online period\";obs",
         setOnlinePeriod_get_handler,
         setOnlinePeriod_post_handler,
         NULL,
	 NULL,
         setOnlinePeriod_event_handler);

EVENT_RESOURCE(res_silentMode,
         "title=\"set silent mode\";obs",
         silentMode_get_handler,
         silentMode_post_handler,
         NULL,
	 NULL,
         silentMode_event_handler);

EVENT_RESOURCE(res_batteryPeriod,
         "title=\"battery period\";obs",
         batteryPeriod_get_handler,
         batteryPeriod_post_handler,
         NULL,
	 NULL,
         batteryPeriod_event_handler);

EVENT_RESOURCE(res_batteryLowBorder,
         "title=\"battery low border\";obs",
         batteryLowBorder_get_handler,
         batteryLowBorder_post_handler,
         NULL,
	 NULL,
         batteryLowBorder_event_handler);

EVENT_RESOURCE(res_reboot,
         "title=\"reboot device\";obs",
         NULL,
         reboot_post_handler,
         NULL,
	 NULL,
         NULL/*reboot_event_handler*/);

EVENT_RESOURCE(res_allowSleeping,
         "title=\"reboot device\";obs",
         allowSleeping_get_handler,
         allowSleeping_post_handler,
         NULL,
	 	 NULL,
         NULL);

EVENT_RESOURCE(res_sendImpPeriod,
         "title=\"sending impulse period\";obs",
         sendImpPeriod_get_handler,
         sendImpPeriod_post_handler,
         NULL,
	 NULL,
         sendImpPeriod_event_handler);

/*-----------------------------------------------------Flash-----------------------------------------------------*/

#define RECORDS_PER_SECTOR  10  				// Количество записей одного устройства в одном секторе
#define FIRST_START  2111988					// Идентификатор, наличие которого говорит о том, что система запускается не в первый раз
static short int BLS_ERASE_SECTOR_SIZE = 4096;	// Размер сектора для стирания
static short int INFO_SECTOR = 0;				// Номер для информационного сектора
static short int PARAMS_SECTOR = 1;			    // Номер сектора для параметров
static short int COUNT_IMPULSES = 2;			    // Номер сектора для параметров
static short int HOURS_SECTOR_MIN = 3;			// Начальный сектор для часового архива
static short int HOURS_SECTOR_MAX = 74;			// Конечноый сектор для часового архива
static short int DAYS_SECTOR_MIN  = 75;			// Начальный сектор для дневного архива
static short int DAYS_SECTOR_MAX  = 83;			// Конечный сектор для дневного архива
static short int MONTHS_SECTOR_MIN = 84;		// Начальный сектор для месячного архива
static short int MONTHS_SECTOR_MAX = 85;		// Конечный сектор для месячного архива

//static short int HOURS = 720;					// Размер часового архива
//static short int DAYS = 90;					// Размер дневного архива
//static short int MONTHS = 20;					// Размер месячного архива

static bool FirstSectorAltered = true;			// Был ли первый сектор изменен и следует ли его заного вычитать
static bool SettingsAltered = true;				// Изменились ли настройки
static bool CntImpAltered = true;				// Изменились ли счетчики

enum schedule_type{								// Тип архива
	hours, days, months
};

typedef struct PointImpulse_s{					// Структура 1 записи архива
	unsigned int cntImp;
	unsigned long date;
} PointImpulse;

typedef struct sheduler_data_s{					// Структура RECORDS_PER_SECTOR записей архива
	PointImpulse Data[RECORDS_PER_SECTOR];
} sheduler_data;

typedef struct sheduler_datas_s{				// Структура RECORDS_PER_SECTOR записей архива для COUNT_DEVICE устройств
	sheduler_data Device[COUNT_DEVICE];
} sheduler_datas;

typedef struct cnt_Imp_s{						// Структура количества импульсов для COUNT_DEVICE устройств
	unsigned long cntImp[COUNT_DEVICE];
} cnt_Imp;

typedef struct first_sector_s{					// Структура нулевого сектора
	int first_start;
	int hours_sector;
	int hours_index;
	int day_sector;
	int day_index;
	int month_sector;
	int month_index;
} first_sector;

typedef struct Settings_s{
	int sendCommonImpPeriod;
	int sendImpPeriod;
	int sendHourPeriod;
	int sendDayPeriod;
	int sendMonthPeriod;
	int batteryPeriod;
	short int batteryLowBorder;
	int setOnlinePeriod;
	int silentMode;
	char Serial[30];
} Settings;

//if first_start != FIRST_START it's mean first start
first_sector FirstSector;						// экземпляр структуры Первого сектора
Settings AllSettings;							// экземпляр структуры Настроек
sheduler_datas lastSector;						// Последняя вычитанная из флешки структура sheduler_datas
cnt_Imp cntImpCommon;									// Экземпляр структуры общих Счетчиков импульсов
int lastSectorNumber = -1;						// Номер последнего вычитанного сектора

static int *getCountImp(enum schedule_type type, int maxVal);

static void
load_first_sector()								// вычитать структуру первого сектора в глобальную переменную
{
  printf("\r\n[%s] load_first_sector\r\n", getTimeString());
  if (!FirstSectorAltered) return;
  int rv = ext_flash_open();
  if(!rv) {
    printf("[%s] Could not open flash to load config\n", getTimeString());
    ext_flash_close();
    return;
  }
  rv = ext_flash_read(INFO_SECTOR*BLS_ERASE_SECTOR_SIZE, sizeof(first_sector),
                      (uint8_t *)&FirstSector);
  ext_flash_close();
  if(!rv) {
    printf("[%s] Error loading config\n", getTimeString());
    return;
  }
  if (FirstSectorAltered) FirstSectorAltered = false;
}

static void 
SaveFirstSector(){								// сохранить структуру первого сектора из глобальной переменной
  int rv;
  printf("[%s] Save first sector\r\n", getTimeString());
  rv = ext_flash_open();
  if(!rv) {
    printf("[%s] Could not open flash to save config\n", getTimeString());
    ext_flash_close();
    return;
  }
  rv = ext_flash_erase(INFO_SECTOR*BLS_ERASE_SECTOR_SIZE, sizeof(FirstSector));
  if(!rv) {
    printf("[%s] Error erasing flash\n", getTimeString());
  } else {
    rv = ext_flash_write(INFO_SECTOR*BLS_ERASE_SECTOR_SIZE, sizeof(FirstSector),
                         (uint8_t *)&FirstSector);
    if(!rv) {
      printf("[%s] Error saving config\n", getTimeString());
    }
  }
  ext_flash_close();
  if (!FirstSectorAltered) FirstSectorAltered = true;
}

static void
load_settings()								// вычитать структуру настроек в глобальную переменную

{
  printf("[%s] load settings\r\n", getTimeString());
  if (!SettingsAltered) return;
  int rv = ext_flash_open();
  if(!rv) {
    printf("[%s] Could not open flash to load config\n", getTimeString());
    ext_flash_close();
    return;
  }
  rv = ext_flash_read(PARAMS_SECTOR*BLS_ERASE_SECTOR_SIZE, sizeof(Settings),
                      (uint8_t *)&AllSettings);
  ext_flash_close();
  if(!rv) {
    printf("[%s] Error loading config\n", getTimeString());
    return;
  }
  if (SettingsAltered) SettingsAltered = false;
}

static void 
SaveSettings(){								// сохранить структуру настроек из глобальной переменной
  int rv;
  printf("[%s] Save settings\r\n", getTimeString());
  rv = ext_flash_open();
  if(!rv) {
    printf("[%s] Could not open flash to save config\n", getTimeString());
    ext_flash_close();
    return;
  }
  rv = ext_flash_erase(PARAMS_SECTOR*BLS_ERASE_SECTOR_SIZE, sizeof(Settings));
  if(!rv) {
    printf("[%s] Error erasing flash\n", getTimeString());
  } else {
    rv = ext_flash_write(PARAMS_SECTOR*BLS_ERASE_SECTOR_SIZE, sizeof(Settings),
                         (uint8_t *)&AllSettings);
    if(!rv) {
      printf("[%s] Error saving config\n", getTimeString());
    }
  }
  ext_flash_close();
  if (!SettingsAltered) SettingsAltered = true;
}

static void
load_cntImp()								// вычитать структуру настроек в глобальную переменную
{
  if (!CntImpAltered) return;
  printf("[%s] load cntImp", getTimeString());
  int rv = ext_flash_open();
  if(!rv) {
    printf("[%s] Could not open flash to load config\n", getTimeString());
    ext_flash_close();
    return;
  }
  rv = ext_flash_read(COUNT_IMPULSES*BLS_ERASE_SECTOR_SIZE, sizeof(cntImpCommon),
                      (uint8_t *)&cntImpCommon);
  ext_flash_close();
  if(!rv) {
    printf("[%s] Error loading config\n", getTimeString());
    return;
  }
  if (CntImpAltered) CntImpAltered = false;
}

static void 
SaveCntImp(){								// сохранить структуру настроек из глобальной переменной
  printf("[%s] Записываем cntImpCommon\r\n",getTimeString());
  int rv;
  rv = ext_flash_open();
  if(!rv) {
    printf("[%s] Could not open flash to save config\n", getTimeString());
    ext_flash_close();
    return;
  }
  rv = ext_flash_erase(COUNT_IMPULSES*BLS_ERASE_SECTOR_SIZE, sizeof(cntImpCommon));
  if(!rv) {
    printf("[%s] Error erasing flash\n", getTimeString());
  } else {
    rv = ext_flash_write(COUNT_IMPULSES*BLS_ERASE_SECTOR_SIZE, sizeof(cntImpCommon),
                         (uint8_t *)&cntImpCommon);
    if(!rv) {
      printf("[%s] Error saving config\n", getTimeString());
    }
  }
  ext_flash_close();
  if (!CntImpAltered) CntImpAltered = true;
}

static void										// Сохранить архив на флеш
SaveBunchToFlash(enum schedule_type type, sheduler_datas value)
{
  if (type!=hours){
  	printf("[%s] Saving ", getTimeString());
  }
  short int place;
  short int index;
  switch (type){
	case hours:{place = FirstSector.hours_sector;printf("hours to index = %i",FirstSector.hours_index);index=FirstSector.hours_index;}break;
	case days:{place = FirstSector.day_sector;printf("days to index = %i",FirstSector.day_index);index=FirstSector.day_index;}break;
	case months:{place = FirstSector.month_sector;printf("months to index = %i",FirstSector.month_index);index=FirstSector.month_index;}break;
	default: {index=-1; place=-1;}break;
  }
  printf(" and sector = %i\r\n", place);
  printf("initialize rv\r\n");
  int rv;
  printf("ext_flash_open\r\n");
  rv = ext_flash_open();
  printf("check if ext_flash_open is true\r\n");
  if(!rv) {
    printf("[%s] Could not open flash to save config\n", getTimeString());
    ext_flash_close();
    return;
  }
  printf("It is true. Success opening flash. Let's try to erase flash in place - %i with sector size - %i and with size - %i\r\n", place, BLS_ERASE_SECTOR_SIZE, sizeof(value));
  rv = ext_flash_erase(place*BLS_ERASE_SECTOR_SIZE, sizeof(value));
  printf("Check is erasing is true\r\n");
  if(!rv) {
    printf("[%s] Error erasing flash\n", getTimeString());
  } else {
	printf("It's true. Let's try to write to flash in place - %i with sector size - %i and with size - %i\r\n", place, BLS_ERASE_SECTOR_SIZE, sizeof(value));
    rv = ext_flash_write(place*BLS_ERASE_SECTOR_SIZE, sizeof(value),
                         (uint8_t *)&value);
	printf("Check is saving is success\r\n");
    if(!rv) {
      printf("[%s] Error saving config\n", getTimeString());
    }
	printf("Success saving!!!\r\n");
  }
  printf("ext_flash_close\r\n");
  ext_flash_close();
  printf("lastSector = value\r\n");
  lastSector = value;
  printf("[%s] Hour = %i, Day = %i, Month= %i\r\n", getTimeString(), LastTime.Hour, LastTime.Day, LastTime.Month);
  for (int j=0;j<RECORDS_PER_SECTOR;j++){
	printf(" %s%i|%lu%s",(index==j)?"(":"", value.Device[1].Data[j].cntImp, value.Device[1].Data[j].date, (index==j)?")":"");
  }
  printf("\r\n");
	
  switch (type){
	case hours:{
	  FirstSector.hours_index++;
	  if (FirstSector.hours_index>=RECORDS_PER_SECTOR){
		  FirstSector.hours_index = 0;
		  FirstSector.hours_sector++;
		  if (FirstSector.hours_sector>HOURS_SECTOR_MAX){
			  FirstSector.hours_sector = HOURS_SECTOR_MIN;
		  }
	  }
  	}break;
	case days:{
	  FirstSector.day_index++;
	  if (FirstSector.day_index>=RECORDS_PER_SECTOR){
		  FirstSector.day_index = 0;
		  FirstSector.day_sector++;
		  if (FirstSector.day_sector>DAYS_SECTOR_MAX){
			  FirstSector.day_sector = DAYS_SECTOR_MIN;
		  }
	  }
    }break;
	case months:{
	  FirstSector.month_index++;
	  if (FirstSector.month_index>=RECORDS_PER_SECTOR){
		  FirstSector.month_index = 0;
		  FirstSector.month_sector++;
		  if (FirstSector.month_sector>MONTHS_SECTOR_MAX){
			  FirstSector.month_sector = MONTHS_SECTOR_MIN;
		  }
	  }
  	}break;
  }
  SaveFirstSector();
}

#define PERIODIC_INTERVAL         CLOCK_SECOND
#define KEEP_MAC_ON_MIN_PERIOD 5 /* secs */

static struct stimer st_min_mac_on_duration;

#define MAC_CAN_BE_TURNED_OFF  0
#define MAC_MUST_STAY_ON 1


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
    printf("MAC_MUST_STAY_ON first\n");
    return MAC_MUST_STAY_ON;
  }

#if RPL_WITH_PROBING
  /* Determine if we are about to send a RPL probe */
  if(CLOCK_LT(etimer_expiration_time(
                &rpl_get_default_instance()->probing_timer.etimer),
              (clock_time() + PERIODIC_INTERVAL))) {
    rv = MAC_MUST_STAY_ON;
    printf("MAC_MUST_STAY_ON PROBING\n");
  }
#endif

  /* It's OK to pass a NULL pointer, the callee checks and returns NULL */
  nbr = uip_ds6_nbr_lookup(uip_ds6_defrt_choose());

  if(nbr == NULL) {
    /* We don't have a default route, or it's not reachable (NUD likely). */
    rv = MAC_MUST_STAY_ON;
    printf("MAC_MUST_STAY_ON no default route\n");
  } else {
    if(nbr->state != NBR_REACHABLE) {
      rv = MAC_MUST_STAY_ON;
      printf("MAC_MUST_STAY_ON not NBR REACHABLE\n");
    }
  }

  if(rv == MAC_MUST_STAY_ON && stimer_expired(&st_min_mac_on_duration)) {
    stimer_set(&st_min_mac_on_duration, KEEP_MAC_ON_MIN_PERIOD);
    printf("Set keep mac on period timer\n");
  }

  return rv;
}

static sheduler_datas							// Подгрузить архив из флешки из последней - back ячейки
load_from_flash(enum schedule_type type, int back)
{
  sheduler_datas badnews;
  int ii = - back;
  switch (type){
	case hours:{ii += FirstSector.hours_sector; if (ii<HOURS_SECTOR_MIN){ii=HOURS_SECTOR_MAX-(HOURS_SECTOR_MIN-ii)+1;}}break;
	case days:{ii += FirstSector.day_sector; if (ii<DAYS_SECTOR_MIN){ii=DAYS_SECTOR_MAX-(DAYS_SECTOR_MIN-ii)+1;}}break;
	case months:{ii += FirstSector.month_sector; if (ii<MONTHS_SECTOR_MIN){ii=MONTHS_SECTOR_MAX-(MONTHS_SECTOR_MIN-ii)+1;}}break;
  }
  if (ii!=lastSectorNumber){  
	  if (type!=hours){
		  printf("[%s] %s. loading from sector %i\r\n", getTimeString(),(type==hours)?"Hours":(type==days)?"Days":"Months",ii);
	  }
	  sheduler_datas tmp_cfg;
	  int rv = ext_flash_open();
	  if(!rv) {
		printf("[%s] Could not open flash to load config\n", getTimeString());
		ext_flash_close();
		return badnews;
	  }
	  rv = ext_flash_read(ii*BLS_ERASE_SECTOR_SIZE, sizeof(sheduler_datas),

						  (uint8_t *)&tmp_cfg);
	  ext_flash_close();
	  if(!rv) {
		printf("[%s] Error loading config\n", getTimeString());
		return badnews;
	  }
	  lastSector = tmp_cfg;
	  lastSectorNumber = ii;
	  return tmp_cfg;
  }
  else return lastSector;
}

static void
EraceArchive(){
  printf("[%s] Eracing archive", getTimeString());
  sheduler_datas value;
  PointImpulse emptyObj = {0, 0};
  for (int i=0;i<COUNT_DEVICE;i++){
  	for (int j=0;j<RECORDS_PER_SECTOR;j++){
  		value.Device[i].Data[j] = emptyObj;
  	}
  }
  int rv = ext_flash_open();
  if(!rv) {
	printf("[%s] Could not open flash to save config\n", getTimeString());
	ext_flash_close();
	return;
  }
  for (int i=HOURS_SECTOR_MIN;i<MONTHS_SECTOR_MAX;i++){
	printf("No - %i 4 /r/n",i);
	rv = ext_flash_erase(i*BLS_ERASE_SECTOR_SIZE, sizeof(value));
	if(!rv) {
		printf("[%s] Error erasing flash\n", getTimeString());
	  } else {
		rv = ext_flash_write(i*BLS_ERASE_SECTOR_SIZE, sizeof(value),
							 (uint8_t *)&value);
		if(!rv) {
		  printf("[%s] Error saving config\n", getTimeString());
		}
	  }
  }
  ext_flash_close();
}

static int *
getCountImpLastN(enum schedule_type type){
	int maxValue = (type==hours)?24:DaysInMonth(LastTime.Month, LastTime.Year);
	printf("[%s] getCountImpLastN max = %i, month = %i, Year = %i\r\n", getTimeString(), maxValue, LastTime.Month, LastTime.Year);
	return getCountImp(type,maxValue);
}

char currentMessageString[29]={};
char arrayString[29]={};

static char*
getCountImpLastOne(enum schedule_type type){
	sprintf(arrayString, "[");
	int *countImpArr = getCountImp(type,1);
	for (int i=1;i<COUNT_DEVICE;i++){
		sprintf(arrayString, "%s%s%i", arrayString,(i==1?"":","),countImpArr[i]);
	}
	sprintf(arrayString, "%s]", arrayString);
	return arrayString;
}

int LocalValuesCntImp[COUNT_DEVICE]={};

static int *
getCountImp(enum schedule_type type, int maxVal){
	for (int i=0;i<COUNT_DEVICE;i++){
		LocalValuesCntImp[i] = 0;
	}
	int maxCount = 0;
	int currentIndex=(type==hours)?FirstSector.hours_index:FirstSector.day_index;
	int sectorBack = 0;
	sheduler_datas Sector = load_from_flash(type,sectorBack);
	while (maxCount<maxVal){
		maxCount++;
		currentIndex--;
		printf("maxCount= %i, currentIndex=%i, sectorBack = %i\r\n",maxCount, currentIndex, sectorBack);
		if (currentIndex<0){
			currentIndex=RECORDS_PER_SECTOR-1;
			sectorBack++;
			Sector = load_from_flash(type,sectorBack);
		}
		for (int i=0;i<COUNT_DEVICE;i++){
			long localImp =  Sector.Device[i].Data[currentIndex].cntImp;
			LocalValuesCntImp[i]+=(localImp>0)?localImp:0;
		}
	}
	//printf("[%s] %s archive count = %i\r\n", getTimeString(),(type==hours)?"Dairly":"Monthly", cntImpLoc);
	return LocalValuesCntImp;
}

static void first_start_func(int reset){
	load_first_sector();
	load_settings();
	load_cntImp();
	if (FirstSector.first_start!=FIRST_START || reset == 1 || TEST_PROGRAM){
		EraceArchive();
		printf("[%s] first_start = %i\r\nhours_sector = %i\r\nhours_index = %i\r\nday_sector = %i\r\nday_index = %i\r\nmonth_sector = %i\r\nmonth_index = %i\r\n", getTimeString(),
		FirstSector.first_start,FirstSector.hours_sector,FirstSector.hours_index,FirstSector.day_sector,FirstSector.day_index,FirstSector.month_sector,FirstSector.month_index);
		printf("---------------------------");
		printf("\r\n");
		printf("FirstSector.first_start = %i;\r\n", FIRST_START);
		FirstSector.first_start = FIRST_START;	
		printf("FirstSector.hours_sector = %i;\r\n", HOURS_SECTOR_MIN);
		FirstSector.hours_sector = HOURS_SECTOR_MIN;	
		printf("FirstSector.hours_index = 0;\r\n");
		FirstSector.hours_index = 0;	
		printf("FirstSector.day_sector = %i;\r\n", DAYS_SECTOR_MIN);
		FirstSector.day_sector = DAYS_SECTOR_MIN;	
		printf("FirstSector.day_index = 0;\r\n");
		FirstSector.day_index = 0;	
		printf("FirstSector.month_sector = %i;\r\n", MONTHS_SECTOR_MIN);
		FirstSector.month_sector = MONTHS_SECTOR_MIN;	
		printf("FirstSector.month_index = 0;\r\n");
		FirstSector.month_index = 0;	
		printf("SaveFirstSector();\r\n");
		SaveFirstSector();
		printf("cntImpCommon erase\r\n");
		for (int i=0;i<COUNT_DEVICE;i++){
			cntImpCommon.cntImp[i] = 0;
		}
		printf("SaveCntImp\r\n");
		SaveCntImp();
		printf("AllSettings.setOnlinePeriod=15;\r\n");
		AllSettings.setOnlinePeriod=15;
		printf("AllSettings.sendCommonImpPeriod=20;\r\n");
		AllSettings.sendCommonImpPeriod=20;
		printf("AllSettings.batteryPeriod=86400;\r\n");
		AllSettings.batteryPeriod=86400;
		printf("AllSettings.batteryLowBorder=2850;\r\n");
		AllSettings.batteryLowBorder=2850;
		printf("AllSettings.silentMode=1;\r\n");
		AllSettings.silentMode=1;
		printf("strcpy(AllSettings.Serial, '0000-0000-0000-0000');\r\n");
		strcpy(AllSettings.Serial, "0000-0000-0000-0000");
		printf("SaveSettings();\r\n");
		SaveSettings();
	}
	printf("[%s] first_start = %i\r\nhours_sector = %i\r\nhours_index = %i\r\nday_sector = %i\r\nday_index = %i\r\nmonth_sector = %i\r\nmonth_index = %i\r\n", getTimeString(),
		  FirstSector.first_start,FirstSector.hours_sector,FirstSector.hours_index,FirstSector.day_sector,FirstSector.day_index,FirstSector.month_sector,FirstSector.month_index);
}

static void
FriteArchiveToFlash(enum schedule_type type, unsigned long currentTime){
	int currentIndex=(type==hours)?FirstSector.hours_index:(type==days)?FirstSector.day_index:FirstSector.month_index;
	printf("[%s] currentIndex = %i\r\n", getTimeString(), currentIndex);
	sheduler_datas valuej = load_from_flash(type,0);
	int *LocalValues = LocalValuesCntImp;
	if (type == days){
		LocalValues = getCountImpLastN(hours);
	}
	if (type == months){
		LocalValues = getCountImpLastN(days);
	}
	for (int i=0;i<COUNT_DEVICE;i++){
		printf("FriteArchiveToFlash month = %i, Year = %i\r\n", LastTime.Month, LastTime.Year);
		int countLocal = (cntImp[i]<0)?0:cntImp[i];
		if (type == hours){
			cntImp[i] = 0;
		}
		if (type == days || type == months){
			printf("[%s] value = %i for input - %i\r\n", getTimeString(), LocalValues[i], i);
			countLocal = LocalValues[i];
		}
		PointImpulse pImp = {countLocal, currentTime};
		valuej.Device[i].Data[currentIndex]=pImp;
	}
	SaveBunchToFlash(type, valuej);
}

static void 
ExecutePlans(){
	unsigned long currentTime = getCurrentTimeSec();
 	Customtm tm = SecondsToDate(currentTime);
	if (LastTime.Hour!=tm.Hour){
		printf("[%s] Начали записывать часы\r\n", getTimeString());
		FriteArchiveToFlash(hours, currentTime);
		//ToDo: алгоритм долгого простоя
		LastTime.Hour = tm.Hour;
		res_hour.trigger();
		printf("[%s] Закончили записывать часы\r\n", getTimeString());
	}
	if (LastTime.Day!=tm.Day){
		printf("[%s] Начали записывать дни\r\n", getTimeString());
		FriteArchiveToFlash(days, currentTime);
		//ToDo: алгоритм долгого простоя
		LastTime.Day = tm.Day;
		res_day.trigger();
		printf("[%s] Закончили записывать дни\r\n", getTimeString());
	}
	if (LastTime.Month!=tm.Month){
		printf("[%s] Начали записывать месяца\r\n", getTimeString());
		
		FriteArchiveToFlash(months, currentTime);
		//ToDo: алгоритм долгого простоя
		LastTime.Month = tm.Month;
		res_month.trigger();
		printf("[%s] Закончили записывать месяца\r\n", getTimeString());
	}
}

/*--------------/Flash-----------------*/

//Мигание лампочками
static void LedsToggle(){
 if (AllSettings.silentMode==1){
 	 led_clear();
	 leds_off(0xFF);
	 led_set();
	 /*leds_on(leds_state);
	 leds_state ++;
	 if (leds_state>2) { leds_state = 1; }*/
 }
}

static bool 
checkBattery(bool showLog){

    int voltage = batmon_sensor.value(BATMON_SENSOR_TYPE_VOLT);
    if (showLog){
		printf("[%s] Battery. current = %i, min - %i\r\n", getTimeString(),((voltage * 125) >> 5),AllSettings.batteryLowBorder);
    }
    if (((voltage * 125) >> 5) < AllSettings.batteryLowBorder){
	return false;
    }
    return true;
}

static bool 
checkShortcircuit(bool showLog){
	bool kz = scifTaskData.newTask.output.channelErrorCh1==2 || scifTaskData.newTask.output.channelErrorCh2==2;
    if (showLog){
		printf("[%s] Проверка короткого замыкания = %s (%i,%i)\r\n",getTimeString(),kz?"True":"False", scifTaskData.newTask.output.channelErrorCh1,scifTaskData.newTask.output.channelErrorCh2);
    }
	return kz;
}

static bool 
checkOpencircuit(bool showLog){
	bool oc = scifTaskData.newTask.output.channelErrorCh1==1 || scifTaskData.newTask.output.channelErrorCh2==1;
    if (showLog){
		printf("[%s] Проверка обрыва сети - %s (%i,%i)\r\n",getTimeString(),oc?"True":"False", scifTaskData.newTask.output.channelErrorCh1,scifTaskData.newTask.output.channelErrorCh2);
    }
    return oc;
}

static void
checkDanger(){
	if (!checkBattery(true)){
		  res_danger_battery.trigger();
	}
	if (checkShortcircuit(true)){
		  res_shortcircuit.trigger();
	}
	if (checkOpencircuit(true)){
		  res_opencircuit.trigger();
	}
}

static void
switchSleeping(bool sleep){
	if (!isSleeping && sleep && !cantSleep && globalSleepAllowed==1){
		uint8_t mac_keep_on = keep_mac_on();
		printf("mac_keep_on is %d\n", mac_keep_on);
		if(mac_keep_on == MAC_CAN_BE_TURNED_OFF){
			isSleeping = true;
			printf("[%s] mac_off\r\n", getTimeString());
			NETSTACK_MAC.off(0);
			leds_off(0xFF);
			led_clear();
		}
		else if (etimer_expired(&network_on_timer)){
				etimer_set(&network_on_timer,CLOCK_SECOND);
			 }
	} else
	if (isSleeping==true && !sleep){
		isSleeping = false;
       	printf("[%s] mac_on\r\n", getTimeString());
		NETSTACK_MAC.on();
		LedsToggle();
		checkDanger();
	}
}

static void start_indicate(enum blink_mode mode, bool ignoreSettings){
if (AllSettings.silentMode==0 || ignoreSettings==true){
 if (mode == transfer){
     if (isIndicateGreen == false && ignoreSettings==true){
	     //isSleeping = true;
		 //switchSleeping(false);
		 isIndicateRed = false;
		 isIndicateGreen = true;
		 //leds_off(LEDS_GREEN);
		 led_clear();
		 currentBlinks = 0;
		 etimer_set(&blink_timer_green,blinkDelayGreen);
		 printf("[%s] green timer started\r\n", getTimeString());
	 }
     else if (isIndicateGreen == true) printf("green indicate already started\r\n");
 }
 if (mode == findNetwork){
     if (isIndicateRed == false){
	 isIndicateRed = true;
	 leds_off(LEDS_RED);
	 led_clear();
	 //led_clear();
	 currentBlinks = 0;
	 etimer_set(&blink_timer_red,blinkDelayRed);
     }
     else printf("[%s] red indicate already started/r/n", getTimeString());
 }
}
}

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

static void
restore_default_params(){
	/*cantSleep=true;
    switchSleeping(false);
    etimer_stop(&delay_timer);
	etimer_stop(&send_CommonImp_timer);
	etimer_stop(&network_on_timer);
	etimer_stop(&blink_timer_green);
	etimer_stop(&blink_timer_red);
	etimer_stop(&button_timer);
	first_start_func(1);
	printf("10");
	system_started = false;
	printf("11");
	first_start = true;
	cantSleep=false;*/
	FirstSector.first_start=0;
	SaveFirstSector();
	ti_lib_sys_ctrl_system_reset();
}

//Обработка импульса
static int GetImpulse(int device, bool invert){
 //ExecutePlans();
 int valReed = 0;
 if (device==1){
	 valReed = reed_relay_sensor_red.value(0);
 } else  if (device==2){
	 valReed = reed_relay_sensor_green.value(0);
 } else  if (device==0){
	 valReed = reed_relay_sensor.value(0);
 } else  if (device==3){
	 valReed = reed_relay_sensor_opening.value(0);
 }
 //printf("value = %d", valReed);
 bool impulseUp = false;
 if(invert == true){
	 impulseUp = valReed == 0;
 } else {
	 impulseUp = valReed != 0;
 }
 if (impulseUp)
 {
   clock_wait(DREBEZG_TIME);
   if (device==1){
	 valReed = reed_relay_sensor_red.value(0);
	} else if (device==2){
	 valReed = reed_relay_sensor_green.value(0);
	} else if (device==0){
	 valReed = reed_relay_sensor.value(0);
	} else if (device==3){
	 valReed = reed_relay_sensor_opening.value(0);
	}
    if(invert == true){
		 impulseUp = valReed == 0;
	 } else {
		 impulseUp = valReed != 0;
	 }
   if (impulseUp){
    if (device==3){
	  printf("[%s] Roof is gone!\r\n", getTimeString());
	}
	else{
	  cntImp[device]++;
	  cntImpCommon.cntImp[device]++;
	  SaveCntImp();
	  printf("[%s] impulse-%lu(%d)input-%d;\r\n",getTimeString(), cntImp[device],valReed,device);
	}
	return 1;
   }
   else printf("[%s] DREBEZG!!!\r\n", getTimeString());
  }
  return 0;
}

static char*
getMessageString(char* val){
  sprintf(currentMessageString, "{'time':'%s', 'values':%s}", getTimeString(), val);
  return currentMessageString;
}

static void
res_get_serial(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "{serial:'%s',countDevices:%i, types:[%i,%i]}", AllSettings.Serial, COUNT_DEVICE-1, scifTaskData.newTask.output.typeCh1, scifTaskData.newTask.output.typeCh2));
}

static void
res_post_serial(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  uint8_t *incoming = NULL;
  unsigned int ct = -1;

  REST.get_header_content_type(request, &ct);

  int obs_content_len = REST.get_request_payload(request,(const uint8_t **)&incoming);
  char obs_content[obs_content_len];
  memcpy(obs_content, incoming, obs_content_len);
  memset(AllSettings.Serial, '\0', obs_content_len);
  strcpy(AllSettings.Serial, obs_content);
  printf("[%s] AllSettings.Serial = %s\r\n", getTimeString(), AllSettings.Serial);
  REST.set_response_status(response, REST.status.CHANGED);
  SaveSettings();
}

static void
res_get_speed(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%i", speed));
}

static void
res_post_speed(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  uint8_t *incoming = NULL;
  unsigned int ct = -1;

  REST.get_header_content_type(request, &ct);

  int obs_content_len = REST.get_request_payload(request,(const uint8_t **)&incoming);
  char obs_content[obs_content_len];
  memcpy(obs_content, incoming, obs_content_len);
  speed = atoi(obs_content);
  printf("[%s] speed = %i\r\n", getTimeString(), speed);
  REST.set_response_status(response, REST.status.CHANGED);
}

static void
res_get_time(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  char const *const message = getTimeString();//obs_content;
  int length = strlen(message); /*           |<-------->| */
  memcpy(buffer, message, length);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_header_etag(response, (uint8_t *)&length, 1);
  REST.set_response_payload(response, buffer, length);
}

static void
res_put_time(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  uint8_t *incoming = NULL;
  unsigned int ct = -1;

  REST.get_header_content_type(request, &ct);

    int obs_content_len = REST.get_request_payload(request,(const uint8_t **)&incoming);
	char obs_content[obs_content_len];
    memcpy(obs_content, incoming, 19);
    //sprintf(obs_content, "%s", year);
  printf("[%s] Было время LastTime.Hour=%i, LastTime.Day=%i, LastTime.Month=%i, LastTime.Year=%i\r\n", getTimeString(), LastTime.Hour, LastTime.Day, LastTime.Month,LastTime.Year);
  Customtm lastTimeLocal = get_time_from_string(obs_content);
  LastTime.Year = lastTimeLocal.Year;
  LastTime.Month = lastTimeLocal.Month;
  LastTime.Day = lastTimeLocal.Day;
  LastTime.Hour = lastTimeLocal.Hour;
  LastTime.Minute = lastTimeLocal.Minute;
  LastTime.Second = lastTimeLocal.Second;
  printf("[%s] Получено время LastTime.Hour=%i, LastTime.Day=%i, LastTime.Month=%i, LastTime.Year=%i\r\n", getTimeString(), LastTime.Hour, LastTime.Day, LastTime.Month, 	LastTime.Year);
  unsigned long current_time = clock_seconds();
  addTime = DateToSeconds(LastTime) - current_time;
  REST.set_response_status(response, REST.status.CHANGED);
  if (!system_started)
  {
    system_started = true;
  }
  //Should we change LastTime every time Current time changed?
}

/*static void 
rssi_event_handler(void)
{
	REST.notify_subscribers(&res_rssi);
}*/

/*static void 
reboot_event_handler(void)
{
	REST.notify_subscribers(&res_reboot);
}*/

static void 
speed_event_handler(void)
{
  REST.notify_subscribers(&res_speed);
}

static void 
time_event_handler(void)
{
  REST.notify_subscribers(&res_time);
}

static void 
commonImp_event_handler(void)
{
  REST.notify_subscribers(&res_commonImp);
}

static int GetCountImpFromSensorController(int inputNum, bool clear){
	//чтение импульса из сенсор-контроллера
	printf("[%s] Reading data from SC\r\n", getTimeString());
	int outValue = 0;
	switch(inputNum){
		case 1:{
		 	if (scifTaskData.newTask.output.typeCh1 == 2){
				outValue = scifTaskData.newTask.output.counterNamurCh1;
			}
		    else if (scifTaskData.newTask.output.typeCh1 == 1){
				outValue = scifTaskData.newTask.output.counterReedCh1;
			}
			if (clear) scifTaskData.newTask.state.clearCounterCh1 = 1;
		} break;
		case 2:{ 
			if (scifTaskData.newTask.output.typeCh2 == 2){
				outValue = scifTaskData.newTask.output.counterNamurCh2;
			}
		    else if (scifTaskData.newTask.output.typeCh2 == 1){
				outValue = scifTaskData.newTask.output.counterReedCh2;
			}
			if (clear) scifTaskData.newTask.state.clearCounterCh2 = 1;
		} break;
	}

	return outValue;
}

static void
res_get_commonImp(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
	char arrayStringLocal[29]={};
	for (int i=1;i<COUNT_DEVICE;i++){
		sprintf(arrayStringLocal, "%s%s%lu", arrayStringLocal,(i==1?"":","),(cntImpCommon.cntImp[i]+GetCountImpFromSensorController(i, false)));
	}
	sprintf(arrayString, "[%s]", arrayStringLocal);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%s", getMessageString(arrayString)));
}

/*static void
rssi_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  start_indicate(transfer, ignore_settings);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); 
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%i", sicslowpan_get_last_rssi()));
}*/

static void
sendCommonImpPeriod_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  uint8_t *incoming = NULL;
  unsigned int ct = -1;

  REST.get_header_content_type(request, &ct);

  int obs_content_len = REST.get_request_payload(request,(const uint8_t **)&incoming);
  char obs_content[obs_content_len];
  memcpy(obs_content, incoming, obs_content_len);
  AllSettings.sendCommonImpPeriod = atoi(obs_content);
  if (AllSettings.sendCommonImpPeriod < 10)
	AllSettings.sendCommonImpPeriod = 10;
  if (AllSettings.setOnlinePeriod>AllSettings.sendCommonImpPeriod - 5)
	AllSettings.sendCommonImpPeriod = AllSettings.setOnlinePeriod + 5;
  printf("[%s] commonImpPeriod = %i\r\n", getTimeString(), AllSettings.sendCommonImpPeriod);
  REST.set_response_status(response, REST.status.CHANGED);
  SaveSettings();
  res_sendCommonImpPeriod.trigger();
}

static void
setOnlinePeriod_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  uint8_t *incoming = NULL;
  unsigned int ct = -1;

  REST.get_header_content_type(request, &ct);

  int obs_content_len = REST.get_request_payload(request,(const uint8_t **)&incoming);
  char obs_content[obs_content_len];
  memcpy(obs_content, incoming, obs_content_len);
  AllSettings.setOnlinePeriod = atoi(obs_content);
  if (AllSettings.setOnlinePeriod>AllSettings.sendCommonImpPeriod - 5  && AllSettings.sendCommonImpPeriod!=0)
	AllSettings.setOnlinePeriod = AllSettings.sendCommonImpPeriod - 5;
  printf("[%s] commonImpPeriod = %i\r\n", getTimeString(), AllSettings.setOnlinePeriod);
  REST.set_response_status(response, REST.status.CHANGED);
  SaveSettings();
  res_setOnlinePeriod.trigger();
}

static void
silentMode_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  uint8_t *incoming = NULL;
  unsigned int ct = -1;

  REST.get_header_content_type(request, &ct);

  int obs_content_len = REST.get_request_payload(request,(const uint8_t **)&incoming);
  char obs_content[obs_content_len];
  memcpy(obs_content, incoming, obs_content_len);
  AllSettings.silentMode = atoi(obs_content);
  if (AllSettings.silentMode>1)
	AllSettings.silentMode = 0;
  printf("[%s] silentMode = %i\r\n", getTimeString(), AllSettings.silentMode);
  REST.set_response_status(response, REST.status.CHANGED);
  SaveSettings();
  res_silentMode.trigger();
  //start_indicate(transfer, ignore_settings);
}

static void 
battery_event_handler(void)
{
  REST.notify_subscribers(&res_battery);
}

static void 
battery_event_danger_handler(void)
{
  REST.notify_subscribers(&res_danger_battery);
}

static void 
shortcircuit_event_danger_handler(void)
{
  REST.notify_subscribers(&res_shortcircuit);
}

static void 
opencircuit_event_danger_handler(void)
{
  REST.notify_subscribers(&res_opencircuit);
}

static void 
opening_event_danger_handler(void)
{
  REST.notify_subscribers(&res_danger_opening);
}

static void 
hour_event_handler(void)
{
	REST.notify_subscribers(&res_hour);
}

static void 
day_event_handler(void)
{
	REST.notify_subscribers(&res_day);
}

static void 
month_event_handler(void)
{
	REST.notify_subscribers(&res_month);
}

static void 
sendCommonImpPeriod_event_handler(void)
{
	REST.notify_subscribers(&res_sendCommonImpPeriod);
}

static void 
setOnlinePeriod_event_handler(void)
{
	REST.notify_subscribers(&res_setOnlinePeriod);
}

static void 
silentMode_event_handler(void)
{
	REST.notify_subscribers(&res_silentMode);
}

static void 
batteryPeriod_event_handler(void)
{
	REST.notify_subscribers(&res_batteryPeriod);
}

static void 
batteryLowBorder_event_handler(void)
{
	REST.notify_subscribers(&res_batteryLowBorder);
}

static void 
sendImpPeriod_event_handler(void)
{
	REST.notify_subscribers(&res_sendImpPeriod);
}

static void
res_get_battery(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  unsigned int accept = -1;
  int voltage;

  if(request != NULL) {
    REST.get_header_accept(request, &accept);
  }

  voltage = batmon_sensor.value(BATMON_SENSOR_TYPE_VOLT);

    REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
    snprintf((char *)buffer, REST_MAX_CHUNK_SIZE, "Voltage=%dmV", (voltage * 125) >> 5);

    REST.set_response_payload(response, buffer, strlen((char *)buffer));
}

static void
res_get_danger_battery(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  unsigned int accept = -1;
  bool voltageCheck = checkBattery(true);

  if(request != NULL) {
    REST.get_header_accept(request, &accept);
  }

    REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
    snprintf((char *)buffer, REST_MAX_CHUNK_SIZE, voltageCheck?"Normal":"DANGER!!!");

    REST.set_response_payload(response, buffer, strlen((char *)buffer));
}

static void
res_get_danger_shortcircuit(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  unsigned int accept = -1;

  if(request != NULL) {
    REST.get_header_accept(request, &accept);
  }

    REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
    snprintf((char *)buffer, REST_MAX_CHUNK_SIZE, "['%s','%s']",scifTaskData.newTask.output.channelErrorCh1==2?"DANGER!!!":"Normal",scifTaskData.newTask.output.channelErrorCh2==2?"DANGER!!!":"Normal");
	printf("Обнуляем ошибку");
	scifTaskData.newTask.state.clearErrorCh1 = 1;
	scifTaskData.newTask.state.clearErrorCh2 = 1;
	checkShortcircuit(true);
    REST.set_response_payload(response, buffer, strlen((char *)buffer));
}

static void
res_get_danger_opencircuit(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  unsigned int accept = -1;

  if(request != NULL) {
    REST.get_header_accept(request, &accept);
  }

    REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
    snprintf((char *)buffer, REST_MAX_CHUNK_SIZE, "['%s','%s']",scifTaskData.newTask.output.channelErrorCh1==1?"DANGER!!!":"Normal",scifTaskData.newTask.output.channelErrorCh2==1?"DANGER!!!":"Normal");

    REST.set_response_payload(response, buffer, strlen((char *)buffer));
}

static void
res_get_danger_opening(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  unsigned int accept = -1;
  int opening = GetImpulse(3, true);

  if(request != NULL) {
    REST.get_header_accept(request, &accept);
  }

    REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
    snprintf((char *)buffer, REST_MAX_CHUNK_SIZE, opening==0?"Normal":"DANGER!!!");

    REST.set_response_payload(response, buffer, strlen((char *)buffer));
}

static void
batteryPeriod_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  uint8_t *incoming = NULL;
  unsigned int ct = -1;

  REST.get_header_content_type(request, &ct);

  int obs_content_len = REST.get_request_payload(request,(const uint8_t **)&incoming);
  char obs_content[obs_content_len];
  memcpy(obs_content, incoming, obs_content_len);
  AllSettings.batteryPeriod = atoi(obs_content);
  printf("[%s] batteryPeriod = %i\r\n", getTimeString(), AllSettings.batteryPeriod);
  REST.set_response_status(response, REST.status.CHANGED);
  SaveSettings();
  res_batteryPeriod.trigger();
}

static void
batteryLowBorder_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  uint8_t *incoming = NULL;
  unsigned int ct = -1;

  REST.get_header_content_type(request, &ct);

  int obs_content_len = REST.get_request_payload(request,(const uint8_t **)&incoming);
  char obs_content[obs_content_len];
  memcpy(obs_content, incoming, obs_content_len);
  AllSettings.batteryLowBorder = atoi(obs_content);
  printf("[%s] batteryLowBorder = %i\r\n", getTimeString(), AllSettings.batteryLowBorder);
  REST.set_response_status(response, REST.status.CHANGED);
  SaveSettings();
  res_batteryLowBorder.trigger();
}

static void
sendImpPeriod_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%i", AllSettings.sendImpPeriod));
}

static void
hour_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  printf("[%s] Отправляем часы\r\n", getTimeString());
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%s", getMessageString(getCountImpLastOne(hours))));

  //start_indicate(transfer, ignore_settings);
  /*char* message = getMessageString(getCountImpLastOne(hours,currentDevice), currentDevice);//obs_content;
  int length = strlen(message); 
  memcpy(buffer, message, length);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
  REST.set_response_payload(response, buffer, length);*/
}

static void
day_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  printf("[%s] Отправляем дниr\n", getTimeString());
  //start_indicate(transfer, ignore_settings);
  char* message = getMessageString(getCountImpLastOne(days));//obs_content;
  int length = strlen(message); /*           |<-------->| */
  memcpy(buffer, message, length);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, length);
}

static void
month_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  printf("[%s] Отправляем месяца\r\n", getTimeString());
  //start_indicate(transfer, ignore_settings);
  char* message = getMessageString(getCountImpLastOne(months));//obs_content;
  int length = strlen(message); /*           |<-------->| */
  memcpy(buffer, message, length);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, length);
}

static void
sendImpPeriod_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  uint8_t *incoming = NULL;
  unsigned int ct = -1;

  REST.get_header_content_type(request, &ct);

  int obs_content_len = REST.get_request_payload(request,(const uint8_t **)&incoming);
  char obs_content[obs_content_len];
  memcpy(obs_content, incoming, obs_content_len);
  AllSettings.sendImpPeriod = atoi(obs_content);
  printf("[%s] sendImpPeriod = %i\r\n", getTimeString(), AllSettings.sendImpPeriod);
  REST.set_response_status(response, REST.status.CHANGED);
  SaveSettings();
  res_sendImpPeriod.trigger();
}

static void
sendCommonImpPeriod_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%i", AllSettings.sendCommonImpPeriod));
}

static void
setOnlinePeriod_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%i", AllSettings.setOnlinePeriod));
}

static void
silentMode_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%i", AllSettings.silentMode));
}

static void
batteryPeriod_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%i", AllSettings.batteryPeriod));
}

static void
batteryLowBorder_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%i", AllSettings.batteryLowBorder));
}

static void
allowSleeping_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  //start_indicate(transfer, ignore_settings);
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%i", globalSleepAllowed));
}

static void
reboot_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)

{
  //start_indicate(transfer, ignore_settings);
  uint8_t *incoming = NULL;
  unsigned int ct = -1;

  REST.get_header_content_type(request, &ct);

  int obs_content_len = REST.get_request_payload(request,(const uint8_t **)&incoming);
  char obs_content[obs_content_len];
  memcpy(obs_content, incoming, obs_content_len);
  int pass = atoi(obs_content);
  printf("[%s] pass = %i\r\n", getTimeString(), pass);
  if (pass == PIN_Reboot){
   ti_lib_sys_ctrl_system_reset();
  }
  else REST.set_response_status(response, REST.status.NOT_ACCEPTABLE);
}

static short int delayWithSpeed = 0;

/*static void
controlTimers(int allow){
  if (system_started){
	if (globalSleepAllowed == 0 && allow == 1)
		{
			printf("change globalSleepAllowed to 1\r\n");
		    globalSleepAllowed = 1;
			if (first_start){
				printf("start system\r\n");
				first_start = false;
				if (TEST_PROGRAM){
					printf("TEST MODE!!!!!");
					etimer_set(&delay_timer,CLOCK_SECOND);
				}
				else{
					etimer_set(&delay_timer,CLOCK_SECOND*AllSettings.sendCommonImpPeriod);
					//etimer_set(&delay_timer,CLOCK_SECOND);
					printf("start send_CommonImp_timer with time %i\r\n", AllSettings.sendCommonImpPeriod);
					//run sensorcontroller get data timer
					Customtm tmCur = SecondsToDate(getCurrentTimeSec());
					int delaySCMin = 60 - tmCur.Minute - 1;
					int delaySCSec = 60 - tmCur.Second;
					int delaySCSecCommon = delaySCSec + delaySCMin * 60;
					delayWithSpeed = delaySCSecCommon/speed;
					//if (delaySCSecCommon<=0) delaySCSecCommon = 3600 - 1;
					printf("[%s] after %i seconds data from sensorcontroller will be written\r\n", getTimeString(), delayWithSpeed);
					etimer_set(&sct,CLOCK_SECOND*delayWithSpeed);
				}
			}
			if (!TEST_PROGRAM){
				cantSleep = false;
				switchSleeping(true);
			}
		} else
	if (globalSleepAllowed == 1 && allow == 0){
		printf("change globalSleepAllowed to 0\r\n");
		globalSleepAllowed = 0;
		cantSleep = true;
		switchSleeping(false);
	}
  }
  else printf("Cant sleep because system not started. Fill all params and set time\r\n");
}*/

static void
allowSleeping_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)

{
  //start_indicate(transfer, ignore_settings);
  uint8_t *incoming = NULL;
  unsigned int ct = -1;

  REST.get_header_content_type(request, &ct);

  int obs_content_len = REST.get_request_payload(request,(const uint8_t **)&incoming);
  char obs_content[obs_content_len];
  memcpy(obs_content, incoming, obs_content_len);
  int allow = atoi(obs_content);
  if (allow>1){ allow = 1; }
  if (allow<0){ allow = 0; }
  if (allow != globalSleepAllowed){
   printf("[%s] allowSleeping = %s\r\n", getTimeString(), allow==1?"True":"False");
	  globalSleepAllowed= allow;
   //controlTimers(allow);
  }
  else { 
	  printf("[%s] cannot change allowSleeping from %i to %i\r\n", getTimeString(), globalSleepAllowed ,allow);
	  REST.set_response_status(response, REST.status.NOT_ACCEPTABLE);
  }
}

static void Sensor_activate(){
	SENSORS_ACTIVATE(batmon_sensor);
	SENSORS_ACTIVATE(button_left_sensor);
	SENSORS_ACTIVATE(button_right_sensor);
	SENSORS_ACTIVATE(reed_relay_sensor);
	SENSORS_ACTIVATE(reed_relay_sensor_red);
	SENSORS_ACTIVATE(reed_relay_sensor_green);
	SENSORS_ACTIVATE(reed_relay_sensor_opening);
	SENSORS_ACTIVATE(button_red_sensor);
	SENSORS_ACTIVATE(button_green_sensor);
	SENSORS_ACTIVATE(button_opening_sensor);
}

static void Resourse_Activate(){
    //rest_activate_resource(&res_cntImp, "rest/cntImp");
	//rest_activate_resource(&res_rssi, "rest/last_rssi");
	rest_activate_resource(&res_speed, "rest/speed");
	rest_activate_resource(&res_commonImp, "rest/commonImp");
	rest_activate_resource(&res_battery, "rest/battery");
	rest_activate_resource(&res_danger_battery, "rest/danger/battery");
	rest_activate_resource(&res_danger_opening, "rest/danger/opening");
	rest_activate_resource(&res_shortcircuit, "rest/danger/shortcircuit");
	rest_activate_resource(&res_opencircuit, "rest/danger/opencircuit");
	
	rest_activate_resource(&res_hour, "rest/shedule/hour");
	rest_activate_resource(&res_day, "rest/shedule/day");
	rest_activate_resource(&res_month, "rest/shedule/month");
	
	rest_activate_resource(&res_time, "rest/params/time");
	rest_activate_resource(&res_sendCommonImpPeriod, "rest/params/sendCommonImpPeriod");
	rest_activate_resource(&res_setOnlinePeriod, "rest/params/onlinePeriod");
	rest_activate_resource(&res_silentMode, "rest/params/silentMode");
	rest_activate_resource(&res_batteryPeriod, "rest/params/batteryPeriod");
	rest_activate_resource(&res_batteryLowBorder, "rest/params/batteryLowBorder");
	rest_activate_resource(&res_serial, "rest/params/serial");
	rest_activate_resource(&res_allowSleeping, "rest/params/allowSleeping");
	rest_activate_resource(&res_reboot, "rest/REDBUTTON/NOTPUSH");
}

static void initFlash(){
	ext_flash_init();
	first_start_func(0);
}

PROCESS(lpm_test_process, "lpm test process");
AUTOSTART_PROCESSES(&lpm_test_process);
LPM_MODULE(lpm_module, NULL, shutdown_handler, wakeup_handler, LPM_DOMAIN_PERIPH);

static void
initialize(){
	clock_init();
	rest_init_engine();
	initFlash();
	if (!TEST_PROGRAM) start_indicate(findNetwork, true);
	Resourse_Activate();
	Sensor_activate();
	lpm_shutdown_event = process_alloc_event();
	lpm_wake_event = process_alloc_event();
	lpm_register_module(&lpm_module);
}

static void
initializeSC(){
	//start sensorcontroller
	aux_ctrl_register_consumer(&sc_aux);
	scifInit(&scifDriverSetup);
	scifExecuteTasksOnceNbl(BV(SCIF_NEW_TASK_TASK_ID));
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
}

static int counter = 0;

PROCESS_THREAD(lpm_test_process, ev, data){
	PROCESS_BEGIN();
	initialize();
	led_init();
	initializeSC();

	while(1) {
        PROCESS_WAIT_EVENT();
	if ((system_started && AllSettings.setOnlinePeriod!=0 && AllSettings.sendCommonImpPeriod!=0 && AllSettings.batteryPeriod!=0 && AllSettings.batteryLowBorder!=0) || TEST_PROGRAM)
		{
			if (first_start)
			{
				printf("[%s] start system\r\n", getTimeString());
				if (TEST_PROGRAM){
					printf("TEST MODE!!!!!");
					etimer_set(&delay_timer,CLOCK_SECOND);

				}
				else{
					etimer_set(&delay_timer,CLOCK_SECOND*AllSettings.sendCommonImpPeriod);
					//etimer_set(&delay_timer,CLOCK_SECOND);
					printf("[%s} start send_CommonImp_timer with time %i\r\n", getTimeString(), AllSettings.sendCommonImpPeriod);
					//run sensorcontroller get data timer
 					Customtm tmCur = SecondsToDate(getCurrentTimeSec());
					int delaySCMin = 60 - tmCur.Minute - 1;
					int delaySCSec = 60 - tmCur.Second;
					int delaySCSecCommon = delaySCSec + delaySCMin * 60;
					delayWithSpeed = delaySCSecCommon/speed;
					//if (delaySCSecCommon<=0) delaySCSecCommon = 3600 - 1;
					printf("[%s] after %i seconds data from sensorcontroller will be written\r\n", getTimeString(), delayWithSpeed);
					etimer_set(&sct,CLOCK_SECOND*delayWithSpeed);
					cantSleep = false;
					switchSleeping(true);
				}
			}
			if (ev == PROCESS_EVENT_TIMER){
				if (data == &sct){
					int addValue = delayWithSpeed*speed-delayWithSpeed;
					printf("[%s] add %i seconds to mainTime\r\n", getTimeString(), addValue);
					addTime += addValue;
					if (delayWithSpeed != 3600 / speed){
						delayWithSpeed = 3600 / speed;
						printf("delayWithSpeed - %i\r\n", delayWithSpeed);
					}
					etimer_set(&sct,CLOCK_SECOND*delayWithSpeed);
					printf("[%s] added %lu seconds to main time", getTimeString(), addTime);
					printf("[%s] after %i seconds data from sensorcontroller will be written\r\n", getTimeString(), delayWithSpeed);
					int count1 = GetCountImpFromSensorController(1, true);
					cntImp[1] = count1;
					cntImpCommon.cntImp[1] += count1;
					int count2 = GetCountImpFromSensorController(2, true);
					cntImp[2] = count2;
					cntImpCommon.cntImp[2] += count2;
					SaveCntImp();
					ExecutePlans();
					checkDanger();
				}
				if (data == &delay_timer)
				{
					printf("[%s] delay_timer tick\r\n", getTimeString());
					switchSleeping(false);
					cantSleep = true;
					if (TEST_PROGRAM){
					  counter++;
					  cntImpCommon.cntImp[0] = cntImpCommon.cntImp[1] = cntImpCommon.cntImp[2] = cntImp[0] = cntImp[1] = cntImp[2] = counter;
					  //addTime += 3599; //Add day
					  addTime += 1000; //Add 1/3 of day
					  printf("%i.%i.%i %i:%i:%i\r\n", LastTime.Year,LastTime.Month,LastTime.Day,LastTime.Hour,LastTime.Minute,LastTime.Second);
					  etimer_set(&delay_timer,CLOCK_SECOND);
					  if (counter>23) counter=0;
					}
					else{
						printf("[%s] send_CommonImp_timer tick\r\n", getTimeString());
						res_commonImp.trigger();
						//Стартуем таймер, который должен усыпить контроллер
						etimer_set(&network_on_timer,CLOCK_SECOND*AllSettings.setOnlinePeriod);
						//Запускаем таймер, который должен пробудить контроллер
						etimer_set(&delay_timer,CLOCK_SECOND*AllSettings.sendCommonImpPeriod);
					}
					//ExecutePlans();
					//res_cntImp.trigger();
				}
				if (data == &network_on_timer)
				{
					printf("[%s] network_on_timer tick\r\n", getTimeString());
					cantSleep = false;
					switchSleeping(true);
				}
				if (data == &button_timer){
					if (countPressed == 3){
						restore_default_params();
					}
					if (countPressed == 1){
						printf("[%s] send all to clients\r\n", getTimeString());
						ignore_settings = true;
						switchSleeping(false);
						start_indicate(transfer, ignore_settings);
						res_commonImp.trigger();
						res_battery.trigger();
						res_sendCommonImpPeriod.trigger();
						res_setOnlinePeriod.trigger();
						res_silentMode.trigger();
						res_batteryPeriod.trigger();
						res_batteryLowBorder.trigger();
						
						res_hour.trigger();
						res_day.trigger();
						res_month.trigger();
						switchSleeping(true);
						ignore_settings = false;
					}
				 	countPressed = 0;
				 	printf("[%s] timer pressed expired\r\n", getTimeString());
				}
			}
			if (ev == sensors_event){
				if (data == &reed_relay_sensor){
					GetImpulse(0, false);
					//if (!isSleeping)
					  //res_cntImp.trigger();
				}
				/*if (data == &reed_relay_sensor_red){
					GetImpulse(1, true);
					printf("red. ");
				}
				if (data == &reed_relay_sensor_green){
					GetImpulse(2, true);
					printf("green. ");
				}*/
				
				if (data == &reed_relay_sensor_opening){
					int opening = GetImpulse(3, true);
					if (opening){
						res_danger_opening.trigger();
					}
					printf("[%s] roof. \r\n", getTimeString());
				}

				if (data == &button_right_sensor){
					if (!first_start){
						countPressed++;
						printf("[%s] countPressed = %d\r\n", getTimeString(),countPressed);
						etimer_set(&button_timer,CLOCK_SECOND);
					} else printf("[%s] System started by button!", getTimeString());
				}
			}
			if (first_start){
				first_start = false;
			}
		} else {
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

		    printf("system_started=%d\r\n, setOnlinePeriod=%i\r\n, sendCommonImpPeriod=%i\r\n, batteryPeriod=%i\r\n, batteryLowBorder=%i\r\n",system_started, AllSettings.setOnlinePeriod, AllSettings.sendCommonImpPeriod, AllSettings.batteryPeriod, AllSettings.batteryLowBorder);
		}
		if (ev == PROCESS_EVENT_TIMER && data == &blink_timer_green){
			currentBlinks++;
			printf("[%s] green currentBlinks=%d, countBlinksGreen=%d\r\n", getTimeString(), currentBlinks,countBlinksGreen);
			if (currentBlinks<countBlinksGreen*2){
				//leds_toggle(LEDS_GREEN);
				//led_toggle();
			   	etimer_reset(&blink_timer_green);
				printf("[%s] green currentBlinks=%d\r\n", getTimeString(), currentBlinks);
			}
			else {
				isIndicateGreen = false; 
				//leds_off(LEDS_GREEN);
				led_clear();
				printf("[%s] Blinked green stoped\r\n", getTimeString());
				switchSleeping(true);
			}
		}
		if (ev == PROCESS_EVENT_TIMER && data == &blink_timer_red){
			currentBlinks++;
			if (currentBlinks<countBlinksRed*2 && isIndicateRed == true && (uip_ds6_get_global(ADDR_PREFERRED) == NULL || sicslowpan_get_last_rssi()==0)){
				leds_toggle(LEDS_RED);
				led_set();
				//led_toggle();
			   	etimer_reset(&blink_timer_red);
				//printf("red currentBlinks=%d\r\n; sicslowpan_get_last_rssi() = %d; addr = %s", currentBlinks, sicslowpan_get_last_rssi(), uip_ds6_get_global(ADDR_PREFERRED));
			}
			else {
				isIndicateRed = false; 
				leds_off(LEDS_RED);
				led_clear();
				//led_clear();
				printf("[%s] Blinked red stoped\r\n", getTimeString());
			}
		}
	}
	PROCESS_END();
}
