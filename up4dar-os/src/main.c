/*

Copyright (C) 2011,2012   Michael Dirska, DL1BFF (dl1bff@mdx.de)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/



#include <asf.h>

#include "board.h"
#include "gpio.h"
#include "power_clocks_lib.h"


#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "up_io/serial.h"

#include "up_dstar/phycomm.h"

#include "up_dstar/dstar.h"

#include "up_dstar/txtest.h"

#include "up_io/eth.h"
#include "up_io/eth_txmem.h"
#include "up_net/ipneigh.h"

#include "up_dstar/vdisp.h"
#include "up_dstar/rtclock.h"
#include "up_dstar/ambe.h"

#include "up_io/wm8510.h"

#include "up_dstar/audio_q.h"

#include "gcc_builtin.h"

#include "up_net/snmp.h"
#include "up_net/snmp_data.h"

#include "up_net/ipv4.h"


#include "up_net/lldp.h"
#include "up_dstar/dcs.h"

#include "up_net/dhcp.h"
#include "up_dstar/gps.h"

#include "up_io/lcd.h"
#include "up_dstar/settings.h"


#define standard_TASK_PRIORITY		( tskIDLE_PRIORITY + 1 )


U32 counter = 0;
U32 errorCounter = 0;


audio_q_t  audio_tx_q;
audio_q_t  audio_rx_q;

ambe_q_t microphone;

static int32_t voltage = 0;

int snmp_get_voltage(int32_t arg, uint8_t * res, int * res_len, int maxlen)
{
	return snmp_encode_int( voltage, res, res_len, maxlen );
}



static unsigned char x_counter = 0;

/*

static xComPortHandle debugOutput;



static void printDebug(const char * s)
{
	if (debugOutput >= 0)
	{
		vSerialPutString(debugOutput, s);
	}
}

*/

/*
static U32 counter_FRO = 0;
static U32 counter_RRE = 0;
static U32 counter_ROVR = 0;
*/


static char tmp_buf[7];



uint32_t  pwm_value = 520;

/*
static void set_pwm(void)
{
	char buf[10];
	
	vdisp_i2s(buf, 8, 10, 0, pwm_value);
	vdisp_prints_xy( 0, 56, VDISP_FONT_6x8, 0, buf );
	
	AVR32_PWM.channel[0].cdty = pwm_value;
}

*/

#define DLE 0x10
#define STX 0x02
#define ETX 0x03

static void send_cmd(const char* Befehl, const short size){
	const char* nachricht = Befehl;
	
	char  buf[100];
	char  zeichen;
	short ind = 2;
	
	
	buf[0] = DLE;
	buf[1] = STX;
	
	for (int i=0; i<size; ++i){
		zeichen = *nachricht++;
		if (zeichen == DLE){
			buf[ind++] = DLE;
			buf[ind++] = DLE;
		} else {
			buf[ind++] = zeichen;
		}
	}
	
	buf[ind++] = DLE;
    buf[ind++] = ETX;
	
	phyCommSend(buf, ind);
}


static const char tx_on[1] = {0x10};
static char header[40];

static char send_voice[11];
static char send_data [ 4];

// static const char YOUR[9] = "CQCQCQ  ";
// static const char RPT2[9] = "DB0DF  G";
// static const char RPT1[9] = "DB0DF  B";
// static const char MY1[9]  = "DL1BFF  ";
// static const char MY2[5]  = "    ";

static int phy_frame_counter = 0;
static int txmsg_counter = 0;

static const char direct_callsign[8] = "DIRECT  ";

