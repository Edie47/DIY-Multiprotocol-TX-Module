/*********************************************************
					Multiprotocol Tx code
               by Midelic and Pascal Langer(hpnuts)
	http://www.rcgroups.com/forums/showthread.php?t=2165676
    https://github.com/pascallanger/DIY-Multiprotocol-TX-Module/edit/master/README.md

	Thanks to PhracturedBlue, Hexfet, Goebish, Victzh and all protocol developers
				Ported  from deviation firmware 

 This project is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Multiprotocol is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Multiprotocol.  If not, see <http://www.gnu.org/licenses/>.
*/
#define _DISABLE_ARDUINO_TIMER0_INTERRUPT_HANDLER_
#include <wiring.c>

#include <avr/pgmspace.h>

//#define DEBUG_PIN		// Use pin TX for AVR and SPI_CS for STM32 => DEBUG_PIN_on, DEBUG_PIN_off, DEBUG_PIN_toggle
//#define DEBUG_SERIAL	// Only for STM32_BOARD, compiled with Upload method "Serial"->usart1, "STM32duino bootloader"->USB serial

#ifdef __arm__			// Let's automatically select the board if arm is selected
	#define STM32_BOARD
#endif
#if defined (ARDUINO_AVR_XMEGA32D4) || defined (ARDUINO_MULTI_ORANGERX)
	#include "MultiOrange.h"
#endif

#include "Multiprotocol.h"

//Multiprotocol module configuration file
#include "_Config.h"

//Personal config file
#if defined(USE_MY_CONFIG)
#include "_MyConfig.h"
#endif

#include "Pins.h"
#include "TX_Def.h"
#include "Validate.h"

#ifndef STM32_BOARD
	#include <avr/eeprom.h>
#else
	#include <libmaple/usart.h>
	#include <libmaple/timer.h>
	//#include <libmaple/spi.h>
	#include <SPI.h>
	#include <EEPROM.h>	
	HardwareTimer HWTimer2(2);
	#ifdef ENABLE_SERIAL
		HardwareTimer HWTimer3(3);
		void ISR_COMPB();
	#endif

	void PPM_decode();
	extern "C"
	{
		void __irq_usart2(void);
		void __irq_usart3(void);
	}
	#ifdef SEND_CPPM
		HardwareTimer HWTimer1(1) ;
	#endif				 
#endif

//Global constants/variables
uint32_t MProtocol_id;//tx id,
uint32_t MProtocol_id_master;
uint32_t blink=0,last_signal=0;
//
uint16_t counter;
uint8_t  channel;
#if defined(ESKY150V2_CC2500_INO)
	uint8_t  packet[150];
#else
	uint8_t  packet[50];
#endif

#define NUM_CHN 16
// Servo data
uint16_t Channel_data[NUM_CHN];
uint8_t  Channel_AUX;
#ifdef FAILSAFE_ENABLE
	uint16_t Failsafe_data[NUM_CHN];
#endif

// Protocol variables
uint8_t  cyrfmfg_id[6];//for dsm2 and devo
uint8_t  rx_tx_addr[5];
uint8_t  rx_id[5];
uint8_t  phase;
uint16_t bind_counter;
uint8_t  bind_phase;
uint8_t  binding_idx;
uint16_t packet_period;
uint8_t  packet_count;
uint8_t  packet_sent;
uint8_t  packet_length;
#if defined(HOTT_CC2500_INO) || defined(ESKY150V2_CC2500_INO) || defined(MLINK_CYRF6936_INO)
	uint8_t  hopping_frequency[78];
#else
	uint8_t  hopping_frequency[50];
#endif
uint8_t  *hopping_frequency_ptr;
uint8_t  hopping_frequency_no=0;
uint8_t  rf_ch_num;
uint8_t  throttle, rudder, elevator, aileron;
uint8_t  flags;
uint16_t crc;
uint16_t crc16_polynomial;
uint8_t  crc8;
uint8_t  crc8_polynomial;
uint16_t seed;
uint16_t failsafe_count;
uint16_t state;
uint8_t  len;
uint8_t  armed, arm_flags, arm_channel_previous;
uint8_t  num_ch;
uint32_t pps_timer;
uint16_t pps_counter;

#ifdef CC2500_INSTALLED
	#ifdef SCANNER_CC2500_INO
		uint8_t calData[255];
	#elif defined(HOTT_CC2500_INO) || defined(ESKY150V2_CC2500_INO)
		uint8_t calData[75];
	#else
		uint8_t calData[50];
	#endif
#endif

#ifdef CHECK_FOR_BOOTLOADER
	uint8_t BootTimer ;
	uint8_t BootState ;
	uint8_t NotBootChecking ;
	uint8_t BootCount ;

	#define BOOT_WAIT_30_IDLE	0
	#define BOOT_WAIT_30_DATA	1
	#define BOOT_WAIT_20		2
	#define BOOT_READY			3
#endif

//Channel mapping for protocols
uint8_t CH_AETR[]={AILERON, ELEVATOR, THROTTLE, RUDDER, CH5, CH6, CH7, CH8, CH9, CH10, CH11, CH12, CH13, CH14, CH15, CH16};
uint8_t CH_TAER[]={THROTTLE, AILERON, ELEVATOR, RUDDER, CH5, CH6, CH7, CH8, CH9, CH10, CH11, CH12, CH13, CH14, CH15, CH16};
//uint8_t CH_RETA[]={RUDDER, ELEVATOR, THROTTLE, AILERON, CH5, CH6, CH7, CH8, CH9, CH10, CH11, CH12, CH13, CH14, CH15, CH16};
uint8_t CH_EATR[]={ELEVATOR, AILERON, THROTTLE, RUDDER, CH5, CH6, CH7, CH8, CH9, CH10, CH11, CH12, CH13, CH14, CH15, CH16};

// Mode_select variables
uint8_t mode_select;
uint8_t protocol_flags=0,protocol_flags2=0,protocol_flags3=0;
uint8_t option_override;

#ifdef ENABLE_PPM
// PPM variable
volatile uint16_t PPM_data[NUM_CHN];
volatile uint8_t  PPM_chan_max=0;
uint32_t chan_order=0;
#endif

#if not defined (ORANGE_TX) && not defined (STM32_BOARD)
//Random variable
volatile uint32_t gWDT_entropy=0;
#endif

//Serial protocol
uint8_t sub_protocol;
uint8_t protocol;
uint8_t option;
uint8_t cur_protocol[3];
uint8_t prev_option;
uint8_t prev_power=0xFD; // unused power value
uint8_t  RX_num;

//Serial RX variables
#define BAUD 100000
#define RXBUFFER_SIZE 36	// 26+1+9
volatile uint8_t rx_buff[RXBUFFER_SIZE];
volatile uint8_t rx_ok_buff[RXBUFFER_SIZE];
volatile bool discard_frame = false;
volatile uint8_t rx_idx=0, rx_len=0;

// Callback
uint16_function_t remote_callback = 0;

#ifdef HUBSAN_HUB_TELEMETRY
  uint8_t telemetry_link=0; 
  uint8_t TX_RSSI;
  uint8_t v_lipo1;
  int16_t est_altitude;
  int16_t angle_pitch;
  int16_t angle_roll;
  int16_t angle_yaw;
  int16_t giro_pitch;
  int16_t giro_roll;
  int16_t giro_yaw;
#endif

uint8_t multi_protocols_index=0xFF;

