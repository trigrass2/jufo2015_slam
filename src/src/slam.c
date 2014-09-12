#include "FreeRTOS.h"
#include "task.h"

#include "stm32f4xx.h"
#include "stm32f4_discovery.h"
#include "stm32f4xx_conf.h"
#include "utils.h"
#include "debug.h"
#include "printf.h"
#include "xv11.h"
#include "main.h"
#include "comm.h"
#include "comm_api.h"

#include "gui.h"
#include "slam.h"
#include "slamdefs.h"

#include "SSD1963.h"
#include "SSD1963_api.h"

#include <math.h>
#include <stdlib.h>

slam_t slam; //slam container structure
mot_t motor; //Motor information (encoder etc.)

int32_t processMovement_enc_l_old = 0, processMovement_enc_r_old = 0; //For processMovement functions

///////////////Private prototypes
void slam_processMovement(slam_t *slam);

///////SLAM Task
portTASK_FUNCTION( vSLAMTask, pvParameters ) {
	portTickType xLastWakeTime;

	#if(configDEBUG_MESSAGES == 1)
		printf("xTask SLAM started.\r\n");
	#endif

	xLastWakeTime = xTaskGetTickCount();

	motor.driver_standby = 0;

	slam_init(&slam, 1000, 1000, 0, 90, (XV11_t *) &xv11, &motor.enc_l, &motor.enc_r);
	comm_readMotorData(&motor);
	processMovement_enc_l_old = *slam.sensordata.odo_l;
	processMovement_enc_r_old = *slam.sensordata.odo_r;

	/*for(uint8_t i = 0; i < 20; i++)
	{
		motor.enc_l += 80;
		motor.enc_r += 100;
		slam_processMovement(&slam);
		slam.map.px[(int)(slam.robot_pos.coord.x / 10)][(int)(slam.robot_pos.coord.y / 10)][0] = 255;
	}*/

	for(;;)
	{
		if(mapping)
		{
			motor.speed_l_to = 18;
			motor.speed_r_to = 13;

			slam_processMovement(&slam);
			slam_map_update(&slam, 20, 100);
		}
		else
		{
			motor.speed_l_to = 0;
			motor.speed_r_to = 0;
		}

		comm_setMotor(&motor);
		comm_readMotorData(&motor);

		int var = slam_distanceScanToMap(&slam, &slam.robot_pos);
		printf("match: %i\n", var);

		vTaskDelayUntil( &xLastWakeTime, ( 200 / portTICK_RATE_MS ) );
	}
}

//////////////////////////////////////////////////////////////////////////////////////////
/// \brief slam_processMovement
///		Transfers the driven encoder distance to a cartesian posisition and adds it to the
///		old robot position in the slam structure.
/// \param slam
///		slam container structure
/// \param mot
///		motor information structure
///
/// Source:
/// http://www6.in.tum.de/Main/Publications/5224223.pdf

void slam_processMovement(slam_t *slam)
{
	float dl_enc, dr_enc; //Driven distance (since last function call) in mm.
	float dx = 0, dy = 0, dpsi = 0, dist_driven = 0;

	dl_enc = (*slam->sensordata.odo_l - processMovement_enc_l_old) * 2 * WHEELRADIUS * M_PI / TICKSPERREV;
	dr_enc = (*slam->sensordata.odo_r - processMovement_enc_r_old) * 2 * WHEELRADIUS * M_PI / TICKSPERREV;
	processMovement_enc_l_old = *slam->sensordata.odo_l;
	processMovement_enc_r_old = *slam->sensordata.odo_r;

	if(fabsf(dl_enc - dr_enc) > 0)
	{
		float r = -WHEELDIST * (dl_enc + dr_enc) / (2 * (dr_enc - dl_enc));
		dpsi = -(dr_enc - dl_enc) / WHEELDIST;

		dx = r * sinf(dpsi + (slam->robot_pos.psi * 180 / M_PI)) - r * sinf((slam->robot_pos.psi * 180 / M_PI));
		dy = -r * cosf(dpsi + (slam->robot_pos.psi * 180 / M_PI)) + r * cosf((slam->robot_pos.psi * 180 / M_PI));

		dpsi *= 180 / M_PI; //Convert radian to degree
	}
	else // basically going straight
	{
		dx = dl_enc * cosf(slam->robot_pos.psi * M_PI / 180);
		dy = dr_enc * sinf(slam->robot_pos.psi * M_PI / 180);
	}

	dist_driven = sqrtf(dx * dx + dy * dy);

	slam->robot_pos.coord.x += dist_driven * cosf((180 - slam->robot_pos.psi + dpsi) * M_PI / 180);
	slam->robot_pos.coord.y += dist_driven * sinf((180 - slam->robot_pos.psi + dpsi) * M_PI / 180);
	slam->robot_pos.psi += dpsi;
}

/////////////////////////////////////////////////////////////////
/// \brief slam_LCD_DispMap
///		Displays the slam map
/// \param x0
///		X start coordinate
/// \param y0
///		Y start coordinate
/// \param slam
///		slam container structure

void slam_LCD_DispMap(int16_t x0, int16_t y0, slam_t *slam)
{
	u8 mapval = 0;

	LCD_SetArea(x0,
				y0,
				x0 + (MAP_SIZE_X_MM / MAP_RESOLUTION_MM) - 1,
				y0 + (MAP_SIZE_Y_MM / MAP_RESOLUTION_MM) - 1);

	LCD_WriteCommand(CMD_WR_MEMSTART);

	Clr_Cs;

	for (int16_t y = (MAP_SIZE_Y_MM / MAP_RESOLUTION_MM) - 1; y >= 0; y--)
	{
		for (int16_t x = 0; x < (MAP_SIZE_X_MM / MAP_RESOLUTION_MM); x++)
		{
			mapval = slam->map.px[x][y][slam->robot_pos.coord.z];
			LCD_WriteData(0xffff - RGB565CONVERT(mapval, mapval, mapval));
		}
	}

	Set_Cs;
}
