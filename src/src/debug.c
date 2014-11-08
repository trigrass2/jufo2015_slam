/////////////////////////////////////////////////////////////////////////////
/// debug.c
/// All relevant debugging tasks, including sending data to the PC User Interface
/// via bluetooth
/////////////////////////////////////////////////////////////////////////////
/// Protocol for PC User interface
/// [Startseq][Length][Checksum][ID][Data]
///
/// [Startseq]: ["PCUI_MSG"] (8 chars)
/// [Lenght]: [{b2},{b1_lsb}] (16bit; 2 chars)
/// [checksum]: (Sum of all Data chars) [{b4},{b3},{b2},{b1_lsb}] (32bit; 4 chars)
/// [ID]: (3 chars)
///		["MPD"]: Map Data (13 chars). Rob->PC
///					- resolution (mm) (1byte)
///					- size x (2byte)
///					- size y "
///					- size z (1byte)
///					- rob x  (2byte)
///					- rob y  "
///					- rob z  (1byte)
///					- dir    (2byte)
///		["MAP"]: Map transmission (size y + 3 chars). Rob->PC
///					- current stage (1byte)
///					- current line (transmission linewise) (2byte)
///					- Pixel (size x byte)
///		["WAY"]: Waypoints (n waypoints * 7 chars). Bidirectional
///					- ID (1byte)
///					- x  (2byte)
///					- y  "
///					- z  "
///		["STA"]: Status (1 char). Bidirectional.
///					- start/stop
///	[Data]: [Lenght] chars


#include <stdarg.h>
#include <ctype.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "stm32f4xx.h"
#include "stm32f4_discovery.h"

#include "debug.h"
#include "utils.h"
#include "gui.h"
#include "main.h"
#include "printf.h"
#include "comm_api.h"
#include "comm.h"

// ============================================================================
portTASK_FUNCTION( vDebugTask, pvParameters ) {
	portTickType xLastWakeTime;
	//portBASE_TYPE xStatus;
	//UBaseType_t uxHighWaterMark;

	/* The parameters are not used. */
	( void ) pvParameters;

	xLastWakeTime = xTaskGetTickCount();

	#if(configDEBUG_MESSAGES == 1)
		printf("xTask DEBUG started.\r\n");
	#endif


	for(;;)
	{
		//pcui_sendMap(&slam);
		vTaskDelayUntil( &xLastWakeTime, ( 1000 / portTICK_RATE_MS ) );
	}
}

void USART2_IRQHandler(void) //PCUI Receive...
{
	// check if the USART1 receive interrupt flag was set
	if( USART_GetITStatus(USART2, USART_IT_RXNE) )
	{
		STM_EVAL_LEDToggle(LED5);
		char data = USART2->DR;
		data *= 2;
		//Process...
	}
}

//////////////////////////////////////////////////////////////////////////////
/// \brief pcui_sendMsg
///			Sends a message (definition: see protocol) via bluetooth to the
///			PC (including calculating and sending checksum)
/// \param id
///			ID of the message (see protocol)
/// \param length
///			Length of message (in bytes)
/// \param msg
///			Message
///
void pcui_sendMsg(char *id, u_int32_t length, char *msg)
{
	int32_t checksum = 0;
	char len[2];
	char chk[4];

	len[0] = (char) (length & 0x00ff);
	len[1] = (char) ((length & 0xff00) >> 8);

	for(u_int32_t i = 0; i < length; i++)
		checksum += msg[i];

	chk[0] = (char) (checksum & 0x000000ff);
	chk[1] = (char) ((checksum & 0x0000ff00) >> 8);
	chk[2] = (char) ((checksum & 0x00ff0000) >> 16);
	chk[3] = (char) ((checksum & 0xff000000) >> 24);

	puts_l("PCUI_MSG", 8); //Startseq
	puts_l(len, 2);
	puts_l(chk, 4);
	puts_l(id, 3);
	puts_l(msg, length);
}

