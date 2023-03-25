#include "videoprocess.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
 
bool video_sharpness_vaapi(http_conn_t *conn, char *video_name, char *output_name) {
	/*
	  FILE *fp = fopen(video_name, "w+b");
	  fseek(fp, 0L, SEEK_END);
	  unsigned int file_size = ftell(fp);
	  fseek(fp, 0L, SEEK_SET);
	*/

	char command[4096] = {0};
	
	
	//snprintf(command, sizeof(command), "ffmpeg %s %s %s -i %s -vf '%s,%s' -c:v h264_vaapi %s", "-hwaccel vaapi", "-hwaccel_output_format vaapi", "-vaapi_device /dev/dri/renderD128", video_name, "hwupload", "sharpness_vaapi", output_name);
	output_name[strlen(output_name)-1] = '\0';
	snprintf(command, sizeof(command), "ffmpeg -i '%s' -c:v copy '%s'", video_name, output_name);

	
	return system(command) == 0;
	
}