static void phy_start_tx(void)
{

	// Schalte UP4DAR auf Senden um
	
    send_cmd(tx_on, 1);
	
	// Bereite einen Header vor
	
	header[0] = 0x20;
	header[1] = (SETTING_CHAR(C_DV_DIRECT) == 1) ? 0 :	// "1st control byte"
				  (1 << 6)	// Setze den Repeater-Flag
				;
				
	
	
	header[2] = 0x0;				// "2nd control byte"
	header[3] = 0x0;				// "3rd control byte"
	
	for (short i=0; i<CALLSIGN_LENGTH; ++i){
		if (SETTING_CHAR(C_DV_DIRECT) == 1)
		{
			header[4+i] = direct_callsign[i];
		}
		else
		{
			header[4+i] = settings.s.rpt2[ ((SETTING_CHAR(C_DV_USE_RPTR_SETTING) - 1)*CALLSIGN_LENGTH) + i];
		}		
	}
	
	for (short i=0; i<CALLSIGN_LENGTH; ++i){
		if (SETTING_CHAR(C_DV_DIRECT) == 1)
		{
			header[12+i] = direct_callsign[i];
		}
		else
		{
			header[12+i] = settings.s.rpt1[ ((SETTING_CHAR(C_DV_USE_RPTR_SETTING) - 1)*CALLSIGN_LENGTH) + i];
		}
	}
	
	for (short i=0; i<CALLSIGN_LENGTH; ++i){
		header[20+i] = settings.s.urcall[ ((SETTING_CHAR(C_DV_USE_URCALL_SETTING  ) - 1)*CALLSIGN_LENGTH) + i];
	}
	
	for (short i=0; i<CALLSIGN_LENGTH; ++i){
		header[28+i] = settings.s.my_callsign[i];
	}
	
	for (short i=0; i<CALLSIGN_EXT_LENGTH; ++i){
		header[36+i] = settings.s.my_ext[i];
	}
	
	// Bis zu 70ms kann man sich Zeit lassen, bevor die Header-Daten uebergeben werden.
	// Die genaue Wartezeit ist natruerlich von TX-DELAY abh�ngig.
	//usleep(70000);
	
	// vTaskDelay (50); // 50ms
	
	send_cmd(header, 40);
	
	phy_frame_counter = 0;
	txmsg_counter = 0;
}


static int slow_data_count;
static uint8_t slow_data[5];

// const char dstar_tx_msg[20] = "Michael, Berlin, D23";
// --------------------------- 12345678901234567890

static void send_phy ( const unsigned char * d )
{
	send_voice[0] = 0x21;
	send_voice[1] = 0x01;
	for (short k=0; k<9; ++k)
		send_voice[2+k] = d[k];
	send_cmd(send_voice, 11);

	if (phy_frame_counter > 0)
	{
		send_data[0] = 0x22;
		
		if ((txmsg_counter == 0) && (phy_frame_counter >= 1) && (phy_frame_counter <= 8))
		{
			int i = (phy_frame_counter - 1) >> 1;
			if (phy_frame_counter & 1)
			{
				send_data[1] = 0x40 + i;
				send_data[2] = settings.s.txmsg[ i * 5 + 0 ];
				send_data[3] = settings.s.txmsg[ i * 5 + 1 ];
			}
			else
			{
				send_data[1] = settings.s.txmsg[ i * 5 + 2 ];
				send_data[2] = settings.s.txmsg[ i * 5 + 3 ];
				send_data[3] = settings.s.txmsg[ i * 5 + 4 ];
			}
		}
		else
		{
				if (phy_frame_counter & 1)
				{
					slow_data_count = gps_get_slow_data(slow_data);
					
					if (slow_data_count == 0)
					{
						send_data[1] = 0x66;
						send_data[2] = 0x66;
						send_data[3] = 0x66;
					}
					else
					{
						send_data[1] = (0x30 + slow_data_count);
						send_data[2] = slow_data[ 0 ];
						send_data[3] = slow_data[ 1 ];
					}
				}
				else
				{
					if (slow_data_count <= 2)
					{
						send_data[1] = 0x66;
						send_data[2] = 0x66;
						send_data[3] = 0x66;
					}
					else
					{
						send_data[1] = slow_data[ 2 ];
						send_data[2] = slow_data[ 3 ];
						send_data[3] = slow_data[ 4 ];
					}
				}
			
		}
		
		send_cmd(send_data, 4);
	}
	
	phy_frame_counter++;
	
	if (phy_frame_counter >= 21)
	{
		phy_frame_counter = 0;
		txmsg_counter ++;
		if (txmsg_counter >= 60)
		{
			txmsg_counter = 0;
		}
	}		
}	
	






// #define FWUPLOAD_BUTTON 1

#if defined(FWUPLOAD_BUTTON)


#include "phy_firmware.h"

static unsigned char fw_data[3];