//////////////////////////////////////////////////////////////////////////////
/// \brief pcui_sendMap
///			Sends the map to the computer. Also sends the map frame data and
///			the robot position!
/// \param slam
///			Pointer to slam container
///
void pcui_sendMap(slam_t *slam)
{
	char mpd[13]; //MaPData message container array

	mpd[0] = MAP_RESOLUTION_MM;
	mpd[1] = (MAP_SIZE_X_MM & 0xff);
	mpd[2] = (MAP_SIZE_X_MM & 0xff00) >> 8;
	mpd[3] = (MAP_SIZE_Y_MM & 0xff);
	mpd[4] = (MAP_SIZE_Y_MM & 0xff00) >> 8;
	mpd[5] = MAP_SIZE_Z_LAYERS;
	mpd[6] = ((char)slam->robot_pos.coord.x & 0xff); //Has to be converted from float to char
	mpd[7] = ((char)slam->robot_pos.coord.x & 0xff00) >> 8;
	mpd[8] = ((char)slam->robot_pos.coord.y & 0xff);
	mpd[9] = ((char)slam->robot_pos.coord.y & 0xff00) >> 8;
	mpd[10] = slam->robot_pos.coord.z;
	mpd[11] = ((char)slam->robot_pos.psi & 0xff);
	mpd[12] = ((char)slam->robot_pos.psi & 0xff00) >> 8;

	pcui_sendMsg("MPD", 13, mpd); //Send mapdata

	for(uint8_t z = 0; z < MAP_SIZE_Z_LAYERS; z++)
	{
		for(int16_t y = 0; y < (MAP_SIZE_Y_MM / MAP_RESOLUTION_MM); y++)
		{
			/// Send a message for each line of the map. If we send the whole map, we would calculate the
			/// checksum and be ready one second after that - in the meantime, the map would have changed
			/// and the checksum does not matches anymore. Therefore, we save the current line (y), transmit it
			/// with the line information and the matching checksum and receive it as message on the pc. If the
			/// checksum does not matches there, we simply ignore the line and go on.

			char buf[(MAP_SIZE_X_MM / MAP_RESOLUTION_MM) + 3];
			buf[0] = z; //Current stage to send
			buf[1] = y & 0xff; //Current line to send
			buf[2] = (y & 0xff00) >> 8;

			for(int i = 0; i < (MAP_SIZE_X_MM / MAP_RESOLUTION_MM); i++) //Map information itself beginning in byte 3
				buf[i + 3] = slam->map.px[i][y][z];

			pcui_sendMsg("MAP", (MAP_SIZE_X_MM / MAP_RESOLUTION_MM) + 3, buf); //Send line
		}
	}
}


// Simply print to the debug console a string based on the type of reset.
// ============================================================================
void vDebugPrintResetType( void ) {

	if ( PWR_GetFlagStatus( PWR_FLAG_WU ) )
		printf( "PWR: Wake Up flag\r\n" );
	if ( PWR_GetFlagStatus( PWR_FLAG_SB ) )
		printf( "PWR: StandBy flag.\r\n" );
	if ( PWR_GetFlagStatus( PWR_FLAG_PVDO ) )
		printf( "PWR: PVD Output.\r\n" );
	if ( PWR_GetFlagStatus( PWR_FLAG_BRR ) )
		printf( "PWR: Backup regulator ready flag.\r\n" );
	if ( PWR_GetFlagStatus( PWR_FLAG_REGRDY ) )
		printf( "PWR: Main regulator ready flag.\r\n" );

	if ( RCC_GetFlagStatus( RCC_FLAG_BORRST ) )
		printf( "RCC: POR/PDR or BOR reset\r\n" );
	if ( RCC_GetFlagStatus( RCC_FLAG_PINRST ) )
		printf( "RCC: Pin reset.\r\n" );
	if ( RCC_GetFlagStatus( RCC_FLAG_PORRST ) )
		printf( "RCC: POR/PDR reset.\r\n" );
	if ( RCC_GetFlagStatus( RCC_FLAG_SFTRST ) )
		printf( "RCC: Software reset.\r\n" );
	if ( RCC_GetFlagStatus( RCC_FLAG_IWDGRST ) )
		printf( "RCC: Independent Watchdog reset.\r\n" );
	if ( RCC_GetFlagStatus( RCC_FLAG_WWDGRST ) )
		printf( "RCC: Window Watchdog reset.\r\n" );
	if ( RCC_GetFlagStatus( RCC_FLAG_LPWRRST ) )
		printf( "RCC: Low Power reset.\r\n" );
}
