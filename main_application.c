/* Standard includes. */
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <string.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"

/* Hardware simulator utility functions */
#include "HW_access.h"

/*SERIAL COMUNICATION CHANNELS*/
#define COM0 (0)
#define COM1 (1)

/* DEFINISAN MIN I MAX KAO I PRAZAN I PUN REZERVOAR U LITRIMA */
#define PREDEFINED_MIN 0
#define PREDEFINED_MAX 10000
#define MINFUEL 0
#define MAXFUEL 40

/*TASK PRIORITIES*/
#define TASK_PRIO_1 ( tskIDLE_PRIORITY + 4 )
#define TASK_PRIO_2 ( tskIDLE_PRIORITY + 3 )
#define TASK_PRIO_3 ( tskIDLE_PRIORITY + 2 )
#define TASK_PRIO_4 ( tskIDLE_PRIORITY + 1 )

/* 7-SEG NUMBER DATABASE - ALL HEX DIGITS */
static const char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };

/*Za promenljive koje se frekventno menjaju kao i one koje se iz jednog taska pisu a iz drugog citaju (u ovom slucaju nije potrebno koristiti mutex- koristi se samo u slucaju da se promenljiva
menja iz vise od jednog taska) stavljali smo volatile da ih procesor ne bi optimizovao i time dobijali zastarelu vrednost. Sve promenljive su static jer se koriste samo unutar ovog fajla.*/
//Minimalna vrednost podesena preko serijske komunikacije, po defaultu je 0 (pre nego sto se bilo sta validno prosledi)
static volatile uint8_t  u_setMIN = PREDEFINED_MIN;
//Maximalna vrednost podesena preko serijske komunikacije, po defaultu je 10000 (pre nego sto se bilo sta validno prosledi)
static volatile uint8_t  u_setMAX = PREDEFINED_MAX;
//Trenutna minimalna vrednost koja se nalazi u nizu 
static volatile uint16_t u_currentMIN = 0;
//Trenutna maximalna vrednost koja se nalazi u nizu
static volatile uint16_t u_currentMAX = 0;
//Trenutna vrednost prosecne potrosnje
static volatile uint8_t u_PP = 0;
//Niz od 5 elemenata koji sadrzi poslednjih 5 vrednosti otpornosti, a ponasa se kao cirkularni buffer
static volatile uint8_t  a_Values[5] = { 0 };
static volatile uint8_t u_SetStartOrStop[2] = { 0 };
//Pozicija u nizu na koju se upisuje sledeci element (ocitana vrednost otpornosti)
static uint8_t u_WriteIndex = 0;
/*Pozicija u nizu sa koje se cita sledeci element(npr kada ih citamo i trazimo srednju vrednost, da znamo gde smo stali sa citanjem ako nas neko prekine
(neki task viseg prioriteta koji je spreman npr)*/
static uint8_t u_ReadIndex = 0;

/*SEMAPHORES*/
SemaphoreHandle_t s_DataSendingToPC_Semaphore;
SemaphoreHandle_t LED_INT_BinarySemaphore;
SemaphoreHandle_t s_7SEG;

SemaphoreHandle_t TBE_BS_0, TBE_BS_1;
SemaphoreHandle_t RXC_BS_0, RXC_BS_1;

/*TIMER HANDLERS*/
TimerHandle_t t_7SEG_Writing;
TimerHandle_t t_ResReading_Timer;
TimerHandle_t t_DataSending_Timer;

/*MY QUEUES*/
static QueueHandle_t q_PercentValue = NULL;
static QueueHandle_t q_AverageValue = NULL;
static QueueHandle_t q_LEDStates = NULL;
static QueueHandle_t q_Autonomy = NULL;
static QueueHandle_t q_DIFFERENCE = NULL;

/* TBE - TRANSMISSION BUFFER EMPTY - INTERRUPT HANDLER */
static uint32_t prvProcessTBEInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	if (get_TBE_status(0) != 0)
		xSemaphoreGiveFromISR(TBE_BS_0, &xHigherPTW);

	if (get_TBE_status(1) != 0)
		xSemaphoreGiveFromISR(TBE_BS_1, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
}

