/*
 * tinyhttpd tiny http server
 *
 * @build    make examples
 *
 * @server   bin/tinyhttpd 8000
 *
 * @client   bin/curl -v http://127.0.0.1:8000/
 *           bin/curl -v http://127.0.0.1:8000/ping
 *           bin/curl -v http://127.0.0.1:8000/echo -d "hello,world!"
 *
 * @webbench bin/wrk  http://127.0.0.1:8000/ping
 *
 */

#include "include/hloop.h"
#include "include/hssl.h"
#include "serverd.h"
#include "videoprocess.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * workflow:
 * hloop_new -> hloop_create_tcp_server -> hloop_run ->
 * on_accept -> HV_ALLOC(http_conn_t) -> hio_readline ->
 * on_recv -> parse_http_request_line -> hio_readline ->
 * on_recv -> parse_http_head -> ...  -> hio_readbytes(content_length) ->
 * on_recv -> on_request -> http_reply-> hio_write -> hio_close ->
 * on_close -> HV_FREE(http_conn_t)
 *
 */

static char s_date[32] = {0};
static void update_date(htimer_t *timer) {
  uint64_t now = hloop_now(hevent_loop(timer));
  gmtime_fmt(now, s_date);
}

static int http_response_dump(http_msg_t *msg, char *buf, int len,
                              char *file_name) {
  int offset = 0;
  // status line
  offset += snprintf(buf + offset, len - offset, "HTTP/%d.%d %d %s\r\n",
                     msg->major_version, msg->minor_version, msg->status_code,
                     msg->status_message);
  offset += snprintf(buf + offset, len - offset, "%s\r\n",
                     "Access-Control-Allow-Origin: *");

  // headers
  if (file_name != NULL) {
    offset += snprintf(buf + offset, len - offset,
                       "Content-Disposition: attachment; filename=\"%s\"\r\n",
                       file_name);
  }
  offset += snprintf(buf + offset, len - offset, "Server: libhv/%s\r\n",
                     hv_version());
  offset += snprintf(buf + offset, len - offset, "Connection: %s\r\n",
                     msg->keepalive ? "keep-alive" : "close");
  if (msg->content_length > 0) {
    offset += snprintf(buf + offset, len - offset, "Content-Length: %d\r\n",
                       msg->content_length);
  }
  if (*msg->content_type) {
    offset += snprintf(buf + offset, len - offset, "Content-Type: %s\r\n",
                       msg->content_type);
  }
  if (*s_date) {
    offset += snprintf(buf + offset, len - offset, "Date: %s\r\n", s_date);
  }
  // TODO: Add your headers
  offset += snprintf(buf + offset, len - offset, "\r\n");
  // body
  if (msg->body && msg->content_length > 0) {
    memcpy(buf + offset, msg->body, msg->body_len);
    offset += msg->body_len;
  }
  return offset;
}

static int http_reply(http_conn_t *conn, int status_code,
                      const char *status_message, const char *content_type,
                      const char *body, int body_len, char *file_name) {
  http_msg_t *req = &conn->request;
  http_msg_t *resp = &conn->response;
  resp->major_version = req->major_version;
  resp->minor_version = req->minor_version;
  resp->status_code = status_code;
  if (status_message)
    strncpy(resp->status_message, status_message,
            sizeof(req->status_message) - 1);
  if (content_type)
    strncpy(resp->content_type, content_type, sizeof(req->content_type) - 1);
  resp->keepalive = req->keepalive;
  if (body) {
    if (body_len <= 0)
      body_len = strlen(body);
    resp->content_length = body_len;
    resp->body = (char *)body;
  }
  char *buf = NULL;
  STACK_OR_HEAP_ALLOC(buf, HTTP_MAX_HEAD_LENGTH + resp->content_length,
                      HTTP_MAX_HEAD_LENGTH + 1024);
  int msglen = 0;
  msglen = http_response_dump(
      resp, buf, HTTP_MAX_HEAD_LENGTH + resp->content_length, file_name);
  int nwrite = hio_write(conn->io, buf, msglen);
  STACK_OR_HEAP_FREE(buf);
  return nwrite < 0 ? nwrite : msglen;
}