static void fw_upload(void)
{
	fw_data[0] = 0xe1;
	send_cmd((char *) fw_data, 1);
			
	vTaskDelay (120); // 120ms
			
	long fw_counter;
			
	for (fw_counter = 0; fw_counter < (sizeof fw_bytes); fw_counter += 512)
	{
		int i;
				
		fw_data[0] = DLE;
		fw_data[1] = STX;
		fw_data[2] = 0xe2;
		phyCommSend((char *) fw_data, 3);
	
		for (i=0; i < 512; i++)
		{
			
			fw_data[0] = fw_bytes[fw_counter + i];
					
			if (fw_data[0] == DLE)
			{
				fw_data[1] = DLE;
				phyCommSend((char *) fw_data, 2);						
			}
			else
			{
				phyCommSend((char *) fw_data, 1);						
			}
						
		}	
				
		fw_data[0] = DLE;
		fw_data[1] = ETX;
		phyCommSend((char *) fw_data, 2);
			
		vTaskDelay (2000); // 2s
	}
			
	fw_data[0] = 0xe3;
	send_cmd((char *) fw_data, 1);
}


#endif




#define NUMBER_OF_KEYS  7

static int touchKeyCounter[NUMBER_OF_KEYS] = { 0,0,0,0,0,0, 0 };
	
	
static const int button_pins[NUMBER_OF_KEYS] = {
			AVR32_PIN_PA18,
			AVR32_PIN_PA19,
			AVR32_PIN_PA20,
			AVR32_PIN_PA28, // SW3 -> analog channel
			AVR32_PIN_PA22,
			AVR32_PIN_PA23,
			AVR32_PIN_PA28
		};
	
int debug1;

int maxTXQ;

int8_t lldp_counter = 0;


static void show_dcs_state(void)
{
	char buf[10];
	dcs_get_current_reflector_name(buf);
	buf[8] = 0;
	vdisp_prints_xy( 95, 27, VDISP_FONT_4x6, 
		dcs_is_connected(), buf );
}		

static void set_phy_parameters(void) 
{	
	uint8_t value;
	
	value = SETTING_SHORT(S_PHY_TXDELAY) & 0xFF;
	snmp_set_phy_sysparam(1, &value, 1);
	value = SETTING_CHAR(C_PHY_TXGAIN) & 0xFF;
	snmp_set_phy_sysparam(2, &value, 1);
	value = SETTING_CHAR(C_PHY_RXINV) & 0xFF;
	snmp_set_phy_sysparam(3, &value, 1);
	value = SETTING_CHAR(C_PHY_TXDCSHIFT) & 0xFF;
	snmp_set_phy_sysparam(4, &value, 1);
	value = SETTING_SHORT(S_PHY_MATFST) & 0xFF;
	snmp_set_phy_sysparam(5, &value, 1);
	value = SETTING_SHORT(S_PHY_LENGTHOFVW) & 0xFF;
	snmp_set_phy_sysparam(6, &value, 1);
}