/* RXC - RECEPTION COMPLETE - INTERRUPT HANDLER */
static uint32_t prvProcessRXCInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	if (get_RXC_status(0) != 0)
		xSemaphoreGiveFromISR(RXC_BS_0, &xHigherPTW);

	if (get_RXC_status(1) != 0)
		xSemaphoreGiveFromISR(RXC_BS_1, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
}

/* OPC - ON INPUT PIN CHANGE - INTERRUPT HANDLER */
static uint32_t OnLED_ChangeInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(LED_INT_BinarySemaphore, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
}

/*TIMER CALLBACK FUNCTIONS*/
//Svakih 100ms se daje semafor za ispis na displej
static void v_7SEGWritingCallback(TimerHandle_t t_7SEG_Writing);
static void v_7SEGWritingCallback(TimerHandle_t t_7SEG_Writing)
{
	xSemaphoreGive(s_7SEG);
}

//Svakih 1000ms se daje semafor za ispis podataka na PC (serijsku)
static void v_DataSendingToPCCallback(TimerHandle_t t_DataSending_Timer);
static void v_DataSendingToPCCallback(TimerHandle_t t_DataSending_Timer)
{
	xSemaphoreGive(s_DataSendingToPC_Semaphore);
}

/*Function for converting resistance values to  percents*/
static void v_FuelLevelPercent(uint16_t* p_ResValue, uint8_t* p_PercentValue);

/*Function for calculating average value of resistance array*/
static void v_Average(uint8_t* u_Average);

// Funkcija kao parametre prima pointer na raw vrednost otpornosti koji treba da se proracuna u procentima i promenljivu na ciju adresu treba da sacuva proracunatu vrednost
static void v_FuelLevelPercent(uint16_t* p_ResValue, uint8_t* p_PercentValue)
{
	//Proverimo da li su poslati pointeri razliciti od NULL pointera
	if ((p_ResValue != (void*)0) && (p_PercentValue != (void*)0))
	{
		unsigned int u_Res = *p_ResValue;
		*p_PercentValue = (uint8_t)(100 * (u_Res - u_setMIN) / (u_setMAX - u_setMIN));
		printf("In percent %d %% \n", p_PercentValue);
		if (p_PercentValue < (uint16_t)10)
		{
			if (set_LED_BAR(0, 0x01) != 0)
			{
				printf("Problem");
			}
			else
			{
				if (set_LED_BAR(0, 0x00) != 0)
				{
					printf("No problem");
				}
			}
		}
	}
}

// Funkcija za racunanje srednje vrednosti niza, ide kroz niz sabira elemente i deli ih sa ukupnim brojem elemenata tj 5
static void v_Average(uint8_t* u_Average)
{
	double d_Sum = 0;
	for (int i = 0; i < 5; i++)
	{
		d_Sum += a_Values[i];
	}
	*u_Average = (uint8_t)((d_Sum) / 5);
}

/*TASKS FUNCTIONS DECLARATIONS*/
//Funkcija taska za merenje proseka nivoa goriva
static void v_MeasuringAverageFuelLevel(void* pvParameters);
//Funkcija taska za racunanje minimalne maximalne i srednje vrednosti poslednjih 5 ocitavanja napona 
void v_FuelLevelInPercent(void* p_Parameter);
//Funkcija taska za iscitavanje stanja ulaznih tastera (LED bar stubca)
void v_LEDStatesProcessing(void* p_Parameters);
void v_LEDReadingStates(void* p_Parameters);
//Funkcija taska za ispis podataka na displej
void v_7SEGWriting(void* p_Parameters);
//Funkcija taska za ispis podataka na PC 
void v_SendingToPC(void* p_Parameters);
//Funkcija taska za prijem podataka sa PC
void v_ReceivingCommands(void* p_Parameters);
//Funkcija za inicijalizaciju displeja, LED bara i serisjke, poziva se samo jednom na pocetku main funkcije
static void v_Init(void)
{
	init_7seg_comm();
	init_LED_comm();
	init_serial_uplink(COM0);
	init_serial_downlink(COM0);
	init_serial_uplink(COM1);
	init_serial_downlink(COM1);
}