// Init
void setup()
{
  #ifdef OLED_DISPLAY
    dispaly_init();
  #endif
  
	// Setup diagnostic uart before anything else
	#ifdef DEBUG_SERIAL
		Serial.begin(115200,SERIAL_8N1);

		// Wait up to 30s for a serial connection; double-blink the LED while we wait
//		unsigned long currMillis = millis();
//		unsigned long initMillis = currMillis;
//		pinMode(LED_pin,OUTPUT);
//		LED_off;
//		while (!Serial && (currMillis - initMillis) <= 30000) {
//			LED_on;
//			delay(100);
//			LED_off;
//			delay(100);
//			LED_on;
//			delay(100);
//			LED_off;
//			delay(500);
//			currMillis = millis();
//		}

//		delay(250);  // Brief delay for FTDI debugging
		debugln("Multiprotocol version: %d.%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION, VERSION_PATCH_LEVEL);
	#endif

	// General pinout
	#ifdef ORANGE_TX
		//XMEGA
		PORTD.OUTSET = 0x17 ;
		PORTD.DIRSET = 0xB2 ;
		PORTD.DIRCLR = 0x4D ;
		PORTD.PIN0CTRL = 0x18 ;
		PORTD.PIN2CTRL = 0x18 ;
		PORTE.DIRSET = 0x01 ;
		PORTE.DIRCLR = 0x02 ;
		// Timer1 config
		// TCC1 16-bit timer, clocked at 0.5uS
		EVSYS.CH3MUX = 0x80 + 0x04 ;				// Prescaler of 16
		TCC1.CTRLB = 0; TCC1.CTRLC = 0; TCC1.CTRLD = 0; TCC1.CTRLE = 0;
		TCC1.INTCTRLA = 0; TIMSK1 = 0;
		TCC1.PER = 0xFFFF ;
		TCNT1 = 0 ;
		TCC1.CTRLA = 0x0B ;							// Event3 (prescale of 16)
	#elif defined STM32_BOARD
		//STM32
		afio_cfg_debug_ports(AFIO_DEBUG_NONE);
		pinMode(LED_pin,OUTPUT);
		pinMode(LED2_pin,OUTPUT);
		pinMode(A7105_CSN_pin,OUTPUT);
		pinMode(CC25_CSN_pin,OUTPUT);
		pinMode(NRF_CSN_pin,OUTPUT);
		pinMode(CYRF_CSN_pin,OUTPUT);
		pinMode(SPI_CSN_pin,OUTPUT);
		pinMode(CYRF_RST_pin,OUTPUT);
		pinMode(PE1_pin,OUTPUT);
		pinMode(PE2_pin,OUTPUT);
		pinMode(TX_INV_pin,OUTPUT);
		pinMode(RX_INV_pin,OUTPUT);
		pinMode(BIND_pin,INPUT_PULLUP);
		pinMode(PPM_pin,INPUT);
		pinMode(S1_pin,INPUT_PULLUP);				// dial switch
		pinMode(S2_pin,INPUT_PULLUP);
		pinMode(S3_pin,INPUT_PULLUP);
		pinMode(S4_pin,INPUT_PULLUP);
		
		#ifdef MULTI_5IN1_INTERNAL
			//pinMode(SX1276_RST_pin,OUTPUT);		// already done by LED2_pin
			pinMode(SX1276_TXEN_pin,OUTPUT);		// PB0
			pinMode(SX1276_DIO0_pin,INPUT_PULLUP);
		#else
			//Random pin
			pinMode(RND_pin, INPUT_ANALOG);			// set up PB0 pin for analog input
		#endif
	
		#if defined ENABLE_DIRECT_INPUTS
			#if defined (DI1_PIN)
				pinMode(DI1_PIN,INPUT_PULLUP);
			#endif
			#if defined (DI2_PIN)
				pinMode(DI2_PIN,INPUT_PULLUP);
			#endif
			#if defined (DI3_PIN)
				pinMode(DI3_PIN,INPUT_PULLUP);
			#endif
			#if defined (DI4_PIN)
				pinMode(DI4_PIN,INPUT_PULLUP);
			#endif
		#endif

		#ifdef SEND_CPPM
			pinMode(PA9,INPUT);						// make sure the USART1.TX pin is released for heartbeat use
		#endif

		//Timers
		init_HWTimer();								//0.5us

		//Read module flash size
		#ifndef DISABLE_FLASH_SIZE_CHECK
			unsigned short *flashSize = (unsigned short *) (0x1FFFF7E0);// Address register 
			debugln("Module Flash size: %dKB",(int)(*flashSize & 0xffff));
			if((int)(*flashSize & 0xffff) < MCU_EXPECTED_FLASH_SIZE)  // Not supported by this project
				while (true) { //SOS
					for(uint8_t i=0; i<3;i++)
					{
						LED_on;
						delay(100);
						LED_off;
						delay(100);
					}
					for(uint8_t i=0; i<3;i++)
					{
						LED_on;
						delay(500);
						LED_off;
						delay(100);
					}
					for(uint8_t i=0; i<3;i++)
					{
						LED_on;
						delay(100);
						LED_off;
						delay(100);
					}
					LED_off;
					delay(1000);
				}
		#endif

		// Initialize the EEPROM
		uint16_t eepromStatus = EEPROM.init();
		debugln("EEPROM initialized: %d",eepromStatus);

		// If there was no valid EEPROM page the EEPROM is corrupt or uninitialized and should be formatted
		if( eepromStatus == EEPROM_NO_VALID_PAGE )
		{
			EEPROM.format();
			debugln("No valid EEPROM page, EEPROM formatted");
		}
	#else
		//ATMEGA328p
		// all inputs
		DDRB=0x00;DDRC=0x00;DDRD=0x00;
		// outputs
		SDI_output;
		SCLK_output;
		#ifdef A7105_CSN_pin
			A7105_CSN_output;
		#endif
		#ifdef CC25_CSN_pin
			CC25_CSN_output;
		#endif
		#ifdef CYRF_CSN_pin
			CYRF_RST_output;
			CYRF_CSN_output;
		#endif
		#ifdef NRF_CSN_pin
			NRF_CSN_output;
		#endif
		PE1_output;
		PE2_output;
		SERIAL_TX_output;

		// pullups
		PROTO_DIAL1_port |= _BV(PROTO_DIAL1_pin);
		PROTO_DIAL2_port |= _BV(PROTO_DIAL2_pin);
		PROTO_DIAL3_port |= _BV(PROTO_DIAL3_pin);
		PROTO_DIAL4_port |= _BV(PROTO_DIAL4_pin);
		BIND_port |= _BV(BIND_pin);

		// Timer1 config
		TCCR1A = 0;
		TCCR1B = (1 << CS11);	//prescaler8, set timer1 to increment every 0.5us(16Mhz) and start timer

		// Random
		random_init();
	#endif

	LED2_on;
	
	// Set Chip selects
	#ifdef A7105_CSN_pin
		A7105_CSN_on;
	#endif
	#ifdef CC25_CSN_pin
		CC25_CSN_on;
	#endif
	#ifdef CYRF_CSN_pin
		CYRF_CSN_on;
	#endif
	#ifdef NRF_CSN_pin
		NRF_CSN_on;
	#endif
	#ifdef SPI_CSN_pin
		SPI_CSN_on;
	#endif

	//	Set SPI lines
	#ifdef	STM32_BOARD
		initSPI2();
	#else
		SDI_on;
		SCLK_off;
	#endif

	//Wait for every component to start
	delayMilliseconds(100);
	
	// Read status of bind button
	if( IS_BIND_BUTTON_on )
	{
		BIND_BUTTON_FLAG_on;	// If bind button pressed save the status
		BIND_IN_PROGRESS;		// Request bind
	}
	else
		BIND_DONE;

	// Read status of mode select binary switch
	// after this mode_select will be one of {0000, 0001, ..., 1111}
	#ifndef ENABLE_PPM
		mode_select = MODE_SERIAL ;	// force serial mode
	#elif defined STM32_BOARD
		mode_select= 0x0F -(uint8_t)(((GPIOA->regs->IDR)>>4)&0x0F);
	#else
		mode_select = 1;
//		mode_select =
//			((PROTO_DIAL1_ipr & _BV(PROTO_DIAL1_pin)) ? 0 : 1) + 
//			((PROTO_DIAL2_ipr & _BV(PROTO_DIAL2_pin)) ? 0 : 2) +
//			((PROTO_DIAL3_ipr & _BV(PROTO_DIAL3_pin)) ? 0 : 4) +
//			((PROTO_DIAL4_ipr & _BV(PROTO_DIAL4_pin)) ? 0 : 8);
	#endif
	//mode_select=1;
    debugln("Protocol selection switch reads as %d", mode_select);

	#ifdef ENABLE_PPM
		uint8_t bank=bank_switch();
	#endif

	// Set default channels' value
	for(uint8_t i=0;i<NUM_CHN;i++)
		Channel_data[i]=1024;
	Channel_data[THROTTLE]=0;	//0=-125%, 204=-100%

	#ifdef ENABLE_PPM
		// Set default PPMs' value
		for(uint8_t i=0;i<NUM_CHN;i++)
			PPM_data[i]=PPM_MAX_100+PPM_MIN_100;
		PPM_data[THROTTLE]=PPM_MIN_100*2;
	#endif

	// Update LED
	LED_off;
	LED_output;

	//Init RF modules
	modules_reset();

#ifndef ORANGE_TX
	#ifdef STM32_BOARD
		uint32_t seed=0;
		for(uint8_t i=0;i<4;i++)
		#ifdef RND_pin
			seed=(seed<<8) | (analogRead(RND_pin)& 0xFF);
		#else
		//TODO find something to randomize...
			seed=(seed<<8);
		#endif
		randomSeed(seed);
	#else
		//Init the seed with a random value created from watchdog timer for all protocols requiring random values
		randomSeed(random_value());
	#endif
#endif

	// Read or create protocol id
	MProtocol_id_master=random_id(EEPROM_ID_OFFSET,false);

	debugln("Module Id: %lx", MProtocol_id_master);

#ifdef ENABLE_PPM
	//Protocol and interrupts initialization
	if(mode_select != MODE_SERIAL)
	{ // PPM
		#ifndef MY_PPM_PROT
			const PPM_Parameters *PPM_prot_line=&PPM_prot[bank*14+mode_select-1];
		#else
			const PPM_Parameters *PPM_prot_line=&My_PPM_prot[bank*14+mode_select-1];
		#endif
		
		protocol		=	PPM_prot_line->protocol;
		cur_protocol[1] =	protocol;
		sub_protocol   	=	PPM_prot_line->sub_proto;
		RX_num			=	PPM_prot_line->rx_num;
		chan_order		=	PPM_prot_line->chan_order;

		//Forced frequency tuning values for CC2500 protocols
		#if defined(FORCE_FRSKYD_TUNING) && defined(FRSKYD_CC2500_INO)
			if(protocol==PROTO_FRSKYD) 
				option			=	FORCE_FRSKYD_TUNING;		// Use config-defined tuning value for FrSkyD
			else
		#endif
		#if defined(FORCE_FRSKYL_TUNING) && defined(FRSKYL_CC2500_INO)
			if(protocol==PROTO_FRSKYL) 
				option			=	FORCE_FRSKYL_TUNING;		// Use config-defined tuning value for FrSkyL
			else
		#endif
		#if defined(FORCE_FRSKYV_TUNING) && defined(FRSKYV_CC2500_INO)
			if(protocol==PROTO_FRSKYV)
				option			=	FORCE_FRSKYV_TUNING;		// Use config-defined tuning value for FrSkyV
			else
		#endif
		#if defined(FORCE_FRSKYX_TUNING) && defined(FRSKYX_CC2500_INO)
			if(protocol==PROTO_FRSKYX || protocol==PROTO_FRSKYX2)
				option			=	FORCE_FRSKYX_TUNING;		// Use config-defined tuning value for FrSkyX
			else
		#endif 
		#if defined(FORCE_FUTABA_TUNING) && defined(FUTABA_CC2500_INO)
			if (protocol==PROTO_FUTABA)
				option			=	FORCE_FUTABA_TUNING;			// Use config-defined tuning value for SFHSS
			else
		#endif
		#if defined(FORCE_CORONA_TUNING) && defined(CORONA_CC2500_INO)
			if (protocol==PROTO_CORONA)
				option			=	FORCE_CORONA_TUNING;		// Use config-defined tuning value for CORONA
			else
		#endif
		#if defined(FORCE_SKYARTEC_TUNING) && defined(SKYARTEC_CC2500_INO)
			if (protocol==PROTO_SKYARTEC)
				option			=	FORCE_SKYARTEC_TUNING;		// Use config-defined tuning value for SKYARTEC
			else
		#endif
		#if defined(FORCE_REDPINE_TUNING) && defined(REDPINE_CC2500_INO)
			if (protocol==PROTO_REDPINE)
				option			=	FORCE_REDPINE_TUNING;		// Use config-defined tuning value for REDPINE
			else
		#endif
		#if defined(FORCE_RADIOLINK_TUNING) && defined(RADIOLINK_CC2500_INO)
			if (protocol==PROTO_RADIOLINK)
				option			=	FORCE_RADIOLINK_TUNING;		// Use config-defined tuning value for RADIOLINK
			else
		#endif
		#if defined(FORCE_HITEC_TUNING) && defined(HITEC_CC2500_INO)
			if (protocol==PROTO_HITEC)
				option			=	FORCE_HITEC_TUNING;		// Use config-defined tuning value for HITEC
			else
		#endif
		#if defined(FORCE_HOTT_TUNING) && defined(HOTT_CC2500_INO)
			if (protocol==PROTO_HOTT)
				option			=	FORCE_HOTT_TUNING;			// Use config-defined tuning value for HOTT
			else
		#endif
				option			=	(uint8_t)PPM_prot_line->option;	// Use radio-defined option value

		if(PPM_prot_line->power)		POWER_FLAG_on;
		if(PPM_prot_line->autobind)
		{
			AUTOBIND_FLAG_on;
			BIND_IN_PROGRESS;	// Force a bind at protocol startup
		}

		protocol_init();

		#ifndef STM32_BOARD
			//Configure PPM interrupt
			#if PPM_pin == 2
				EICRA |= _BV(ISC01);	// The rising edge of INT0 pin D2 generates an interrupt request
				EIMSK |= _BV(INT0);		// INT0 interrupt enable
			#elif PPM_pin == 3
				EICRA |= _BV(ISC11);	// The rising edge of INT1 pin D3 generates an interrupt request
				EIMSK |= _BV(INT1);		// INT1 interrupt enable
			#else
				#error PPM pin can only be 2 or 3
			#endif
		#else
			attachInterrupt(PPM_pin,PPM_decode,FALLING);
		#endif
	}
	else
#endif //ENABLE_PPM
	{ // Serial
		#ifdef ENABLE_SERIAL
			for(uint8_t i=0;i<3;i++)
				cur_protocol[i]=0;
			protocol=0;
			#ifdef CHECK_FOR_BOOTLOADER
				Mprotocol_serial_init(1); 	// Configure serial and enable RX interrupt
			#else
				Mprotocol_serial_init(); 	// Configure serial and enable RX interrupt
			#endif
		#endif //ENABLE_SERIAL
	}
	debugln("Init complete");
	LED2_on;
}

// Main
// Protocol scheduler
void loop()
{ 
	uint16_t next_callback, diff;
	uint8_t count=0;

	while(1)
	{
		while(remote_callback==0 || IS_WAIT_BIND_on || IS_INPUT_SIGNAL_off)
			if(!Update_All())
			{
				cli();								// Disable global int due to RW of 16 bits registers
				OCR1A=TCNT1;						// Callback should already have been called... Use "now" as new sync point.
				sei();								// Enable global int
			}
		TX_MAIN_PAUSE_on;
		tx_pause();
		next_callback=remote_callback()<<1;
		TX_MAIN_PAUSE_off;
		tx_resume();
		cli();										// Disable global int due to RW of 16 bits registers
		OCR1A+=next_callback;						// Calc when next_callback should happen
		#ifndef STM32_BOARD			
			TIFR1=OCF1A_bm;							// Clear compare A=callback flag
		#else
			TIMER2_BASE->SR = 0x1E5F & ~TIMER_SR_CC1IF;	// Clear Timer2/Comp1 interrupt flag
		#endif		
		diff=OCR1A-TCNT1;							// Calc the time difference
		sei();										// Enable global int
		if((diff&0x8000) && !(next_callback&0x8000))
		{ // Negative result=callback should already have been called... 
			debugln("Short CB:%d",next_callback);
		}
		else
		{
			if(IS_RX_FLAG_on || IS_PPM_FLAG_on)
			{ // Serial or PPM is waiting...
				if(++count>10)
				{ //The protocol does not leave enough time for an update so forcing it
					count=0;
					debugln("Force update");
					Update_All();
				}
			}
			#ifndef STM32_BOARD
				while((TIFR1 & OCF1A_bm) == 0)
			#else
				while((TIMER2_BASE->SR & TIMER_SR_CC1IF )==0)
			#endif
			{
				if(diff>900*2)
				{	//If at least 1ms is available update values 
					if((diff&0x8000) && !(next_callback&0x8000))
					{//Should never get here...
						debugln("!!!BUG!!!");
						break;
					}
					count=0;
					Update_All();
          #ifdef DEBUG_SERIAL
  					#ifndef STM32_BOARD 
              if(TIFR1 & OCF1A_bm )
            #else
  						if(TIMER2_BASE->SR & TIMER_SR_CC1IF )
            #endif
  							debugln("Long update");
          #endif
					if(remote_callback==0)
						break;
					cli();							// Disable global int due to RW of 16 bits registers
					diff=OCR1A-TCNT1;				// Calc the time difference
					sei();							// Enable global int
				}

        if(diff>2000*2) //must be less than 2800
        { //If at least 2ms is available print debug and/or update display
          #ifdef HUBSAN_HUB_TELEMETRY
            if (telemetry_link & 1) {
              debugln("Volts: %d, TX_RSSI: %d, Angle: %d, %d, %d", v_lipo1, TX_RSSI, angle_pitch, angle_roll, angle_yaw);
              debugln("Giro: %d, %d, %d, Alt: %d", giro_pitch, giro_roll, giro_yaw, est_altitude);
              #ifdef OLED_DISPLAY
                printVolts(v_lipo1);
              #endif
              telemetry_link &= ~1;    // Sent, clear bit 0
            }
          #endif
//          #ifdef OLED_DISPLAY
//            #ifndef STM32_BOARD 
//              if(TIFR1 & OCF1A_bm )
//            #else
//              if(TIMER2_BASE->SR & TIMER_SR_CC1IF )
//            #endif
//              printLongUpdate();
//          #endif
        }
			}
		}			
	}
}

void End_Bind()
{
	//Request protocol to terminate bind
	if(protocol==PROTO_FRSKYD || protocol==PROTO_FRSKYL || protocol==PROTO_FRSKYX || protocol==PROTO_FRSKYX2 || protocol==PROTO_FRSKYV || protocol==PROTO_FRSKY_R9
	|| protocol==PROTO_DSM_RX || protocol==PROTO_AFHDS2A_RX || protocol==PROTO_FRSKY_RX || protocol==PROTO_BAYANG_RX
	|| protocol==PROTO_AFHDS2A || protocol==PROTO_BUGS || protocol==PROTO_BUGSMINI || protocol==PROTO_HOTT || protocol==PROTO_ASSAN)
		BIND_DONE;
	else
		if(bind_counter>2)
			bind_counter=2;
}

bool Update_All()
{
	#ifdef ENABLE_SERIAL
		#ifdef CHECK_FOR_BOOTLOADER
			if ( (mode_select==MODE_SERIAL) && (NotBootChecking == 0) )
				pollBoot() ;
			else
		#endif
		if(mode_select==MODE_SERIAL && IS_RX_FLAG_on)		// Serial mode and something has been received
		{
			update_serial_data();							// Update protocol and data
			update_channels_aux();
			INPUT_SIGNAL_on;								//valid signal received
			last_signal=millis();
		}
	#endif //ENABLE_SERIAL
	#ifdef ENABLE_PPM
		if(mode_select!=MODE_SERIAL && IS_PPM_FLAG_on)		// PPM mode and a full frame has been received
		{
			uint32_t chan_or=chan_order;
			uint8_t ch;		
			uint8_t channelsCount = PPM_chan_max;
			
			#ifdef ENABLE_DIRECT_INPUTS				
				#ifdef DI_CH1_read
					PPM_data[channelsCount] = DI_CH1_read;
					channelsCount++;
				#endif
				#ifdef DI_CH2_read
					PPM_data[channelsCount] = DI_CH2_read;
					channelsCount++;
				#endif
				#ifdef DI_CH3_read
					PPM_data[channelsCount] = DI_CH3_read;
					channelsCount++;
				#endif
				#ifdef DI_CH4_read
					PPM_data[channelsCount] = DI_CH4_read;
					channelsCount++;
				#endif 
			#endif
			
			for(uint8_t i=0;i<channelsCount;i++)
			{ // update servo data without interrupts to prevent bad read
				uint16_t val;
				cli();										// disable global int
				val = PPM_data[i];
				sei();										// enable global int
				val=map16b(val,PPM_MIN_100*2,PPM_MAX_100*2,CHANNEL_MIN_100,CHANNEL_MAX_100);
				if(val&0x8000) 					val=CHANNEL_MIN_125;
				else if(val>CHANNEL_MAX_125)	val=CHANNEL_MAX_125;
				if(chan_or)
				{
					ch=chan_or>>28;
					if(ch)
						Channel_data[ch-1]=val;
					else
						Channel_data[i]=val;
					chan_or<<=4;
				}
				else
					Channel_data[i]=val;
			}
			PPM_FLAG_off;									// wait for next frame before update
			#ifdef FAILSAFE_ENABLE
				PPM_failsafe();
			#endif
			update_channels_aux();
			INPUT_SIGNAL_on;								// valid signal received
			last_signal=millis();
		}
	#endif //ENABLE_PPM
	update_led_status();
	
	#ifdef SEND_CPPM
		if ( telemetry_link & 0x80 )
		{ // Protocol requests telemetry to be disabled
			if( protocol == PROTO_FRSKY_RX || protocol == PROTO_AFHDS2A_RX || protocol == PROTO_BAYANG_RX || protocol == PROTO_DSM_RX )
			{ // RX protocol
				if(RX_LQI == 0)
					telemetry_link = 0x00;					// restore normal telemetry on connection loss
				else if(telemetry_link & 1)
				{ // New data available
					Send_CCPM_USART1();
					telemetry_link &= 0xFE;					// update done
				}
			}
		}
		else
	#endif
	#ifdef ENABLE_BIND_CH
		if(IS_AUTOBIND_FLAG_on && IS_BIND_CH_PREV_off && Channel_data[BIND_CH-1]>CHANNEL_MAX_COMMAND)
		{ // Autobind is on and BIND_CH went up
			CHANGE_PROTOCOL_FLAG_on;						// reload protocol
			BIND_IN_PROGRESS;								// enable bind
			BIND_CH_PREV_on;
		}
		if(IS_AUTOBIND_FLAG_on && IS_BIND_CH_PREV_on && Channel_data[BIND_CH-1]<CHANNEL_MIN_COMMAND)
		{ // Autobind is on and BIND_CH went down
			BIND_CH_PREV_off;
			End_Bind();
		}
	#endif //ENABLE_BIND_CH
	if(IS_CHANGE_PROTOCOL_FLAG_on)
	{ // Protocol needs to be changed or relaunched for bind
		protocol_init();									// init new protocol
		return true;
	}
	return false;
}

#if defined(FAILSAFE_ENABLE) && defined(ENABLE_PPM)
void PPM_failsafe()
{
	static uint8_t counter=0;
	
	if(IS_BIND_IN_PROGRESS || IS_FAILSAFE_VALUES_on) 	// bind is not finished yet or Failsafe already being sent
		return;
	BIND_SET_INPUT;
	BIND_SET_PULLUP;
	if(IS_BIND_BUTTON_on)
	{// bind button pressed
		counter++;
		if(counter>227)
		{ //after 5s with PPM frames @22ms
			counter=0;
			for(uint8_t i=0;i<NUM_CHN;i++)
				Failsafe_data[i]=Channel_data[i];
			FAILSAFE_VALUES_on;
		}
	}
	else
		counter=0;
	BIND_SET_OUTPUT;
}
#endif

// Update channels direction and Channel_AUX flags based on servo AUX positions
static void update_channels_aux(void)
{
	//Reverse channels direction
	#ifdef REVERSE_AILERON
		reverse_channel(AILERON);
	#endif
	#ifdef REVERSE_ELEVATOR
		reverse_channel(ELEVATOR);
	#endif
	#ifdef REVERSE_THROTTLE
		reverse_channel(THROTTLE);
	#endif
	#ifdef REVERSE_RUDDER
		reverse_channel(RUDDER);
	#endif
		
	//Calc AUX flags
	Channel_AUX=0;
	for(uint8_t i=0;i<8;i++)
		if(Channel_data[CH5+i]>CHANNEL_SWITCH)
			Channel_AUX|=1<<i;
}

// Update led status based on binding and serial
static void update_led_status(void)
{
	if(IS_INPUT_SIGNAL_on)
		if(millis()-last_signal>70)
		{
			INPUT_SIGNAL_off;							//no valid signal (PPM or Serial) received for 70ms
			debugln("No input signal");
		}
	if(blink<millis())
	{
		if(IS_INPUT_SIGNAL_off)
		{
			if(mode_select==MODE_SERIAL)
				blink+=BLINK_SERIAL_TIME;				//blink slowly if no valid serial input
			else
				blink+=BLINK_PPM_TIME;					//blink more slowly if no valid PPM input
		}
		else
			if(remote_callback == 0)
			{ // Invalid protocol
				if(IS_LED_on)							//flash to indicate invalid protocol
					blink+=BLINK_BAD_PROTO_TIME_LOW;
				else
					blink+=BLINK_BAD_PROTO_TIME_HIGH;
			}
			else
			{
				if(IS_WAIT_BIND_on)
				{
					if(IS_LED_on)							//flash to indicate WAIT_BIND
						blink+=BLINK_WAIT_BIND_TIME_LOW;
					else
						blink+=BLINK_WAIT_BIND_TIME_HIGH;
				}
				else
				{
					if(IS_BIND_DONE)
						LED_off;							//bind completed force led on
					blink+=BLINK_BIND_TIME;					//blink fastly during binding
				}
			}
		LED_toggle;
	}
}

#ifdef ENABLE_PPM
uint8_t bank_switch(void)
{
	uint8_t bank=eeprom_read_byte((EE_ADDR)EEPROM_BANK_OFFSET);
	if(bank>=NBR_BANKS)
	{ // Wrong number of bank
		eeprom_write_byte((EE_ADDR)EEPROM_BANK_OFFSET,0x00);	// set bank to 0
		bank=0;
	}
	debugln("Using bank %d", bank);

	phase=3;
	uint32_t check=millis();
	blink=millis();
	while(mode_select==15)
	{ //loop here if the dial is on position 15 for user to select the bank
		if(blink<millis())
		{
			switch(phase & 0x03)
			{ // Flash bank number of times
				case 0:
					LED_on;
					blink+=BLINK_BANK_TIME_HIGH;
					phase++;
					break;
				case 1:
					LED_off;
					blink+=BLINK_BANK_TIME_LOW;
					phase++;
					break;
				case 2:
					if( (phase>>2) >= bank)
					{
						phase=0;
						blink+=BLINK_BANK_REPEAT;
					}
					else
						phase+=2;
					break;
				case 3:
					LED_output;
					LED_off;
					blink+=BLINK_BANK_TIME_LOW;
					phase=0;
					break;
			}
		}
		if(check<millis())
		{
			//Test bind button: for AVR it's shared with the LED so some extra work is needed to check it...
			#ifndef STM32_BOARD
				bool led=IS_LED_on;
				BIND_SET_INPUT;
				BIND_SET_PULLUP;
			#endif
			bool test_bind=IS_BIND_BUTTON_on;
			#ifndef STM32_BOARD
				if(led)
					LED_on;
				else
					LED_off;
				LED_output;
			#endif
			if( test_bind )
			{	// Increase bank
				LED_on;
				bank++;
				if(bank>=NBR_BANKS)
					bank=0;
				eeprom_write_byte((EE_ADDR)EEPROM_BANK_OFFSET,bank);
				debugln("Using bank %d", bank);
				phase=3;
				blink+=BLINK_BANK_REPEAT;
				check+=2*BLINK_BANK_REPEAT;
			}
			check+=1;
		}
	}
	return bank;
}
#endif

inline void tx_pause()
{

}

inline void tx_resume()
{

}

void rf_switch(uint8_t comp)
{
	PE1_off;
	PE2_off;
	switch(comp)
	{
		case SW_CC2500:
			PE2_on;
			break;
		case SW_CYRF:
			PE2_on;
		case SW_NRF:
			PE1_on;
			break;
	}
}

// Protocol start
static void protocol_init()
{
	if(IS_WAIT_BIND_off)
	{
		remote_callback = 0;			// No protocol
		LED_off;						// Led off during protocol init
		modules_reset();				// Reset all modules
		crc16_polynomial = 0x1021;		// Default CRC crc16_polynomial
		crc8_polynomial  = 0x31;		// Default CRC crc8_polynomial
		prev_option = option;

		binding_idx=0;
		
		//Stop CPPM if it was previously running
		#ifdef SEND_CPPM
			release_trainer_ppm();
		#endif
		
		//Set global ID and rx_tx_addr
		MProtocol_id = RX_num + MProtocol_id_master;
		set_rx_tx_addr(MProtocol_id);
		
		#ifdef FAILSAFE_ENABLE
			FAILSAFE_VALUES_off;
		#endif
		DATA_BUFFER_LOW_off;

		SUB_PROTO_VALID;
		option_override = 0xFF;
		
		blink=millis();

		debugln("Protocol selected: %d, sub proto %d, rxnum %d, option %d", protocol, sub_protocol, RX_num, option);
		uint8_t index=0;
		#if defined(FRSKYX_CC2500_INO) && defined(EU_MODULE)
			if( ! ( (protocol == PROTO_FRSKYX || protocol == PROTO_FRSKYX2) && sub_protocol < 2 ) )
		#endif
		while(multi_protocols[index].protocol != 0xFF)
		{
			if(multi_protocols[index].protocol==protocol)
			{
				//Save index
				multi_protocols_index = index;
				//Set the RF switch
				rf_switch(multi_protocols[multi_protocols_index].rfSwitch);
				//Init protocol
				multi_protocols[multi_protocols_index].Init();
				//Save call back function address
				remote_callback = multi_protocols[multi_protocols_index].CallBack;
				//Send a telemetry status right now
				SEND_MULTI_STATUS_on;
				#ifdef DEBUG_SERIAL
					debug("Proto=%s",multi_protocols[multi_protocols_index].ProtoString);
					uint8_t nbr=multi_protocols[multi_protocols_index].nbrSubProto;
					debug(", nbr_sub=%d, Sub=",nbr);
					if(nbr && (sub_protocol&0x07)<nbr)
					{
						uint8_t len=multi_protocols[multi_protocols_index].SubProtoString[0];
						uint8_t offset=len*(sub_protocol&0x07)+1;
						for(uint8_t j=0;j<len;j++)
							debug("%c",multi_protocols[multi_protocols_index].SubProtoString[j+offset]);
					}
					debug(", Opt=%d",multi_protocols[multi_protocols_index].optionType);
					debug(", FS=%d",multi_protocols[multi_protocols_index].failSafe);
					debug(", CHMap=%d",multi_protocols[multi_protocols_index].chMap);
					debugln(", rfSw=%d",multi_protocols[multi_protocols_index].rfSwitch);
				#endif
				break;
			}
			index++;
		}
	}
	
	#if defined(WAIT_FOR_BIND) && defined(ENABLE_BIND_CH)
		if( IS_AUTOBIND_FLAG_on && IS_BIND_CH_PREV_off && (cur_protocol[1]&0x80)==0 && mode_select == MODE_SERIAL)
		{ // Autobind is active but no bind requested by either BIND_CH or BIND. But do not wait if in PPM mode...
			WAIT_BIND_on;
			return;
		}
	#endif
	WAIT_BIND_off;
	CHANGE_PROTOCOL_FLAG_off;

	//Wait 5ms after protocol init
	cli();										// disable global int
	OCR1A = TCNT1 + 5000*2;						// set compare A for callback
	#ifndef STM32_BOARD
		TIFR1 = OCF1A_bm ;						// clear compare A flag
	#else
		TIMER2_BASE->SR = 0x1E5F & ~TIMER_SR_CC1IF;	// Clear Timer2/Comp1 interrupt flag
	#endif	
	sei();										// enable global int
	BIND_BUTTON_FLAG_off;						// do not bind/reset id anymore even if protocol change
}

void update_serial_data()
{
	static bool prev_ch_mapping=false;

	RX_DONOTUPDATE_on;
	RX_FLAG_off;								//data is being processed

	#ifdef SAMSON	// Extremely dangerous, do not enable this unless you know what you are doing...
		if( rx_ok_buff[0]==0x55 && (rx_ok_buff[1]&0x1F)==PROTO_FRSKYD && rx_ok_buff[2]==0x7F && rx_ok_buff[24]==217 && rx_ok_buff[25]==202 )
		{//proto==FRSKYD+sub==7+rx_num==7+CH15==73%+CH16==73%
			rx_ok_buff[1]=(rx_ok_buff[1]&0xE0) | PROTO_FLYSKY;			// change the protocol to Flysky
			memcpy((void*)(rx_ok_buff+4),(void*)(rx_ok_buff+4+11),11);	// reassign channels 9-16 to 1-8
		}
	#endif
	#ifdef BONI	// Extremely dangerous, do not enable this!!! This is really for a special case...
		if(CH14_SW)
			rx_ok_buff[2]=(rx_ok_buff[2]&0xF0)|((rx_ok_buff[2]+1)&0x0F);
	#endif

	if(rx_ok_buff[1]&0x20)						//check range
		RANGE_FLAG_on;
	else
		RANGE_FLAG_off;
	if(rx_ok_buff[1]&0x40)						//check autobind
		AUTOBIND_FLAG_on;
	else
		AUTOBIND_FLAG_off;
	if(rx_ok_buff[2]&0x80)						//if rx_ok_buff[2] ==1,power is low ,0-power high
		POWER_FLAG_off;							//power low
	else
		POWER_FLAG_on;							//power high

	//Forced frequency tuning values for CC2500 protocols
	#if defined(FORCE_FRSKYD_TUNING) && defined(FRSKYD_CC2500_INO)
		if(protocol==PROTO_FRSKYD)
			option=FORCE_FRSKYD_TUNING;			// Use config-defined tuning value for FrSkyD
		else
	#endif
	#if defined(FORCE_FRSKYL_TUNING) && defined(FRSKYL_CC2500_INO)
		if(protocol==PROTO_FRSKYL)
			option=FORCE_FRSKYL_TUNING;			// Use config-defined tuning value for FrSkyL
		else
	#endif
	#if defined(FORCE_FRSKYV_TUNING) && defined(FRSKYV_CC2500_INO)
		if(protocol==PROTO_FRSKYV)
			option=FORCE_FRSKYV_TUNING;			// Use config-defined tuning value for FrSkyV
		else
	#endif
	#if defined(FORCE_FRSKYX_TUNING) && defined(FRSKYX_CC2500_INO)
		if(protocol==PROTO_FRSKYX || protocol==PROTO_FRSKYX2)
			option=FORCE_FRSKYX_TUNING;			// Use config-defined tuning value for FrSkyX
		else
	#endif 
	#if defined(FORCE_FUTABA_TUNING) && defined(FUTABA_CC2500_INO)
		if (protocol==PROTO_FUTABA)
			option=FORCE_FUTABA_TUNING;			// Use config-defined tuning value for SFHSS
		else
	#endif
	#if defined(FORCE_CORONA_TUNING) && defined(CORONA_CC2500_INO)
		if (protocol==PROTO_CORONA)
			option=FORCE_CORONA_TUNING;			// Use config-defined tuning value for CORONA
		else
	#endif
	#if defined(FORCE_SKYARTEC_TUNING) && defined(SKYARTEC_CC2500_INO)
		if (protocol==PROTO_SKYARTEC)
			option=FORCE_SKYARTEC_TUNING;			// Use config-defined tuning value for SKYARTEC
		else
	#endif
	#if defined(FORCE_REDPINE_TUNING) && defined(REDPINE_CC2500_INO)
		if (protocol==PROTO_REDPINE)
			option=FORCE_REDPINE_TUNING;		// Use config-defined tuning value for REDPINE
		else
	#endif
	#if defined(FORCE_RADIOLINK_TUNING) && defined(RADIOLINK_CC2500_INO)
		if (protocol==PROTO_RADIOLINK)
			option			=	FORCE_RADIOLINK_TUNING;		// Use config-defined tuning value for RADIOLINK
		else
	#endif
	#if defined(FORCE_HITEC_TUNING) && defined(HITEC_CC2500_INO)
		if (protocol==PROTO_HITEC)
			option=FORCE_HITEC_TUNING;			// Use config-defined tuning value for HITEC
		else
	#endif
	#if defined(FORCE_HOTT_TUNING) && defined(HOTT_CC2500_INO)
		if (protocol==PROTO_HOTT)
			option=FORCE_HOTT_TUNING;			// Use config-defined tuning value for HOTT
		else
	#endif
			option=rx_ok_buff[3];				// Use radio-defined option value

	#ifdef FAILSAFE_ENABLE
		bool failsafe=false;
		if(rx_ok_buff[0]&0x02)
		{ // Packet contains failsafe instead of channels
			failsafe=true;
			rx_ok_buff[0]&=0xFD;				// Remove the failsafe flag
			FAILSAFE_VALUES_on;					// Failsafe data has been received
			debugln("Failsafe received");
		}
	#endif

	DISABLE_CH_MAP_off;
	DISABLE_TELEM_off;
	if(rx_len>26)
	{//Additional flag received at the end
		rx_ok_buff[0]=(rx_ok_buff[26]&0xF0) | (rx_ok_buff[0]&0x0F);	// Additional protocol numbers and RX_Num available -> store them in rx_ok_buff[0]
		if(rx_ok_buff[26]&0x02)
			DISABLE_TELEM_on;
		if(rx_ok_buff[26]&0x01)
			DISABLE_CH_MAP_on;
	}

	if( (rx_ok_buff[0] != cur_protocol[0]) || ((rx_ok_buff[1]&0x5F) != (cur_protocol[1]&0x5F)) || ( (rx_ok_buff[2]&0x7F) != (cur_protocol[2]&0x7F) ) )
	{ // New model has been selected
		CHANGE_PROTOCOL_FLAG_on;				//change protocol
		WAIT_BIND_off;
		if((rx_ok_buff[1]&0x80)!=0 || IS_AUTOBIND_FLAG_on)
			BIND_IN_PROGRESS;					//launch bind right away if in autobind mode or bind is set
		else
			BIND_DONE;
		protocol=rx_ok_buff[1]&0x1F;			//protocol no (0-31)
		if(!(rx_ok_buff[0]&1))
			protocol+=32;						//protocol no (0-63)
		if(rx_len>26)
			protocol|=rx_ok_buff[26]&0xC0;		//protocol no (0-255)
		sub_protocol=(rx_ok_buff[2]>>4)& 0x07;	//subprotocol no (0-7) bits 4-6
		RX_num=rx_ok_buff[2]& 0x0F;				//rx_num no (0-15)
		if(rx_len>26)
			RX_num|=rx_ok_buff[26]&0x30;		//rx_num no (0-63)
	}
	else
		if( ((rx_ok_buff[1]&0x80)!=0) && ((cur_protocol[1]&0x80)==0) )		// Bind flag has been set
		{ // Restart protocol with bind
			CHANGE_PROTOCOL_FLAG_on;
			BIND_IN_PROGRESS;
		}
		else
			if( ((rx_ok_buff[1]&0x80)==0) && ((cur_protocol[1]&0x80)!=0) )	// Bind flag has been reset
			{ // Request protocol to end bind
				End_Bind();
			}
			
	//store current protocol values
	for(uint8_t i=0;i<3;i++)
		cur_protocol[i] =  rx_ok_buff[i];

	//disable channel mapping
	if(multi_protocols[multi_protocols_index].chMap == 0)
		DISABLE_CH_MAP_off;						//not a protocol supporting ch map to be disabled

	if(prev_ch_mapping!=IS_DISABLE_CH_MAP_on)
	{
		prev_ch_mapping=IS_DISABLE_CH_MAP_on;
		if(IS_DISABLE_CH_MAP_on)
		{
			for(uint8_t i=0;i<4;i++)
				CH_AETR[i]=CH_TAER[i]=CH_EATR[i]=i;
			debugln("DISABLE_CH_MAP_on");
		}
		else
		{
			CH_AETR[0]=AILERON;CH_AETR[1]=ELEVATOR;CH_AETR[2]=THROTTLE;CH_AETR[3]=RUDDER;
			CH_TAER[0]=THROTTLE;CH_TAER[1]=AILERON;CH_TAER[2]=ELEVATOR;CH_TAER[3]=RUDDER;
			CH_EATR[0]=ELEVATOR;CH_EATR[1]=AILERON;CH_EATR[2]=THROTTLE;CH_EATR[3]=RUDDER;
			debugln("DISABLE_CH_MAP_off");
		}
	}
	
	// decode channel/failsafe values
	volatile uint8_t *p=rx_ok_buff+3;
	uint8_t dec=-3;
	for(uint8_t i=0;i<NUM_CHN;i++)
	{
		dec+=3;
		if(dec>=8)
		{
			dec-=8;
			p++;
		}
		p++;
		uint16_t temp=((*((uint32_t *)p))>>dec)&0x7FF;
		#ifdef FAILSAFE_ENABLE
			if(failsafe)
				Failsafe_data[i]=temp;			//value range 0..2047, 0=no pulse, 2047=hold
			else
		#endif
				Channel_data[i]=temp;			//value range 0..2047, 0=-125%, 2047=+125%
	}

	if(rx_len>27)
	{ // Data available for the current protocol
		#if defined(FRSKYX_CC2500_INO) || defined(FRSKYR9_SX1276_INO)
			if((protocol==PROTO_FRSKYX || protocol==PROTO_FRSKYX2 || protocol==PROTO_FRSKY_R9) && rx_len==28)
			{//Protocol waiting for 1 byte during bind
				binding_idx=rx_ok_buff[27];
			}
		#endif
		#ifdef SPORT_SEND
			if((protocol==PROTO_FRSKYX || protocol==PROTO_FRSKYX2 || protocol==PROTO_FRSKY_R9) && rx_len==27+8)
			{//Protocol waiting for 8 bytes
				#define BYTE_STUFF	0x7D
				#define STUFF_MASK	0x20
				//debug("SPort_in: ");
				boolean sport_valid=false;
				for(uint8_t i=28;i<28+7;i++)
					if(rx_ok_buff[i]!=0) sport_valid=true;	//Check that the payload is not full of 0
				if((rx_ok_buff[27]&0x1F) > 0x1B)				//Check 1st byte validity
					sport_valid=false;
				if(sport_valid)
				{
					SportData[SportTail]=0x7E;
					SportTail = (SportTail+1) & (MAX_SPORT_BUFFER-1);
					SportData[SportTail]=rx_ok_buff[27]&0x1F;
					SportTail = (SportTail+1) & (MAX_SPORT_BUFFER-1);
					for(uint8_t i=28;i<28+7;i++)
					{
						if( (rx_ok_buff[i]==BYTE_STUFF) || (rx_ok_buff[i]==0x7E) )
						{//stuff
							SportData[SportTail]=BYTE_STUFF;
							SportTail = (SportTail+1) & (MAX_SPORT_BUFFER-1);
							SportData[SportTail]=rx_ok_buff[i]^STUFF_MASK;
						}
						else
							SportData[SportTail]=rx_ok_buff[i];
						//debug("%02X ",SportData[SportTail]);
						SportTail = (SportTail+1) & (MAX_SPORT_BUFFER-1);
					}
					uint8_t used = SportTail;
					if ( SportHead > SportTail )
						used += MAX_SPORT_BUFFER - SportHead ;
					else
						used -= SportHead ;
					if ( used >= MAX_SPORT_BUFFER-(MAX_SPORT_BUFFER>>2) )
					{
						DATA_BUFFER_LOW_on;
						SEND_MULTI_STATUS_on;	//Send Multi Status ASAP to inform the TX
						debugln("Low buf=%d,h=%d,t=%d",used,SportHead,SportTail);
					}
				}
			}
		#endif //SPORT_SEND
		#ifdef DSM_FWD_PGM
			if(protocol==PROTO_DSM && rx_len==27+7)
			{//Protocol waiting for 7 bytes
				memcpy(DSM_SerialRX_val, (const void *)&rx_ok_buff[27],7);
				DSM_SerialRX=true;
			}
		#endif
		#ifdef MULTI_CONFIG_INO
			if(protocol==PROTO_CONFIG && rx_len==27+7)
			{//Protocol waiting for 7 bytes
				memcpy(CONFIG_SerialRX_val, (const void *)&rx_ok_buff[27],7);
				CONFIG_SerialRX=true;
			}
		#endif
	}

	RX_DONOTUPDATE_off;
	#ifdef ORANGE_TX
		cli();
	#else
		UCSR0B &= ~_BV(RXCIE0);					// RX interrupt disable
	#endif
	if(IS_RX_MISSED_BUFF_on)					// If the buffer is still valid
	{	
		if(rx_idx>=26 && rx_idx<RXBUFFER_SIZE)
		{
			rx_len=rx_idx;
			memcpy((void*)rx_ok_buff,(const void*)rx_buff,rx_len);// Duplicate the buffer
			RX_FLAG_on;							// Data to be processed next time...
		}
		RX_MISSED_BUFF_off;
	}
	#ifdef ORANGE_TX
		sei();
	#else
		UCSR0B |= _BV(RXCIE0) ;					// RX interrupt enable
	#endif
}

void modules_reset()
{
	#ifdef	CC2500_INSTALLED
		CC2500_Reset();
	#endif
	#ifdef	A7105_INSTALLED
		A7105_Reset();
	#endif
	#ifdef	CYRF6936_INSTALLED
		CYRF_Reset();
	#endif
	#ifdef	NRF24L01_INSTALLED
		NRF24L01_Reset();
	#endif
	#ifdef	SX1276_INSTALLED
		SX1276_Reset();
	#endif

	//Wait for every component to reset
	delayMilliseconds(100);
	prev_power=0xFD;		// unused power value
}

#ifdef CHECK_FOR_BOOTLOADER
	void Mprotocol_serial_init( uint8_t boot )
#else
	void Mprotocol_serial_init()
#endif
{
	#ifdef ORANGE_TX
		PORTC.OUTSET = 0x08 ;
		PORTC.DIRSET = 0x08 ;

		USARTC0.BAUDCTRLA = 19 ;
		USARTC0.BAUDCTRLB = 0 ;
		
		USARTC0.CTRLB = 0x18 ;
		USARTC0.CTRLA = (USARTC0.CTRLA & 0xCC) | 0x11 ;
		USARTC0.CTRLC = 0x2B ;
		UDR0 ;
		#ifdef INVERT_SERIAL
			PORTC.PIN3CTRL |= 0x40 ;
		#endif
		#ifdef CHECK_FOR_BOOTLOADER
			if ( boot )
			{
				USARTC0.BAUDCTRLB = 0 ;
				USARTC0.BAUDCTRLA = 33 ;		// 57600
				USARTC0.CTRLA = (USARTC0.CTRLA & 0xC0) ;
				USARTC0.CTRLC = 0x03 ;			// 8 bit, no parity, 1 stop
				USARTC0.CTRLB = 0x18 ;			// Enable Tx and Rx
				PORTC.PIN3CTRL &= ~0x40 ;
			}
		#endif // CHECK_FOR_BOOTLOADER
	#elif defined STM32_BOARD
		#ifdef CHECK_FOR_BOOTLOADER
			if ( boot )
			{
				usart2_begin(57600,SERIAL_8N1);
				USART2_BASE->CR1 &= ~USART_CR1_RXNEIE ;
				(void)UDR0 ;
			}
			else
		#endif // CHECK_FOR_BOOTLOADER
		{
			usart2_begin(100000,SERIAL_8E2);
			USART2_BASE->CR1 |= USART_CR1_PCE_BIT;
		}
		USART2_BASE->CR1 &= ~ USART_CR1_TE;		//disable transmit
		usart3_begin(100000,SERIAL_8E2);
	#else
		//ATMEGA328p
		#include <util/setbaud.h>	
		UBRR0H = UBRRH_VALUE;
		UBRR0L = UBRRL_VALUE;
		UCSR0A = 0 ;	// Clear X2 bit
		//Set frame format to 8 data bits, even parity, 2 stop bits
		UCSR0C = _BV(UPM01)|_BV(USBS0)|_BV(UCSZ01)|_BV(UCSZ00);
		while ( UCSR0A & (1 << RXC0) )	//flush receive buffer
			UDR0;
		//enable reception and RC complete interrupt
		UCSR0B = _BV(RXEN0)|_BV(RXCIE0);//rx enable and interrupt
		#ifdef CHECK_FOR_BOOTLOADER
			if ( boot )
			{
				UBRR0H = 0;
				UBRR0L = 33;			// 57600
				UCSR0C &= ~_BV(UPM01);	// No parity
				UCSR0B &= ~_BV(RXCIE0);	// No rx interrupt
				UCSR0A |= _BV(U2X0);	// Double speed mode USART0
			}
		#endif // CHECK_FOR_BOOTLOADER
	#endif //ORANGE_TX
}

#ifdef STM32_BOARD
	void usart2_begin(uint32_t baud,uint32_t config )
	{
		usart_init(USART2); 
		usart_config_gpios_async(USART2,GPIOA,PIN_MAP[PA3].gpio_bit,GPIOA,PIN_MAP[PA2].gpio_bit,config);
		LED2_output;
		usart_set_baud_rate(USART2, STM32_PCLK1, baud);
		usart_enable(USART2);
	}
	void usart3_begin(uint32_t baud,uint32_t config )
	{
		usart_init(USART3);
		usart_config_gpios_async(USART3,GPIOB,PIN_MAP[PB11].gpio_bit,GPIOB,PIN_MAP[PB10].gpio_bit,config);
		usart_set_baud_rate(USART3, STM32_PCLK1, baud);
		USART3_BASE->CR3 &= ~USART_CR3_EIE & ~USART_CR3_CTSIE;	// Disable receive
		USART3_BASE->CR1 &= ~USART_CR1_RE & ~USART_CR1_RXNEIE & ~USART_CR1_PEIE & ~USART_CR1_IDLEIE ; // Disable RX and interrupts
    	USART3_BASE->CR1 |= (USART_CR1_TE | USART_CR1_UE);		// Enable USART3 and TX
	}
	void init_HWTimer()
	{	
		HWTimer2.pause();										// Pause the timer2 while we're configuring it
		TIMER2_BASE->PSC = 35;									// 36-1;for 72 MHZ /0.5sec/(35+1)
		TIMER2_BASE->ARR = 0xFFFF;								// Count until 0xFFFF
		HWTimer2.setMode(TIMER_CH1, TIMER_OUTPUT_COMPARE);		// Main scheduler
		TIMER2_BASE->SR = 0x1E5F & ~TIMER_SR_CC2IF;				// Clear Timer2/Comp2 interrupt flag
		TIMER2_BASE->DIER &= ~TIMER_DIER_CC2IE;					// Disable Timer2/Comp2 interrupt
		HWTimer2.refresh();										// Refresh the timer's count, prescale, and overflow
		HWTimer2.resume();

		#ifdef ENABLE_SERIAL
			HWTimer3.pause();									// Pause the timer3 while we're configuring it
			TIMER3_BASE->PSC = 35;								// 36-1;for 72 MHZ /0.5sec/(35+1)
			TIMER3_BASE->ARR = 0xFFFF;							// Count until 0xFFFF
			HWTimer3.setMode(TIMER_CH2, TIMER_OUTPUT_COMPARE);	// Serial check
			TIMER3_BASE->SR = 0x1E5F & ~TIMER_SR_CC2IF;			// Clear Timer3/Comp2 interrupt flag
			HWTimer3.attachInterrupt(TIMER_CH2,ISR_COMPB);		// Assign function to Timer3/Comp2 interrupt
			TIMER3_BASE->DIER &= ~TIMER_DIER_CC2IE;				// Disable Timer3/Comp2 interrupt
			HWTimer3.refresh();									// Refresh the timer's count, prescale, and overflow
			HWTimer3.resume();
		#endif
	}
#endif

#ifdef CHECK_FOR_BOOTLOADER
void pollBoot()
{
	uint8_t rxchar ;
	uint8_t lState = BootState ;
	uint8_t millisTime = millis();				// Call this once only

	#ifdef ORANGE_TX
	if ( USARTC0.STATUS & USART_RXCIF_bm )
	#elif defined STM32_BOARD
	if ( USART2_BASE->SR & USART_SR_RXNE )
	#else
	if ( UCSR0A & ( 1 << RXC0 ) )
	#endif
	{
		rxchar = UDR0 ;
		BootCount += 1 ;
		if ( ( lState == BOOT_WAIT_30_IDLE ) || ( lState == BOOT_WAIT_30_DATA ) )
		{
			if ( lState == BOOT_WAIT_30_IDLE )	// Waiting for 0x30
				BootTimer = millisTime ;		// Start timeout
			if ( rxchar == 0x30 )
				lState = BOOT_WAIT_20 ;
			else
				lState = BOOT_WAIT_30_DATA ;
		}
		else
			if ( lState == BOOT_WAIT_20 && rxchar == 0x20 )	// Waiting for 0x20
				lState = BOOT_READY ;
	}
	else // No byte received
	{
		if ( lState != BOOT_WAIT_30_IDLE )		// Something received
		{
			uint8_t time = millisTime - BootTimer ;
			if ( time > 5 )
			{
				#ifdef	STM32_BOARD
				if ( BootCount > 4 )
				#else
				if ( BootCount > 2 )
				#endif
				{ // Run normally
					NotBootChecking = 0xFF ;
					Mprotocol_serial_init( 0 ) ;
				}
				else if ( lState == BOOT_READY )
				{
					#ifdef	STM32_BOARD
						nvic_sys_reset();
						while(1);						/* wait until reset */
					#else
						cli();							// Disable global int due to RW of 16 bits registers
						void (*p)();
						#ifndef ORANGE_TX
							p = (void (*)())0x3F00 ;	// Word address (0x7E00 byte)
						#else
							p = (void (*)())0x4000 ;	// Word address (0x8000 byte)
						#endif
						(*p)() ;						// go to boot
					#endif
				}
				else
				{
					lState = BOOT_WAIT_30_IDLE ;
					BootCount = 0 ;
				}
			}
		}
	}
	BootState = lState ;
}
#endif //CHECK_FOR_BOOTLOADER

// Convert 32b id to rx_tx_addr
static void set_rx_tx_addr(uint32_t id)
{ // Used by almost all protocols
	rx_tx_addr[0] = (id >> 24) & 0xFF;
	rx_tx_addr[1] = (id >> 16) & 0xFF;
	rx_tx_addr[2] = (id >>  8) & 0xFF;
	rx_tx_addr[3] = (id >>  0) & 0xFF;
	rx_tx_addr[4] = (rx_tx_addr[2]&0xF0)|(rx_tx_addr[3]&0x0F);
}

static uint32_t random_id(uint16_t address, uint8_t create_new)
{
	#ifndef FORCE_GLOBAL_ID
		uint32_t id=0;

		if(eeprom_read_byte((EE_ADDR)(address+10))==0xf0 && !create_new)
		{  // TXID exists in EEPROM
			for(uint8_t i=4;i>0;i--)
			{
				id<<=8;
				id|=eeprom_read_byte((EE_ADDR)address+i-1);
			}
			if(id!=0x2AD141A7)	//ID with seed=0
			{
				//debugln("Read ID from EEPROM");
				return id;
			}
		}
		// Generate a random ID
		#if defined STM32_BOARD
			#define STM32_UUID ((uint32_t *)0x1FFFF7E8)
			if (!create_new)
			{
				id = STM32_UUID[0] ^ STM32_UUID[1] ^ STM32_UUID[2];
				debugln("Generated ID from STM32 UUID");
			}
			else
		#endif
				id = random(0xfefefefe) + ((uint32_t)random(0xfefefefe) << 16);

		for(uint8_t i=0;i<4;i++)
			eeprom_write_byte((EE_ADDR)address+i,id >> (i*8));
		eeprom_write_byte((EE_ADDR)(address+10),0xf0);//write bind flag in eeprom.
		return id;
	#else
		(void)address;
		(void)create_new;
		return FORCE_GLOBAL_ID;
	#endif
}

// Generate frequency hopping sequence in the range [02..77]
static void __attribute__((unused)) calc_fh_channels(uint8_t num_ch)
{
	uint8_t idx = 0;
	uint32_t rnd = MProtocol_id;
	uint8_t max=(num_ch/3)+2;
	
	while (idx < num_ch)
	{
		uint8_t i;
		uint8_t count_2_26 = 0, count_27_50 = 0, count_51_74 = 0;

		rnd = rnd * 0x0019660D + 0x3C6EF35F; // Randomization
		// Use least-significant byte. 73 is prime, so channels 76..77 are unused
		uint8_t next_ch = ((rnd >> 8) % 73) + 2;
		// Keep a distance of 5 between consecutive channels
		if (idx !=0)
		{
			if(hopping_frequency[idx-1]>next_ch)
			{
				if(hopping_frequency[idx-1]-next_ch<5)
					continue;
			}
			else
				if(next_ch-hopping_frequency[idx-1]<5)
					continue;
		}
		// Check that it's not duplicated and spread uniformly
		for (i = 0; i < idx; i++) {
			if(hopping_frequency[i] == next_ch)
				break;
			if(hopping_frequency[i] <= 26)
				count_2_26++;
			else if (hopping_frequency[i] <= 50)
				count_27_50++;
			else
				count_51_74++;
		}
		if (i != idx)
			continue;
		if ( (next_ch <= 26 && count_2_26 < max) || (next_ch >= 27 && next_ch <= 50 && count_27_50 < max) || (next_ch >= 51 && count_51_74 < max) )
			hopping_frequency[idx++] = next_ch;//find hopping frequency
	}
}

static uint8_t __attribute__((unused)) bit_reverse(uint8_t b_in)
{
    uint8_t b_out = 0;
    for (uint8_t i = 0; i < 8; ++i)
	{
        b_out = (b_out << 1) | (b_in & 1);
        b_in >>= 1;
    }
    return b_out;
}

static void __attribute__((unused)) crc16_update(uint8_t a, uint8_t bits)
{
	crc ^= a << 8;
    while(bits--)
        if (crc & 0x8000)
            crc = (crc << 1) ^ crc16_polynomial;
		else
            crc = crc << 1;
}

static void __attribute__((unused)) crc8_update(uint8_t byte)
{
	crc8 = crc8 ^ byte;
	for ( uint8_t j = 0; j < 8; j++ )
		if ( crc8 & 0x80 )
			crc8 = (crc8<<1) ^ crc8_polynomial;
		else
			crc8 <<= 1;
}

/**************************/
/**************************/
/**  Interrupt routines  **/
/**************************/
/**************************/

//PPM
#ifdef ENABLE_PPM
	#ifdef ORANGE_TX
		#if PPM_pin == 2
			ISR(PORTD_INT0_vect)
		#else
			ISR(PORTD_INT1_vect)
		#endif
	#elif defined STM32_BOARD
		void PPM_decode()
	#else
		#if PPM_pin == 2
			ISR(INT0_vect, ISR_NOBLOCK)
		#else
			ISR(INT1_vect, ISR_NOBLOCK)
		#endif
	#endif
	{	// Interrupt on PPM pin
		static int8_t chan=0,bad_frame=1;
		static uint16_t Prev_TCNT1=0;
		uint16_t Cur_TCNT1;

		Cur_TCNT1 = TCNT1 - Prev_TCNT1 ;	// Capture current Timer1 value
		if(Cur_TCNT1<1600)
			bad_frame=1;					// bad frame
		else
			if(Cur_TCNT1>4400)
			{  //start of frame
				if(chan>=MIN_PPM_CHANNELS)
				{
					PPM_FLAG_on;			// good frame received if at least 4 channels have been seen
					if(chan>PPM_chan_max) PPM_chan_max=chan;	// Saving the number of channels received
				}
				chan=0;						// reset channel counter
				bad_frame=0;
			}
			else
				if(bad_frame==0)			// need to wait for start of frame
				{  //servo values between 800us and 2200us will end up here
					PPM_data[chan]=Cur_TCNT1;
					if(chan++>=MAX_PPM_CHANNELS)
						bad_frame=1;		// don't accept any new channels
				}
		Prev_TCNT1+=Cur_TCNT1;
	}
#endif //ENABLE_PPM

//Serial RX
#ifdef ENABLE_SERIAL
	#ifdef ORANGE_TX
		ISR(USARTC0_RXC_vect)
	#elif defined STM32_BOARD
		void __irq_usart2()			
	#else
		ISR(USART_RX_vect)
	#endif
	{	// RX interrupt
		#ifdef ORANGE_TX
			if((USARTC0.STATUS & 0x1C)==0)							// Check frame error, data overrun and parity error
		#elif defined STM32_BOARD
			if((USART2_BASE->SR & USART_SR_RXNE) && (USART2_BASE->SR &0x0F)==0)					
		#else
			UCSR0B &= ~_BV(RXCIE0) ;								// RX interrupt disable
			sei() ;
			if((UCSR0A&0x1C)==0)									// Check frame error, data overrun and parity error
		#endif
		{ // received byte is ok to process
			if(rx_idx==0||discard_frame==true)
			{	// Let's try to sync at this point
				RX_MISSED_BUFF_off;									// If rx_buff was good it's not anymore...
				rx_idx=0;discard_frame=false;
				rx_buff[0]=UDR0;
				#ifdef FAILSAFE_ENABLE
					if((rx_buff[0]&0xFC)==0x54)						// If 1st byte is 0x54, 0x55, 0x56 or 0x57 it looks ok
				#else
					if((rx_buff[0]&0xFE)==0x54)						// If 1st byte is 0x54 or 0x55 it looks ok
				#endif
				{
					#if defined STM32_BOARD
						TIMER3_BASE->CCR2=TIMER3_BASE->CNT + 500;	// Next byte should show up within 250us (1 byte = 120us)
						TIMER3_BASE->SR = 0x1E5F & ~TIMER_SR_CC2IF;	// Clear Timer3/Comp2 interrupt flag
						TIMER3_BASE->DIER |= TIMER_DIER_CC2IE;		// Enable Timer3/Comp2 interrupt
					#else
						TX_RX_PAUSE_on;
						tx_pause();
						cli();										// Disable global int due to RW of 16 bits registers
						OCR1B = TCNT1 + 500;						// Next byte should show up within 250us (1 byte = 120us)
						sei();										// Enable global int
						TIFR1 = OCF1B_bm ;							// clear OCR1B match flag
						SET_TIMSK1_OCIE1B ;							// enable interrupt on compare B match
					#endif
					rx_idx++;
				}
			}
			else
			{
				if(rx_idx>=RXBUFFER_SIZE)
				{
					discard_frame=true; 								// Too many bytes being received...
					debugln("RX frame too long");
				}
				else
				{
					rx_buff[rx_idx++]=UDR0;							// Store received byte
					#if defined STM32_BOARD
						TIMER3_BASE->CCR2=TIMER3_BASE->CNT + 500;	// Next byte should show up within 250us (1 byte = 120us)
					#else
						cli();										// Disable global int due to RW of 16 bits registers
						OCR1B = TCNT1 + 500;						// Next byte should show up within 250us (1 byte = 120us)
						sei();										// Enable global int
					#endif
				}
			}
		}
		else
		{
			rx_idx=UDR0;											// Dummy read
			rx_idx=0;
			discard_frame=true;										// Error encountered discard full frame...
			debugln("Bad frame RX");
		}
		if(discard_frame==true)
		{
			#ifdef STM32_BOARD
				TIMER3_BASE->DIER &= ~TIMER_DIER_CC2IE;				// Disable Timer3/Comp2 interrupt
			#else							
				CLR_TIMSK1_OCIE1B;									// Disable interrupt on compare B match
				TX_RX_PAUSE_off;
				tx_resume();
			#endif
		}
		#if not defined (ORANGE_TX) && not defined (STM32_BOARD)
			cli() ;
			UCSR0B |= _BV(RXCIE0) ;									// RX interrupt enable
		#endif
	}

	//Serial timer
	#ifdef ORANGE_TX
		ISR(TCC1_CCB_vect)
	#elif defined STM32_BOARD
		void ISR_COMPB()
	#else
		ISR(TIMER1_COMPB_vect)
	#endif
	{	// Timer1 compare B interrupt
		if(rx_idx>=26 && rx_idx<=RXBUFFER_SIZE)
		{
			// A full frame has been received
			if(!IS_RX_DONOTUPDATE_on)
			{ //Good frame received and main is not working on the buffer
				rx_len=rx_idx;
				memcpy((void*)rx_ok_buff,(const void*)rx_buff,rx_idx);	// Duplicate the buffer
				RX_FLAG_on;											// Flag for main to process data
			}
			else
				RX_MISSED_BUFF_on;									// Notify that rx_buff is good
			#ifdef MULTI_SYNC
				cli();
				last_serial_input=TCNT1;
				sei();
			#endif
		}
		#ifdef DEBUG_SERIAL
			else
				debugln("RX frame size incorrect");
		#endif
		discard_frame=true;
		#ifdef STM32_BOARD
			TIMER3_BASE->DIER &= ~TIMER_DIER_CC2IE;					// Disable Timer3/Comp2 interrupt
		#else
			CLR_TIMSK1_OCIE1B;										// Disable interrupt on compare B match
			TX_RX_PAUSE_off;
			tx_resume();
		#endif
	}
#endif //ENABLE_SERIAL

/**************************/
/**************************/
/**    CPPM  routines    **/
/**************************/
/**************************/
#ifdef SEND_CPPM
	#define PPM_CENTER 1500*2
	uint32_t TrainerTimer ;
	bool CppmInitialised = false;
	uint16_t *TrainerPulsePtr ;
	uint16_t TrainerPpmStream[10] ;
	int16_t CppmChannels[8] ;

	void setupTrainerPulses()
	{
		uint32_t i ;
		uint32_t total ;
		uint32_t pulse ;
		uint16_t *ptr ;
		uint32_t p = 8 ;
		int16_t PPM_range = 512*2 ;										//range of 0.7..1.7msec

		ptr = TrainerPpmStream ;

		total = 22500u*2;												//Minimum Framelen=22.5 ms

		if ( (millis() - TrainerTimer) < 400 )
		{
			for ( i = 0 ; i < p ; i += 1 )
			{
				pulse = max( (int)min(CppmChannels[i],PPM_range),-PPM_range) + PPM_CENTER ;

				total -= pulse ;
				*ptr++ = pulse ;
			}
		}
		*ptr++ = total ;
		*ptr = 0 ;
		TIMER1_BASE->CCR1 = total - 1500 ;								// Update time
		TIMER1_BASE->CCR2 = 300*2 ;
	}

	void init_trainer_ppm()
	{
		// Timer 1, channel 2 on PA9
		RCC_BASE->APB2ENR |= RCC_APB2ENR_TIM1EN ;						// Enable clock
		setupTrainerPulses() ;
		RCC_BASE->APB2ENR |= RCC_APB2ENR_IOPAEN ;						// Enable portA clock
		RCC_BASE->APB2ENR &= ~RCC_APB2ENR_USART1EN ;					// Disable USART1

		GPIOA_BASE->CRH &= ~0x00F0 ;
		GPIOA_BASE->CRH |= 0x00A0 ;										// AF PP OP2MHz

		HWTimer1.pause() ;												// Pause the timer1 while we're configuring it
		TIMER1_BASE->ARR = *TrainerPulsePtr++ ;
		TIMER1_BASE->PSC = 72000000  / 2000000 - 1 ;					// 0.5uS
		TIMER1_BASE->CCR2 = 600 ;										// 300 uS pulse
		TIMER1_BASE->CCR1 = 5000 ;										// 2500 uS pulse
		TIMER1_BASE->CCMR1 = 0x6000 ;									// PWM mode 1 (header file has incorrect bits)
		TIMER1_BASE->EGR = 1 ;
		TIMER1_BASE->CCER = TIMER_CCER_CC2E ;
		TIMER1_BASE->DIER |= TIMER_DIER_UIE ;
		TIMER1_BASE->CR1 = TIMER_CR1_CEN ;
		nvic_irq_set_priority(NVIC_TIMER1_CC, 4 ) ;
		nvic_irq_set_priority(NVIC_TIMER1_UP, 4 ) ;
		HWTimer1.attachInterrupt(TIMER_UPDATE_INTERRUPT,tim1_up);		// Assign function to Timer1/Comp2 interrupt
		HWTimer1.attachInterrupt(TIMER_CH1,tim1_cc);					// Assign function to Timer1/Comp2 interrupt

		CppmInitialised = true ;
		HWTimer1.resume() ;
	}

	void release_trainer_ppm()
	{
		if ( CppmInitialised )
		{
			TIMER1_BASE->CR1 = 0 ;
			pinMode(PA9,INPUT) ;
			CppmInitialised = false ;
		}
	}

	void tim1_up()
	{
		#define TIMER1_SR_MASK	0x1FFF
		// PPM out update interrupt
		if ( (TIMER1_BASE->DIER & TIMER_DIER_UIE) && ( TIMER1_BASE->SR & TIMER_SR_UIF ) )
		{
			GPIOA_BASE->BRR = 0x0200 ;
			TIMER1_BASE->SR = TIMER1_SR_MASK & ~TIMER_SR_UIF ; 			// Clear flag
			TIMER1_BASE->ARR = *TrainerPulsePtr++ ;
			if ( *TrainerPulsePtr == 0 )
			{
				TIMER1_BASE->SR = 0x1FFF & ~TIMER_SR_CC1IF ;			// Clear this flag
				TIMER1_BASE->DIER |= TIMER_DIER_CC1IE ;					// Enable this interrupt
				TIMER1_BASE->DIER &= ~TIMER_DIER_UIE ;					// Stop this interrupt
			}
		}
	}

	void tim1_cc()
	{
		if ( ( TIMER1_BASE->DIER & TIMER_DIER_CC1IE ) && ( TIMER1_BASE->SR & TIMER_SR_CC1IF ) )
		{
			// compare interrupt
			TIMER1_BASE->DIER &= ~TIMER_DIER_CC1IE ;					// Stop this interrupt
			TIMER1_BASE->SR = 0x1FFF & ~TIMER_SR_CC1IF ;				// Clear flag

			setupTrainerPulses() ;

			TrainerPulsePtr = TrainerPpmStream ;
			TIMER1_BASE->SR = 0x1FFF & ~TIMER_SR_UIF ;					// Clear this flag
			TIMER1_BASE->DIER |= TIMER_DIER_UIE ;						// Enable this interrupt
		}
	}

	void Send_CCPM_USART1()
	{
		if ( CppmInitialised == false )
			init_trainer_ppm() ;
		TrainerTimer = millis() ;
		len = packet_in[3] ;
		uint32_t bitsavailable = 0 ;
		uint32_t bits = 0 ; ;
		uint32_t i ;
		int16_t value ;
		uint8_t *packet ;
		packet = &packet_in[4] ;
		i = packet_in[2] ;	// Start channel
		// Load changed channels
		while ( len )
		{
			while ( bitsavailable < 11 )
			{
				bits |= *packet++ << bitsavailable ;
				bitsavailable += 8 ;
			}
			value = bits & 0x07FF ;
			value -= 0x0400 ;
			bitsavailable -= 11 ;
			bits >>= 11 ;
			if ( i < 8 )
				CppmChannels[i] = value * 5 / 4 ;
			i++ ;
			len-- ;
		}
	}
#endif	

/**************************/
/**************************/
/**    Arduino random    **/
/**************************/
/**************************/
#if not defined (ORANGE_TX) && not defined (STM32_BOARD)
	static void random_init(void)
	{
		cli();					// Temporarily turn off interrupts, until WDT configured
		MCUSR = 0;				// Use the MCU status register to reset flags for WDR, BOR, EXTR, and POWR
		WDTCSR |= _BV(WDCE);	// WDT control register, This sets the Watchdog Change Enable (WDCE) flag, which is  needed to set the prescaler
		WDTCSR = _BV(WDIE);		// Watchdog interrupt enable (WDIE)
		sei();					// Turn interupts on
	}

	static uint32_t random_value(void)
	{
		while (!gWDT_entropy);
		return gWDT_entropy;
	}

	// Random interrupt service routine called every time the WDT interrupt is triggered.
	// It is only enabled at startup to generate a seed.
	ISR(WDT_vect)
	{
		static uint8_t gWDT_buffer_position=0;
		#define gWDT_buffer_SIZE 32
		static uint8_t gWDT_buffer[gWDT_buffer_SIZE];
		gWDT_buffer[gWDT_buffer_position] = TCNT1L; // Record the Timer 1 low byte (only one needed) 
		gWDT_buffer_position++;                     // every time the WDT interrupt is triggered
		if (gWDT_buffer_position >= gWDT_buffer_SIZE)
		{
			// The following code is an implementation of Jenkin's one at a time hash
			for(uint8_t gWDT_loop_counter = 0; gWDT_loop_counter < gWDT_buffer_SIZE; ++gWDT_loop_counter)
			{
				gWDT_entropy += gWDT_buffer[gWDT_loop_counter];
				gWDT_entropy += (gWDT_entropy << 10);
				gWDT_entropy ^= (gWDT_entropy >> 6);
			}
			gWDT_entropy += (gWDT_entropy << 3);
			gWDT_entropy ^= (gWDT_entropy >> 11);
			gWDT_entropy += (gWDT_entropy << 15);
			WDTCSR = 0;	// Disable Watchdog interrupt
		}
	}
#endif