static int http_serve_file(http_conn_t *conn, char *file_path_string) {
  http_msg_t *req = &conn->request;
  http_msg_t *resp = &conn->response;
  // GET / HTTP/1.1\r\n
  const char *filepath = NULL;
  if (file_path_string == NULL) {
    const char *filepath = req->path + 1;
    // homepage
    if (*filepath == '\0') {
      filepath = "index.html";
    }
	} else if(file_path_string != NULL){
    filepath = file_path_string;
  }
  FILE *fp = fopen(filepath, "rb");
  if (!fp) {
    http_reply(conn, 404, NOT_FOUND, TEXT_HTML,
               HTML_TAG_BEGIN NOT_FOUND HTML_TAG_END, 0, NULL);
    return 404;
  }
  // send head
  size_t filesize = hv_filesize(filepath);
  resp->content_length = filesize;

  void *buf = malloc(filesize);
  memset(buf, 0, filesize);

  const char *suffix = hv_suffixname(filepath);
  const char *content_type = NULL;
  if (strcmp(suffix, "html") == 0) {
    content_type = TEXT_HTML;
  } else if (strcmp(suffix, "mp4")) {
    // TODO: set content_type by suffix
    content_type = "video/mp4";
  }
	int nwrite = 0;
	if(strcmp(suffix, "html") == 0)
		nwrite = http_reply(conn, 200, "OK", content_type, NULL, 0, NULL);
	else
		nwrite = http_reply(conn, 200, "OK", content_type, NULL, 0, file_path_string);
	if (nwrite < 0)
	  return nwrite; // disconnected
  // send file
  int nread = 0;
  while (1) {
    nread = fread(buf, 1, sizeof(buf), fp);
    if (nread <= 0)
      break;
    nwrite = hio_write(conn->io, buf, nread);
    if (nwrite < 0)
      return nwrite; // disconnected
    if (nwrite == 0) {
      // send too fast or peer recv too slow
      // WARN: too large file should control sending rate, just delay a while in
      // the demo!
      hv_delay(10);
    }
  }
  fclose(fp);
  return 200;
}

static bool parse_http_request_line(http_conn_t *conn, char *buf, int len) {
  // GET / HTTP/1.1
  http_msg_t *req = &conn->request;
  sscanf(buf, "%s %s HTTP/%d.%d", req->method, req->path, &req->major_version,
         &req->minor_version);
  if (req->major_version != 1)
    return false;
  if (req->minor_version == 1)
    req->keepalive = 1;
  // printf("%s %s HTTP/%d.%d\r\n", req->method, req->path, req->major_version,
  // req->minor_version);
  return true;
}

static bool parse_http_head(http_conn_t *conn, char *buf, int len) {
  http_msg_t *req = &conn->request;
  // Content-Type: text/html
  const char *key = buf;
  const char *val = buf;
  char *delim = strchr(buf, ':');
  if (!delim)
    return false;
  *delim = '\0';
  val = delim + 1;
  // trim space
  while (*val == ' ')
    ++val;
  // printf("%s: %s\r\n", key, val);
  if (stricmp(key, "Content-Length") == 0) {
    req->content_length = atoi(val);
  } else if (stricmp(key, "Content-Type") == 0) {
    strncpy(req->content_type, val, sizeof(req->content_type) - 1);
  } else if (stricmp(key, "Connection") == 0) {
    if (stricmp(val, "close") == 0) {
      req->keepalive = 0;
    }
  } else {
    // TODO: save other head
  }
  return true;
}