/* MAIN */
void main_demo(void)
{
	//Inicijalizacija
	v_Init();
	//Creating timers
	t_7SEG_Writing = xTimerCreate("Timer", pdMS_TO_TICKS(100), pdTRUE, NULL, v_7SEGWritingCallback);
	//Startovanje tajmera za ispis na displej
	xTimerStart(t_7SEG_Writing, 0);

	t_DataSending_Timer = xTimerCreate("Timer2", pdMS_TO_TICKS(1000), pdTRUE, NULL, v_DataSendingToPCCallback);
	//Startovanje tajmera za ispis na PC
	xTimerStart(t_DataSending_Timer, 0);

	/* Create TBE semaphore - serial transmit comm */
	TBE_BS_0 = xSemaphoreCreateBinary();
	TBE_BS_1 = xSemaphoreCreateBinary();

	/* Create RXC semaphore - serial transmit comm */
	RXC_BS_0 = xSemaphoreCreateBinary();
	RXC_BS_1 = xSemaphoreCreateBinary();

	/*CREATE MY SEMAPHORES*/
	s_DataSendingToPC_Semaphore = xSemaphoreCreateBinary();
	LED_INT_BinarySemaphore = xSemaphoreCreateBinary();
	s_7SEG = xSemaphoreCreateBinary();

	/*CREATE MY QUEUES */
	q_PercentValue = xQueueCreate(1, sizeof(uint8_t));
	q_AverageValue = xQueueCreate(1, sizeof(uint8_t));
	q_LEDStates = xQueueCreate(1, sizeof(uint8_t));
	q_Autonomy = xQueueCreate(1, sizeof(uint8_t));
	q_DIFFERENCE = xQueueCreate(1, sizeof(uint8_t));

	/* SERIAL TRANSMISSION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_TBE, prvProcessTBEInterrupt);
	/* SERIAL RECEPTION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);
	/*ON INPUT CHANGE INTERRUPT HANDLER*/
	vPortSetInterruptHandler(portINTERRUPT_SRL_OIC, OnLED_ChangeInterrupt);

	/*CREATING TASKS*/
	//Task sa prioritetom TASK_PRIO_1 ima najvisi prioritet i tako redom (kada se ubaci makro dobije se najvisi broj prioriteta)
	xTaskCreate(v_MeasuringAverageFuelLevel, "AVE", configMINIMAL_STACK_SIZE, NULL, TASK_PRIO_2, NULL);
	xTaskCreate(v_LEDReadingStates, "LRS", configMINIMAL_STACK_SIZE, NULL, TASK_PRIO_2, NULL);
	xTaskCreate(v_LEDStatesProcessing, "LED", configMINIMAL_STACK_SIZE, NULL, TASK_PRIO_3, NULL);
	xTaskCreate(v_FuelLevelInPercent, "PER", configMINIMAL_STACK_SIZE, NULL, TASK_PRIO_3, NULL);
	xTaskCreate(v_7SEGWriting, "7SEG", configMINIMAL_STACK_SIZE, NULL, TASK_PRIO_2, NULL);
	xTaskCreate(v_SendingToPC, "CP", configMINIMAL_STACK_SIZE, NULL, TASK_PRIO_2, NULL);
	xTaskCreate(v_ReceivingCommands, "RC", configMINIMAL_STACK_SIZE, NULL, TASK_PRIO_1, NULL);

	vTaskStartScheduler();
	while (1);
}

/* Task koji vrsi prijem podataka sa kanala 0 - stizu vrednosti otpornosti sa senzora.
   Funkcija taska za citanje vrednosti otpornosti (serijeske COM0) i upisivanje vrednosti na odgovarajuce mesto u nizu. Niz je implementiran
   kao cirkularni bafer od 5 elemenata jer je potrebno racunati srednju vrednost poslednjih 5 ocitavanja i ovo se cinilo kao najbolje resenje.
   Kada nam write index dodje do poslednjeg elementa, vraca se na prvu poziciju i overwrite-uje prvi element i nastavlja iz pocetka da pise */