static void vButtonTask( void *pvParameters )
{
	
	for(;;)
	{
		vTaskDelay(100); 			
			
		int v = AVR32_ADC.cdr0;
			
		AVR32_ADC.cr = 2; // start new conversion
			
		// v *= 330 * 430;  // 3.3V , r1+r2 = 43k
		// v /= 1023 * 56;  // inputmax=1023, r1=5.6k
		
		v *= 330 * 347;  // 3.3V , r1+r2 = 34.7k
		v /= 1023 * 47;  // inputmax=1023, r1=4.7k
			
		if (v > 400)  // more than 4 volts
		{
			voltage = v * 10; // millivolt
		}
		else // voltage too low -> key pressed
		{
			vdisp_clear_rect (0, 0, 128, 64);
			vdisp_prints_xy( 30, 48, VDISP_FONT_6x8, 0, "Mode 4 (DVR)" );
			dstarChangeMode(4);
		}			
			
			
			
		// gpio_toggle_pin(AVR32_PIN_PB27);
			
		// gpio_toggle_pin(AVR32_PIN_PB19);
			
		// eth_send_vdisp_frame();
	
		
		int i;
				
		for (i=0; i < NUMBER_OF_KEYS; i++)
		{
			if (gpio_get_pin_value(button_pins[i]) == 0)
			{
				touchKeyCounter[i] ++;
			}
			else
			{
				touchKeyCounter[i] = 0;
					
				/*
				if (i==6)  // PTT off
				{
					ambe_stop_encode();
				}
				*/
			}					
					
			if ((touchKeyCounter[i] == 2) && (tx_active == 0))
			{
				switch(i)
				{
				case 0:
					vdisp_clear_rect (0, 0, 128, 64);
						
#if defined(FWUPLOAD_BUTTON)
					vdisp_prints_xy( 30, 48, VDISP_FONT_6x8, 0, "FW upload" );
						
					fw_upload();
						
					vdisp_prints_xy( 30, 38, VDISP_FONT_6x8, 0, "FW upload done!" );
#else
					vdisp_prints_xy( 30, 48, VDISP_FONT_6x8, 0, "Service Mode" );
					dstarChangeMode(1);
#endif
					break;

				case 1:
					/*
					vdisp_clear_rect (0, 0, 128, 64);
					vdisp_prints_xy( 30, 48, VDISP_FONT_6x8, 0, "TX test" );
					dstarResetCounters();
					tx_active = 1;	
					*/
					//send_dcs(0x0001, 1);
					dcs_on_off();
					break;
										
				case 2:
					vdisp_clear_rect (0, 0, 128, 64);
					vdisp_prints_xy( 30, 48, VDISP_FONT_6x8, 0, "Mode 2 (SUM)" );
					
					set_phy_parameters();
					
					dstarChangeMode(2);
					// vdisp_prints_xy( 30, 48, VDISP_FONT_6x8, 0, "Mode 4 (DVR)" );
					// dstarChangeMode(4);
					break;
					
				/*					
				case 3:
					vdisp_clear_rect (0, 0, 128, 64);
					vdisp_prints_xy( 30, 48, VDISP_FONT_6x8, 0, "Mode 2 (SUM)" );
					dstarChangeMode(2);
					break;
					*/
							
				case 4:
					/* vdisp_clear_rect (0, 0, 128, 64);
						dstarResetCounters();
					tx_active = 1;
					*/
							
					/*
					pwm_value --;
					set_pwm(); */
						
					dcs_select_reflector(0);
					show_dcs_state();
					touchKeyCounter[i] = 0;
						
					x_counter ++ ;
						
					lcd_show_layer( x_counter & 0x01);
						
					break;
							
				case 5:
						/*
					pwm_value ++;
					set_pwm(); */
							
					dcs_select_reflector(1);
					show_dcs_state();
					touchKeyCounter[i] = 0;
					break;
						
						
				// case 6: // PTT
					
				//	ambe_start_encode();
						
					/*
					{
						extern int audio_max;
						audio_max = 0;
					}
					*/							
				//	break;	
						
				} // switch
			} // if	
		}  // for Buttons
		
	
	}	// for(;;)
		
}
		
		
static void vServiceTask( void *pvParameters )
{
	
	int last_backlight = -1;
	int last_contrast = -1;
	

	for (;;)
	{	
		
		vTaskDelay(500); 
		
		// gpio_toggle_pin(AVR32_PIN_PB28);
		//gpio_toggle_pin(AVR32_PIN_PB18);
			
		// x_counter ++;
			
		
		// rtclock_disp_xy(84, 0, x_counter & 0x02, 1);
		rtclock_disp_xy(84, 0, 2, 1);
			
			
			
		vdisp_i2s( tmp_buf, 5, 10, 0, voltage);
		tmp_buf[3] = tmp_buf[2];
		tmp_buf[2] = '.';
		tmp_buf[4] = 'V';
		tmp_buf[5] = 0;
			
			
		vdisp_prints_xy( 55, 0, VDISP_FONT_4x6, 0, tmp_buf );
			
			
		// ethernet status
			
		int v = AVR32_MACB.MAN.data;
			
		AVR32_MACB.man = 0x60C20000; // read register 0x10
			
		//  vdisp_i2s( tmp_buf, 4, 16, 1, maxTXQ );
		//  vdisp_prints_xy( 80, 56, VDISP_FONT_6x8, 0, tmp_buf );
			
		const char * net_status = "     ";
			
		dhcp_set_link_state( v & 1 );
			
		if (v & 1)  // Ethernet link is active
		{
			v = ((v >> 1) & 0x03) ^ 0x01;
				
			switch (v)
			{
				case 0:
					net_status = " 10HD";
					break;
				case 1:
					net_status = "100HD";
					break;
				case 2:
					net_status = " 10FD";
					break;
				case 3:
					net_status = "100FD";
					break;
			}
				
			AVR32_MACB.ncfgr = 0x00000800 | v;  // SPD, FD, CLK = MCK / 32 -> 1.875 MHz
				
			vdisp_prints_xy( 28, 0, VDISP_FONT_4x6, 
				(dhcp_is_ready() != 0) ? 0 : 1, net_status );
		}
		else
		{
			vdisp_prints_xy( 28, 0, VDISP_FONT_4x6, 0, net_status );
		}
			
			
			
		dhcp_service();				
			
		/*
			char buf[10];
			vdisp_i2s(buf, 4, 16, 1, AVR32_USBB.usbsta );
			vdisp_prints_xy( 0, 56, VDISP_FONT_6x8, 0, buf );
			  
			vdisp_i2s(buf, 4, 16, 1, AVR32_USBB.usbfsm );
			vdisp_prints_xy( 30, 56, VDISP_FONT_6x8, 0, buf );
			  
			vdisp_i2s(buf, 8, 16, 1, AVR32_USBB.usbcon );
			vdisp_prints_xy( 60, 56, VDISP_FONT_6x8, 0, buf );
			*/
			
		// printDebug("Test von DL1BFF\r\n");
			
		lldp_counter --;
			
		if (lldp_counter < 0)
		{ 
			lldp_counter = 10;
			lldp_send();
				
				if ((AVR32_USBB.usbsta & 0x0400) == 0) // ID pin pulled low
				{
				gpio_set_pin_low (AVR32_PIN_PB17); // turn on power
				}	
				else
				{
				gpio_set_pin_high (AVR32_PIN_PB17);
				}		
		}				
			 
		ipneigh_service();
			 
			
		dcs_service();
		show_dcs_state();
		
		if (last_backlight != SETTING_CHAR(C_DISP_BACKLIGHT))
		{
			lcd_set_backlight( SETTING_CHAR(C_DISP_BACKLIGHT) );
			last_backlight = SETTING_CHAR(C_DISP_BACKLIGHT);
		}			
	
	    if (last_contrast != SETTING_CHAR(C_DISP_CONTRAST))
	    {
		    lcd_set_contrast( SETTING_CHAR(C_DISP_CONTRAST) );
		    last_contrast = SETTING_CHAR(C_DISP_CONTRAST);
	    }
		
	}
}