static int on_request(http_conn_t *conn) {
  http_msg_t *req = &conn->request;

	printf("req->path = %s\n", req->path);
	
  // TODO: router
  if (strcmp(req->method, "GET") == 0) {
    // GET /ping HTTP/1.1\r\n
    if (strcmp(req->path, "/ping") == 0) {
      http_reply(conn, 200, "OK", TEXT_PLAIN, "pong", 4, NULL);
	  hio_write(conn->io, "pong", 4);
      return 200;
    } else if (strcmp(req->path, "/status") == 0) {
      // TODO: Add handler for your path
      char status[16] = "y";
      http_reply(conn, 200, "OK", "application/text", status, strlen(status),
                 NULL);
      // http_reply(conn, 200, "OK", "application/text", status,
      // strlen(status));
      hio_write(conn->io, status, strlen(status));
    } else if(strcmp(req->path, "/index.html")){
	  // TODO: handle other method
	  http_serve_file(conn, "index.html");
		

		printf("http_reply\n");
		
	}else if (strcmp(req->path, "/")) {
			http_serve_file(conn, "index.html");
			
	}
    // TODO: FIX THIS
    // return http_serve_file(conn, NULL);
  } else if (strcmp(req->method, "POST") == 0) {
    // POST /echo HTTP/1.1\r\n
    if (strcmp(req->path, "/echo") == 0) {
      //		FILE *fp = fopen("./test.mp4", "wb");
      //		fwrite(conn->request.body, 1, req->body_len, fp);
      // printf("body = %s\n",req->body);

      // printf("hio_write\n");
      // hio_write_upstream(conn->io, req->body, req->body_len);

      return 200;
    } else if (strcmp(req->path, "/video_sharpness") == 0) {
      // TODO: Add handler for your path
      strcpy(conn->video_info.video_name_final,
             conn->video_info.video_name_original);
      change_video_name(conn->video_info.video_name_final);

		for(unsigned int i=strlen(conn->video_info.video_name_final)-1; i>=0; i--){
			if(conn->video_info.video_name_final[i] == '.'){
				conn->video_info.video_name_final[i+1] ='m';
				conn->video_info.video_name_final[i+2] ='p';
				conn->video_info.video_name_final[i+3] ='4';	
				break;											
			}
			conn->video_info.video_name_final[i] =' ';
							
		}
		
      if(video_sharpness_vaapi(conn, conn->video_info.video_name_original,
							   conn->video_info.video_name_final)){
		  http_serve_file(conn, conn->video_info.video_name_final);
	  }else {
		  unsigned int message_len = strlen(HTML_TAG_BEGIN) + strlen(NOT_FOUND) + strlen(HTML_TAG_END); 
		  http_reply(conn, 404, NOT_FOUND, TEXT_HTML,
		     HTML_TAG_BEGIN NOT_FOUND HTML_TAG_END, message_len, NULL);
		  hio_write(conn->io, HTML_TAG_BEGIN NOT_FOUND HTML_TAG_END, message_len);
		  
	  }
      
    
      // http_reply(conn, 200, "OK", req->content_type, req->body,
      // req->content_length);
    }
	} else{
	  // TODO: handle other method
    }
  http_reply(conn, 501, NOT_IMPLEMENTED, TEXT_HTML,
             HTML_TAG_BEGIN NOT_IMPLEMENTED HTML_TAG_END, 0, NULL);
  return 501;
}

static void on_close(hio_t *io) {
  // printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
  http_conn_t *conn = (http_conn_t *)hevent_userdata(io);

	if(conn->video_info.original != NULL){
		fclose(conn->video_info.original);
	}
  remove(conn->video_info.video_name_original);
  remove(conn->video_info.video_name_final);
 
	
  if (conn) {
    HV_FREE(conn);
    hevent_set_userdata(io, NULL);
  }
}

unsigned short start_body_if = 0;
unsigned int buffer_len = 0;
bool readline_fallback = false;
void *buffer = NULL;
// bool content_header_end = false;