static void v_FuelLevelInPercent(void* pvParameters)
{
	uint16_t u_Resistance = 0;
	uint16_t u_FuelLevelPercent = 0;
	uint8_t u_Character = 0;
	uint8_t u_NumOfCharacter = 0;
	uint8_t a_ReceivedString[6] = { 0 };
	while (1)
	{
		/*Uzimamo semafor za ocitavanje otpornosti, tj detektujemo prijem sa kanala COM0 serijske*/
		xSemaphoreTake(RXC_BS_0, portMAX_DELAY);
		get_serial_character(COM0, &u_Character);
		//printf("%d", u_Character);
		/*Ocitavamo karakter po karakter sta je stiglo preko serijske komunikacije*/
		/*Ako je poslat karakter za kraj stringa, konvertujemo vrednosti*/
		if (u_Character == 0x0d)
		{
			/*Vrednost otpornosti je konverzija pristiglog stringa u int*/
			u_Resistance = atoi(a_ReceivedString);
			u_NumOfCharacter = 0;
			//printf("%d", u_Resistance);
			/*Ako konvertovani string ima vrednost izmedju 0 i 10000, pozivamo funkciju proracun u procentima i upisujemo novu vrednost na sledecu poziciju u nizu*/
			if ((u_Resistance) >= 0 && (u_Resistance < 10000)) {
				v_FuelLevelPercent(&u_Resistance, &u_FuelLevelPercent);
				a_Values[u_WriteIndex] = u_FuelLevelPercent;
				u_WriteIndex++;
			}
			/*Cirkularni bafer- ako smo stigli do poslednjeg elementa vracamo se na pocetak i pisemo ponovo*/
			if (u_WriteIndex == 5)
			{
				u_WriteIndex = 0;
			}
		}
		/*Ako jos nije kraj stringa, ucitavamo dalje karaktere*/
		else
		{
			a_ReceivedString[u_NumOfCharacter] = u_Character;
			u_NumOfCharacter++;
		}
		xQueueSend(q_PercentValue, &u_FuelLevelPercent, 0U);
	}
}

//Funkcija taska za citanje stanja LED
void v_LEDReadingStates(void* p_Parameters)
{
	uint8_t u_LEDBarState = 0x00;
	while (1)
	{
		xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
		get_LED_BAR(0, &u_LEDBarState);
		xQueueSend(q_LEDStates, &u_LEDBarState, 0U);

	}
}

//Funkcija taska za obradu stanja LED
void v_LEDStatesProcessing(void* p_Parameters)
{
	uint8_t u_LEDBar = 0x00;
	uint8_t u_PercentValueReceived = 0;
	uint8_t STARTpercent = 0;
	uint8_t STOPpercent = 0;
	uint8_t u_DIFFERENCE = 0;
	/*Ako je setovano na 1 posaljemo tasku za prikaz da se prikazuje autonomija vozila i potrosena kolicina goriva u procentima,
	ako ga setujemo na 2 posaljemo da se prikaze trenutni nivo goriva i vrednost ocitane otpornosti*/
	uint8_t d = 0;
	while (1)
	{
		xQueueReceive(q_LEDStates, &u_LEDBar, pdMS_TO_TICKS(20));
		xQueueReceive(q_PercentValue, &u_PercentValueReceived, 0U);
		/*Ako je pritisnut drugi taster nultog stubca zelimo da se prikaze trenutni nivo goriva i vrednost ocitane otpornosti,
		ako je pritisnut treci taster zelimo da se prikaze autonomija vozila i potrosena kolicina goriva u procentima*/
		if (u_LEDBar & 0x02)
		{
			d = 2;
		}
		else if (u_LEDBar & 0x04)
		{
			d = 1;
		}
		else
		{
			d = 0;
		}

		//START i STOP su realizovani preko tastera
		if ((u_SetStartOrStop[0]))
		{
			STARTpercent = u_PercentValueReceived;
			set_LED_BAR(2, 0x01);
		}
		else if ((u_SetStartOrStop[1]))
		{
			STOPpercent = u_PercentValueReceived;
			u_DIFFERENCE = STARTpercent - STOPpercent;
			set_LED_BAR(2, 0x02);
		}
		else {
			set_LED_BAR(2, 0x00);
			printf("Neither start nor stop are pressed");
		}
		xQueueSend(q_DIFFERENCE, &u_DIFFERENCE, 0U);
	}
}