/*-----------------------------------------------------------*/



static void vRXTXEthTask( void *pvParameters )
{
	while (1)
	{
	//	debug1 ++;
		eth_rx(); // receive packets
		eth_txmem_flush_q();  // send frames in Q
		vTaskDelay(1);
	}		
}





static void vTXTask( void *pvParameters )
{
	int tx_state = 0;
	
	int session_id = 5555;

	for(;;)
	{
		switch(tx_state)
		{
		case 0:  // PTT off
			if (gpio_get_pin_value(AVR32_PIN_PA28) == 0)  // PTT pressed
			{
				tx_state = 1;
				ambe_start_encode();
				phy_start_tx();
				vTaskDelay(80); // pre-buffer audio while PHY sends header
				dcs_reset_tx_counters();
				gps_reset_slow_data();
				ambe_q_flush(& microphone, 1);
				vTaskDelay(45); // pre-buffer one AMBE frame
				
				session_id ++;
			}
			else
			{
				vTaskDelay(100); // watch for PTT every 100ms
			}
			break;
			
		case 1:  // PTT on
			if (gpio_get_pin_value(AVR32_PIN_PA28) != 0)  // PTT released
			{
				tx_state = 2;
				ambe_stop_encode();
			}
			else
			{
				if (ambe_q_get(& microphone, dcs_ambe_data) != 0) // queue unexpectedly empty
				{
					ambe_stop_encode();
					send_dcs( session_id, 1); // send end frame
					send_phy ( dcs_ambe_data );
					tx_state = 3; // wait for PTT release
				}					
				else
				{
					send_dcs(  session_id, 0); // send normal frame
					send_phy ( dcs_ambe_data );
					vTaskDelay(20); // wait 20ms
				}
			}
			break;
				
		case 2: // PTT off, drain microphone data
			if (ambe_q_get(& microphone, dcs_ambe_data) != 0) // queue empty
			{
				send_dcs( session_id, 1); // send end frame
				send_phy ( dcs_ambe_data );
				tx_state = 3; // wait for PTT
			}
			else
			{
				send_dcs(  session_id, 0); // send normal frame
				send_phy ( dcs_ambe_data );
				vTaskDelay(20); // wait 20ms
			}
			break;
		
		case 3: // PTT on, wait for release
			if (gpio_get_pin_value(AVR32_PIN_PA28) != 0)  // PTT released
			{
				tx_state = 0;
				wm8510_beep(
					SETTING_SHORT(S_PTT_BEEP_DURATION),
					SETTING_SHORT(S_PTT_BEEP_FREQUENCY),
					SETTING_CHAR(C_PTT_BEEP_VOLUME)
					);
			}
			else
			{
				vTaskDelay(100); // watch for PTT every 100ms
			}
			break;
		}
	}		
	
}	


