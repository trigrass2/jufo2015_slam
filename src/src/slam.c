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

#include "gui.h"
#include "slam.h"
#include "slamdefs.h"

#include "SSD1963.h"
#include "SSD1963_api.h"

slam_t slam; //slam container structure

/*ts_map_t map;
ts_state_t state;
ts_robot_parameters_t robot_parameters;
ts_laser_parameters_t laser_parameters;
ts_position_t robot_position;
ts_sensor_data_t sensor_data;
ts_scan_t laserscan;*/

portTASK_FUNCTION( vSLAMTask, pvParameters ) {
	portTickType xLastWakeTime;

	#if(configDEBUG_MESSAGES == 1)
		printf("xTask SLAM started.\r\n");
	#endif

	xLastWakeTime = xTaskGetTickCount();

	slam_init(&slam, 1000, 100, 0, 0, (XV11_t *) &xv11, NULL, NULL);
	//slam_laserRayToMap(&slam, 85, 33, 220, 33, 210, 33, 255, 100);

	while(xv11_state(XV11_GETSTATE) != XV11_ON);

	//slam_map_update(&slam, 100, 50);

	for(;;)
	{
		slam_map_update(&slam, 50, 50);

		//ts_iterative_map_building(&sensor_data, &state);
		vTaskDelayUntil( &xLastWakeTime, ( 200 / portTICK_RATE_MS ) );
	}
}


void slam_LCD_DispMap(int16_t x0, int16_t y0, slam_t *slam)
{
	u8 mapval = 0;

	LCD_SetArea(x0,
				y0,
				x0 + (MAP_SIZE_X_MM / MAP_RESOLUTION_MM) - 1,
				y0 + (MAP_SIZE_Y_MM / MAP_RESOLUTION_MM) - 1);

	LCD_WriteCommand(CMD_WR_MEMSTART);

	Clr_Cs;

	for (u16 y = 0; y < (MAP_SIZE_Y_MM / MAP_RESOLUTION_MM); y++)
		for (u16 x = 0; x < (MAP_SIZE_X_MM / MAP_RESOLUTION_MM); x++)
		{
			mapval = slam->map.px[x][y][slam->robot_pos.coord.z];
			LCD_WriteData(0xffff - RGB565CONVERT(mapval, mapval, mapval));
		}

	Set_Cs;
}