//Funkcija taska za racunanje minimalne, maximalne i srednje vrednosti poslednjih 5 ocitavanja napona, kao i prosecnu potrosnju
void v_MeasuringAverageFuelLevel(void* p_Parameters)
{
	u_currentMIN = a_Values[0];
	u_currentMAX = a_Values[0];
	uint8_t u_AverageValue = 0;
	//uint8_t u_PP = 0;
	uint8_t u_Autonomy = 0;
	uint8_t u_PercentValueReceived = 0;
	uint8_t PercentValue = 0;
	uint8_t u_Character = 0;
	uint8_t u_NumOfCharacter = 0;
	uint8_t a_ReceivedString[6] = { 0 };
	while (1)
	{
		v_Average(&u_AverageValue);
		for (int i = 0; i < 5; i++)
		{
			if (a_Values[i] < u_currentMIN) {
				u_currentMIN = a_Values[i];
			}
			else if (a_Values[i] > u_currentMAX) {
				u_currentMAX = a_Values[i];
			}
		}
		xQueueSend(q_AverageValue, &u_AverageValue, 0U);

		xQueueReceive(q_PercentValue, &u_PercentValueReceived, 0U);
		PercentValue = u_PercentValueReceived;
		//Funkcija za racunanje prosecne potrosnje-autonomija
		if (u_Character == 0x0d)
		{
			/*Vrednost otpornosti je konverzija pristiglog stringa u int*/
			u_PP = atoi(a_ReceivedString);
			u_NumOfCharacter = 0;
			/*Ako konvertovani string ima vrednost izmedju 0 i 40, racunamo autonomiju vozila*/
			if ((u_PP) >= 0 && (u_PP < 40)) {
				u_Autonomy = (uint8_t)(PercentValue * MAXFUEL / u_PP);
				printf("Can still go %d km\n", u_Autonomy);
			}
		}
		/*Ako jos nije kraj stringa, ucitavamo dalje karaktere*/
		else
		{
			a_ReceivedString[u_NumOfCharacter] = u_Character;
			u_NumOfCharacter++;
		}
		xQueueSend(q_Autonomy, &u_Autonomy, 0U);
	}
}

//Proveri redosled cifara na 7SEG
void v_7SEGWriting(void* p_Parameters)
{
	uint8_t d = 0;
	double DIFF = 0;
	double AUT = 0;
	double Per = 0;
	double Ave = 0;
	uint8_t u_CalculatedDIFFERENCEValue = 0;
	uint8_t u_CalculatedAutonomyValue = 0;
	uint8_t u_CalculatedPercentValue = 0;
	uint8_t u_CalculatedAverageValue = 0;
	double a_ReceivedInformations[4] = { 0.0 };
	while (1)
	{
		//Uzimamo semafor koji se daje pomocu callback funkcije tajmera na svakih 100ms jer na toliko treba da se osvezava ispis
		xSemaphoreTake(s_7SEG, portMAX_DELAY);
		xQueueReceive(q_PercentValue, &u_CalculatedPercentValue, pdMS_TO_TICKS(20));
		xQueueReceive(q_AverageValue, &u_CalculatedAverageValue, pdMS_TO_TICKS(20));
		xQueueReceive(q_Autonomy, &u_CalculatedAutonomyValue, pdMS_TO_TICKS(20));
		xQueueReceive(q_DIFFERENCE, &u_CalculatedDIFFERENCEValue, pdMS_TO_TICKS(20));
		DIFF = u_CalculatedDIFFERENCEValue;
		AUT = u_CalculatedAutonomyValue;
		Per = u_CalculatedPercentValue;
		Ave = u_CalculatedAverageValue;
		//ako je prosledjena 2, ispisuje average i percent value
		if (d == 2) {
			select_7seg_digit(0);
			set_7seg_digit(hexnum[(uint8_t)Per / 10]);
			select_7seg_digit(1);
			set_7seg_digit(hexnum[(uint8_t)Per % 10]);
			select_7seg_digit(2);
			set_7seg_digit(hexnum[(uint8_t)Per % 10]);

			select_7seg_digit(4);
			set_7seg_digit(hexnum[(uint8_t)Ave / 10]);
			select_7seg_digit(5);
			set_7seg_digit(hexnum[(uint8_t)Ave / 10]);
			select_7seg_digit(6);
			set_7seg_digit(hexnum[(uint8_t)Ave / 10]);
			select_7seg_digit(7);
			set_7seg_digit(hexnum[(uint8_t)Ave / 10]);
			select_7seg_digit(8);
			set_7seg_digit(hexnum[(uint8_t)Ave / 10]);
		}
		//ako je prosledjena 1, ispisuje autonomy i difference
		else if (d == 1) {
			select_7seg_digit(0);
			set_7seg_digit(hexnum[(uint8_t)AUT / 10]);
			select_7seg_digit(1);
			set_7seg_digit(hexnum[(uint8_t)AUT % 10]);
			select_7seg_digit(2);
			set_7seg_digit(hexnum[(uint8_t)AUT % 10]);

			select_7seg_digit(4);
			set_7seg_digit(hexnum[(uint8_t)DIFF % 10]);
			select_7seg_digit(5);
			set_7seg_digit(hexnum[(uint8_t)DIFF % 10]);
		}
		else
		{
			select_7seg_digit(0);
			set_7seg_digit(0);
			select_7seg_digit(1);
			set_7seg_digit(0);
			select_7seg_digit(2);
			set_7seg_digit(0);
			select_7seg_digit(4);
			set_7seg_digit(0);
			select_7seg_digit(5);
			set_7seg_digit(0);
			select_7seg_digit(6);
			set_7seg_digit(0);
			select_7seg_digit(7);
			set_7seg_digit(0);
			select_7seg_digit(8);
			set_7seg_digit(0);
		}
	}
}