static xQueueHandle dstarQueue;






int main (void)
{
	board_init();
	
	
/*	debugOutput = xSerialPortInitMinimal( 0, 4800, 10 );
	
	if (debugOutput < 0)
	{
		// TODO: error handling
	}
	*/

	// unsigned char * pixelBuf;
	
	settings_init();
	
	eth_init();
	
	vdisp_init();
	
	int main_screen = vd_new_screen();
	
	if (main_screen != 0)
	{
		// error handling..
	}
	
	int gps_screen = vd_new_screen();
	
	if (gps_screen != 1)
	{
		// error handling..
	}
	
	int save_screen = vd_new_screen();
	
	if (save_screen != 2)
	{
		// error handling..
	}
	
	vdisp_clear_rect(0,0, 128, 64);
	
	ipv4_init(); // includes ipneigh_init()
	dhcp_init();
	
	lcd_init();
		
	

	
	xTaskCreate( vServiceTask, (signed char *) "srv", configMINIMAL_STACK_SIZE, ( void * ) 0, 
			standard_TASK_PRIORITY, ( xTaskHandle * ) NULL );
			
	xTaskCreate( vButtonTask, (signed char *) "button", configMINIMAL_STACK_SIZE, ( void * ) 0, 
			standard_TASK_PRIORITY, ( xTaskHandle * ) NULL );
	
	xTaskCreate( vRXTXEthTask, (signed char *) "rxtxeth", 300, ( void * ) 0,
			standard_TASK_PRIORITY, ( xTaskHandle * ) NULL );
	
	
	vdisp_prints_xy(0, 6, VDISP_FONT_8x12, 0,  "Universal");
	vdisp_prints_xy(0, 18, VDISP_FONT_8x12, 0, " Platform");
	vdisp_prints_xy(0, 30, VDISP_FONT_8x12, 0, "  for Digital");
	vdisp_prints_xy(0, 42, VDISP_FONT_8x12, 0, "   Amateur Radio");
	
	dstarQueue = xQueueCreate( 10, sizeof (struct dstarPacket) );

	dstarInit( dstarQueue );
	
	phyCommInit( dstarQueue );
	
	txtest_init();
	
	dcs_init();
	
	audio_q_initialize(& audio_tx_q);
	audio_q_initialize(& audio_rx_q);
	
	ambe_q_initialize(& microphone);
	
	ambe_init(& audio_tx_q, & audio_rx_q, & microphone);
	
	wm8510Init( & audio_tx_q, & audio_rx_q );
	
	xTaskCreate( vTXTask, (signed char *) "TX", 300, ( void * ) 0, standard_TASK_PRIORITY, ( xTaskHandle * ) NULL );


	gps_init();
	
	if (eth_txmem_init() != 0)
	{
		vdisp_prints_xy( 0, 56, VDISP_FONT_6x8, 0, "MEM failed!!!" );
	}
	

	vTaskStartScheduler();
  
	return 0;
}
