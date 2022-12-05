#pragma once
#include <stdbool.h>
#include "serverd.h" 
bool video_sharpness_vaapi(http_conn_t *conn, char *video_name, char *output_name);