//Funkcija taska za prijem i obradu komandi sa serijskog porta COM1
void v_ReceivingCommands(void* p_Parameters)
{
	//Niz za skladistenje prijemne komande, najvise 13 karaktera(kontrolisano+0)
	uint8_t a_CommandLetters[13];
	//Karakter po karakter sa serijske skladistimo u ovu promenljivu
	uint8_t u_Character = 0;
	/*Duzina primljene komande, inkrementuje se prilikom prijema novog karaktera sa serijske, sluzi za odredjivanje elementa u nizu za smestanje komande gde se nalazimo,
	kao */
	uint8_t u_WordSize = 0;
	uint8_t u_GetMinOrMaxValue = 0;
	uint8_t u_GetPPValue = 0;
	/*niz od dva elementa za smestanje poslednje cetiri vrednosti komande u slucaju setovanja Min ili Max (opseg je od 0 do 10000) vrednosti da bismo ih pretvorili u int vrednost,
	npr MINFUEL12, uzmemo poslednja dva karaktera 12, smestimo ih u ovaj niz i pomocu funkcije atoi ih pretvorimo u int vrednost kako bi koristili tu vrednost kao minimum.
	Za setovanje minimuma je potrebno poslati npr MINFUEL0012 - trebalo bi da se uvek iskoriste sva cetiri mesta za cifre, pa ako nije cetvorocifren broj upisati nule*/
	uint8_t a_ValueMinOrMax[2] = { 0 };
	uint8_t a_ValuePP[1] = { 0 };
	while (1)
	{
		//Dobijamo semafor kada pristigne karakter sa serijske
		xSemaphoreTake(RXC_BS_1, portMAX_DELAY);
		get_serial_character(COM1, &u_Character);
		//Ako smo dosli do kraja komande, obradjujemo je i setujemo promenljive
		if (u_Character == 0x0d)
		{
			if ((u_WordSize == sizeof("MINFUEL--") - 1) && (strncmp(a_CommandLetters, ("MINFUEL"), (u_WordSize - 2)) == 0))
			{
				a_ValueMinOrMax[0] = a_CommandLetters[7];
				a_ValueMinOrMax[1] = a_CommandLetters[8];
				a_ValueMinOrMax[2] = a_CommandLetters[9];
				a_ValueMinOrMax[3] = a_CommandLetters[10];
				/*Namerno castujemo povratnu vrednost funkcije u uint8_t, iz razloga da se setuje maximalna vrednost opsega ako je prosledjena
				vrednost veca od maximalne (manje od 0 ne moze zbog tipa elemenata niza koji su unsigned)
				Ovo se moglo resiti i dodatnom lokalnom promenljivom koja bi npr bila uin16_t tipa i nju bismo proveravali da li upada u opseg
				ako je izvan njega mogli bismo da ignorisemo prosledjivanje max/min vrednosti ili da prosledimo granicu opsega- meni je resenje sa castovanjem
				bilo elegantnije, ne trosimo runtime na poredjenje i dodatnu petlju(iako je malo) i stedimo dodatnu promenljivu na steku*/
				u_GetMinOrMaxValue = (uint8_t)atoi(a_ValueMinOrMax);
				if (u_GetMinOrMaxValue <= PREDEFINED_MAX)
				{
					u_setMIN = u_GetMinOrMaxValue;
					//printf("MINIMUM PROMENJEN: %d\n", u_setMIN);
				}
				else {
					//printf("MINIMUM VAN OPSEGA: %d\n", u_GetMinOrMaxValue);
				}

			}
			else if ((u_WordSize == sizeof("MAXFUEL--") - 1) && (strncmp(a_CommandLetters, ("MAXFUEL"), (u_WordSize - 2)) == 0))
			{
				a_ValueMinOrMax[0] = a_CommandLetters[7];
				a_ValueMinOrMax[1] = a_CommandLetters[8];
				a_ValueMinOrMax[2] = a_CommandLetters[9];
				a_ValueMinOrMax[3] = a_CommandLetters[10];
				u_GetMinOrMaxValue = (uint8_t)atoi(a_ValueMinOrMax);
				if (u_GetMinOrMaxValue <= PREDEFINED_MAX)
				{
					u_setMAX = u_GetMinOrMaxValue;
					//printf("MAXIMUM PROMENJEN: %d\n", u_setMAX);
				}
				else {
					//printf("MAXIMUM VAN OPSEGA: %d\n", u_GetMinOrMaxValue);
				}
			}
			else if ((u_WordSize == sizeof("PP--") - 1) && (strncmp(a_CommandLetters, ("PP"), (u_WordSize - 2)) == 0))
			{
				a_ValuePP[0] = a_CommandLetters[2];
				a_ValuePP[1] = a_CommandLetters[3];
				u_GetPPValue = (uint8_t)atoi(a_ValuePP);
				if (u_GetPPValue <= MAXFUEL)
				{
					u_PP = u_GetPPValue;
					//printf("PP PROMENJEN: %d\n", u_setPP);
				}
				else {
					//printf("PP VAN OPSEGA: %d\n", u_GetPPValue);
				}
			}
			u_WordSize = 0;
		}
		else
		{
			a_CommandLetters[u_WordSize] = u_Character;
			u_WordSize++;
		}
	}
}