void on_recv(hio_t *io, void *buf, int readbytes) {
  char *str = (char *)buf;
  // printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
  // printf("%.*s", readbytes, str);
  http_conn_t *conn = (http_conn_t *)hevent_userdata(io);
  http_msg_t *req = &conn->request;

  unsigned int line_char_count = 0;

  switch (conn->state) {
  case s_begin:
    printf("s_begin");
    conn->state = s_first_line;
  case s_first_line:
    printf("s_first_line\n");
    if (readbytes < 2) {
      fprintf(stderr, "Not match \r\n!");
      hio_close(io);
      return;
    }
    str[readbytes - 2] = '\0';
    if (parse_http_request_line(conn, str, readbytes - 2) == false) {
      fprintf(stderr, "Failed to parse http request line:\n%s\n", str);
      hio_close(io);
      return;
    }
    // start read head
    conn->state = s_head;
    hio_readline(io);
    break;
  case s_head:
    printf("s_head\n");

    //	printf("content lenght = %d", req->content_length);
    if (readbytes < 2) {
      fprintf(stderr, "Not match \r\n!");
      hio_close(io);
      return;
    }
    if (readbytes == 2 && str[0] == '\r' && str[1] == '\n') {
      conn->state = s_head_end;
    } else {
      str[readbytes - 2] = '\0';
      if (parse_http_head(conn, str, readbytes - 2) == false) {
        fprintf(stderr, "Failed to parse http head:\n%s\n", str);
        hio_close(io);
        return;
      }
      hio_readline(io);
      break;
    }
  case s_head_end:
    printf("s_head_end\n");
    if (req->content_length == 0) {
      conn->state = s_end;
      printf("content lenght = %d\n", req->content_length);
      goto s_end;
    } else {
      // start read body
      conn->state = s_body;
      // WARN: too large content_length should read multiple times!
      // hv_delay(1000);
      // hio_readbytes(io, req->content_length);
      hio_read(io);
      // goto s_body;
      break;
    }
  case s_body:

    printf("s_body\n");

    if ((readbytes == 2 && str[0] == '\r' && str[1] == '\n')) {
      printf("str == %s", str);
      start_body_if++;
      hio_read(io);
    }

    printf("%u\n", start_body_if);

    req->body = str;
    req->body_len += readbytes;

    // hv_delay(1);

    if (buffer == NULL) {
      buffer = malloc(req->content_length);
      memset(buffer, 0, req->content_length);
      buffer_len = 0;
    }

    // if(start_body_if){
    memcpy(buffer + buffer_len, buf, readbytes);
    buffer_len += readbytes;
    /*
printf("FILE = %p, buf = %p, readbytes = %d, body_len = %d, content_length "
       "= %d, buffer_len = %u\n",
       conn->video_info.final, buf, readbytes, req->body_len,
       req->content_length, buffer_len);
    */
    if (req->body_len == req->content_length) {
      // hio_read_remain(io);
      conn->state = s_end;
      // buffer = realloc(buffer, buffer_len);
      printf("writing!!!!!!\n");
      // buffer = realloc(buffer, buffer_len);

      printf("bufferlen = %u\n", buffer_len);

      unsigned int vid_name_index = 0;
      unsigned short set_break = 0;
      for (unsigned int i = 0; i < buffer_len; i++) {
        if (strncmp(((char *)buffer + i), "name=", 5) == 0) {
          if (strncmp(((char *)buffer + i + 5), "\"video_file\"", 12) == 0) {
            conn->body_is_video = true;
          }
        }
        if (strncmp(((char *)buffer + i), "filename=", 9) == 0) {
          for (unsigned int j = 0; j < 1024; j++) {
            if (((char *)buffer + i + j + 10)[0] == '\"') {
              break;
            }

            conn->video_info.video_name_original[vid_name_index++] =
                ((char *)buffer + i + j + 10)[0];
          }
        }

        printf("conn->body_is_video = %s, video_name_original = %s\n",
               conn->body_is_video ? "true" : "false",
               conn->video_info.video_name_original);

        if (((char *)buffer)[i] == '\r' && ((char *)buffer)[i + 1] == '\n' &&
            ((char *)buffer)[i + 2] == '\r' &&
            ((char *)buffer)[i + 3] == '\n') {
          memcpy(buffer, &((char *)buffer)[i + 4], buffer_len - i - 4);
          buffer_len = buffer_len - i - 4;

          printf("bufferlen = %u\n", buffer_len);
          break;
        }
      }
      unsigned int zeroDzeroA_count = 0;
      unsigned int deleted_bytes_count = 0;
      for (unsigned int i = buffer_len; i >= 0; i--) {
        deleted_bytes_count++;
        if (((char *)buffer)[i - 1] == '\r' && ((char *)buffer)[i] == '\n') {
          zeroDzeroA_count++;
        }
        if (zeroDzeroA_count == 6) {
          buffer = realloc(buffer, buffer_len - deleted_bytes_count);
          printf("bufferlen = %u\n", buffer_len);
          buffer_len = buffer_len - deleted_bytes_count;

          printf("bufferlen = %u\n", buffer_len);
          break;
        }
      }
	  
      printf("bufferlen = %u\n", buffer_len);

      change_video_name(conn->video_info.video_name_original);
      // file doesn't exist
      conn->video_info.original =
      fopen(conn->video_info.video_name_original, "wb");
      fwrite(buffer, buffer_len, 2, conn->video_info.original);
      fflush(conn->video_info.original);
      ftruncate(fileno(conn->video_info.original), buffer_len);
      free(buffer);
      buffer = NULL;

      goto s_end;
      // hio_read(io);
    }

    // WARN: too large content_length should be handled by streaming!

    //}else{
    hio_read(io);
    //	readline_fallback = false;

    //}
    hio_read(io);

    break;

  case s_end:
  s_end:
    printf("s_end\n");
    // received complete request
    on_request(conn);
    if (hio_is_closed(io))
      return;
    if (req->keepalive) {
      // Connection: keep-alive\r\n
      // reset and receive next request
      memset(&conn->request, 0, sizeof(http_msg_t));
      memset(&conn->response, 0, sizeof(http_msg_t));
      conn->state = s_first_line;
      hio_readline(io);
    } else {
      // Connection: close\r\n
      hio_close(io);
    }
    break;
  default:
    break;
  }
}

