#pragma once

#include "include/hplatform.h"
#include "include/hv.h" 
#include "include/hloop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "io.h"

static const char* host = "0.0.0.0";
static int port = 9000;
static int thread_num = 4;
static hloop_t*  accept_loop = NULL;
static hloop_t** worker_loops = NULL;

#define HTTP_KEEPALIVE_TIMEOUT  60000 // ms
#define HTTP_MAX_URL_LENGTH     256
#define HTTP_MAX_HEAD_LENGTH    4096

#define HTML_TAG_BEGIN  "<html><body><center><h1>"
#define HTML_TAG_END    "</h1></center></body></html>"

// status_message
#define HTTP_OK         "OK"
#define NOT_FOUND       "Not Found"
#define NOT_IMPLEMENTED "Not Implemented"

// Content-Type
#define TEXT_PLAIN      "text/plain"
#define TEXT_HTML       "text/html"

typedef enum {
    s_begin,
    s_first_line,
    s_request_line = s_first_line,
    s_status_line = s_first_line,
    s_head,
    s_head_end,
    s_body,
    s_end
} http_state_e;

typedef struct {
    // first line
    int             major_version;
    int             minor_version;
    union {
        // request line
        struct {
            char method[32];
            char path[HTTP_MAX_URL_LENGTH];
        };
        // status line
        struct {
            int  status_code;
            char status_message[64];
        };
    };
    // headers
    char        host[64];
    int         content_length;
    char        content_type[64];
    unsigned    keepalive:  1;
//  char        head[HTTP_MAX_HEAD_LENGTH];
//  int         head_len;
    // body
    char*       body;
    int         body_len; // body_len = content_length
} http_msg_t;

typedef struct Video_info{
	char        video_process_done[3];
	char        video_name_original[2048];
	char        video_name_final[2048];
	FILE *      original;
}Video_info;

typedef struct http_conn_t{
    hio_t*          io;
    http_state_e    state;
    http_msg_t      request;
    http_msg_t      response;
	Video_info      video_info;
	bool body_is_video;
} http_conn_t;

bool change_video_name(char*name);