//Funkcija taska za ispis na serijskoj komunikaciji, slanje trenutne kolicine goriva prema PC-ju u procentima
void v_SendingToPC(void* p_Parameters)
{
	uint8_t u_CommandSize = 0;
	uint8_t a_Command[13] = { 0 };
	uint8_t u_PercentValueForWriting = 0;
	while (1)
	{
		//Podatke obradjujemo i saljemo na PC (tj serijski COM1) kontinualno svakih 1000ms
		xQueueReceive(q_PercentValue, &u_PercentValueForWriting, pdMS_TO_TICKS(20));
		xSemaphoreTake(s_DataSendingToPC_Semaphore, portMAX_DELAY);
		//Ispisujemo vrednost koja je u procentima, da bismo je ispisali potrebno je da je posaljemo u obliku ascii koda serijskoj komunikaciji,
		//Na izracunatu cifru dodamo karakter '0' (offset) kako bismo dobili zeljeni karakter za slanje na serijsku
		//Cifru jedinica dobijamo kao ostatak pri deljenju sa 10 i castujemo u celobrojan (uint8_t) tip
		a_Command[u_CommandSize] = (uint8_t)u_PercentValueForWriting / 10 + '0';
		send_serial_character(COM1, a_Command[u_CommandSize]);
		u_CommandSize++;
		xSemaphoreTake(TBE_BS_1, portMAX_DELAY);
		//Cifru jedinica dobijamo kao ostatak pri deljenju sa 10 
		a_Command[u_CommandSize] = (uint8_t)u_PercentValueForWriting % 10 + '0';
		send_serial_character(COM1, a_Command[u_CommandSize]);
		xSemaphoreTake(TBE_BS_1, portMAX_DELAY);
		send_serial_character(COM1, 13);
		xSemaphoreTake(TBE_BS_1, portMAX_DELAY);
	}
}