static void new_conn_event(hevent_t *ev) {
  hloop_t *loop = ev->loop;
  hio_t *io = (hio_t *)hevent_userdata(ev);
  hio_attach(loop, io);
  start_body_if = false;
	printf("new_conn_event\n");
  /*
  char localaddrstr[SOCKADDR_STRLEN] = {0};
  char peeraddrstr[SOCKADDR_STRLEN] = {0};
  printf("tid=%ld connfd=%d [%s] <= [%s]\n",
          (long)hv_gettid(),
          (int)hio_fd(io),
          SOCKADDR_STR(hio_localaddr(io), localaddrstr),
          SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
  */

  hio_setcb_close(io, on_close);
  hio_setcb_read(io, on_recv);

  hio_set_keepalive_timeout(io, HTTP_KEEPALIVE_TIMEOUT);

  http_conn_t *conn = NULL;
  HV_ALLOC_SIZEOF(conn);
  conn->io = io;
  hevent_set_userdata(io, conn);
  conn->video_info.video_process_done[0] = 'n';
  conn->body_is_video = false;
  // start read first line
  conn->state = s_first_line;
  hio_readline(io);
}

static hloop_t *get_next_loop() {
  static int s_cur_index = 0;
  if (s_cur_index == thread_num) {
    s_cur_index = 0;
  }
  return worker_loops[s_cur_index++];
}

static void on_accept(hio_t *io) {
  hio_detach(io);

	printf("on_accept\n");
  hloop_t *worker_loop = get_next_loop();
  hevent_t ev;
  memset(&ev, 0, sizeof(ev));
  ev.loop = worker_loop;
  ev.cb = new_conn_event;
  ev.userdata = io;
  hloop_post_event(worker_loop, &ev);
}

static HTHREAD_ROUTINE(worker_thread) {
  hloop_t *loop = (hloop_t *)userdata;
  hloop_run(loop);
  return 0;
}

static HTHREAD_ROUTINE(accept_thread) {
  hloop_t *loop = (hloop_t *)userdata;
	hio_t *listenio = hloop_create_tcp_server(loop, host, port, on_accept);

	//hssl_ctx_opt_t *ssl = malloc(sizeof(hssl_ctx_opt_t));
	//memset(ssl, 0, sizeof(hssl_ctx_opt_t));
	//ssl->crt_file = "./cert/server.crt\0";
	//ssl->key_file = "./cert/server.key\0";
	//ssl->ca_file = "./cert/cacert.pem\0";
	
	//hio_new_ssl_ctx(listenio, ssl);
	

	if (listenio == NULL) {
		exit(1);
	}
  printf("tinyhttpd listening on %s:%d, listenfd=%d, thread_num=%d\n", host,
         port, hio_fd(listenio), thread_num);
  // NOTE: add timer to update date every 1s
  htimer_add(loop, update_date, 1000, INFINITE);
  hloop_run(loop);
  return 0;
}

int main(int argc, char **argv) {

  if (argc < 2) {
    printf("Usage: %s port [thread_num]\n", argv[0]);
    return -10;
  }
  port = atoi(argv[1]);
  if (argc > 2) {
    thread_num = atoi(argv[2]);
  } else {
    thread_num = get_ncpu();
  }
  if (thread_num == 0)
    thread_num = 1;

  worker_loops = (hloop_t **)malloc(sizeof(hloop_t *) * thread_num);
  for (int i = 0; i < thread_num; ++i) {
    worker_loops[i] = hloop_new(HLOOP_FLAG_AUTO_FREE);
    hthread_create(worker_thread, worker_loops[i]);
  }

  accept_loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
  accept_thread(accept_loop);
  return 0;
}
