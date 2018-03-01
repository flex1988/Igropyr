#include "membuf.h"
#include <uv.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <memory.h>

#define IGROPYR_VERSION "0.1.0"


uv_tcp_t    _server;
uv_tcp_t    _client;
const char* _doc_root_path = NULL;




static void igropyr_on_connection(uv_stream_t* server, int status);


int igropyr_init(const char* doc_root_path, const char* ip, int port)
{
	struct sockaddr_in addr;
	uv_ip4_addr((ip && ip[0]) ? ip : "0.0.0.0", port, &addr);
	_doc_root_path = doc_root_path;

	uv_tcp_init(uv_default_loop(), &_server);
	uv_tcp_bind(&_server, (const struct sockaddr*) &addr, 0);
	uv_listen((uv_stream_t*)&_server, 8, igropyr_on_connection);

	return uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}

static void after_uv_close_client(uv_handle_t* client) 
{
	membuf_uninit((membuf_t*)client->data); 
	free(client->data); 
	free(client);
}

static void after_uv_write(uv_write_t* w, int status) 
{
	//	     !!!  !!!  !!!
	//
	//这里可能会有内存泄漏问题 如果这里释放 scheme会报内存被错误释放
	//if(w->data)
	//	free(w->data); //copyed data
	//

	uv_close((uv_handle_t*)w->handle, after_uv_close_client);
	free(w);
}

static void igropyr_close_client(uv_stream_t* client) 
{
	uv_close((uv_handle_t*)client, after_uv_close_client);
}


static void write_uv_data(uv_stream_t* client, const void* data, unsigned int len, int need_copy_data, int need_free_data) 
{
	uv_buf_t buf;
	uv_write_t* w;
	void* newdata  = (void*)data;

	if(data == NULL || len == 0) 
	{
		return;
	}
	if(len ==(unsigned int)-1)
	{
		len = strlen((char*)data);
	}
	if(need_copy_data) 
	{
		newdata = malloc(len);
		memcpy(newdata, data, len);
	}

	buf = uv_buf_init((char*)newdata, len);
	w = (uv_write_t*)malloc(sizeof(uv_write_t));
	w->data = (need_copy_data || need_free_data) ? newdata : NULL;
	uv_write(w, client, &buf, 1, after_uv_write); 
}

char* format_http_response(const char* status, const char* content_type, const void* content, int content_length, int* respone_size) 
{
	int totalsize, header_size;
	char* respone;

	if(content_length < 0)
	{
		content_length = content ? strlen((char*)content) : 0;
	}
		
	totalsize = strlen(status) + strlen(content_type) + content_length + 128;
	respone = (char*) malloc(totalsize);
	header_size = sprintf(respone,  "HTTP/1.1 %s\r\n"
									"Content-Type:%s;charset=utf-8\r\n"
									"Content-Length:%d\r\n\r\n",
						status, content_type, content_length);
	assert(header_size > 0);
	if(content) 
	{
		memcpy(respone + header_size, content, content_length);
	}
	if(respone_size)
	{
		*respone_size = header_size + content_length;
	}
		
	return respone;
}

static char* handle_status_code(int code)
{
	switch(code)
	{
		case 200: return("200 OK");break;
		case 301: return("301 Moved Permanently");break;
		case 302: return("302 Found");break;
		case 404: return("404 Not Found");break;
		case 500: return("500 Internal Server Error");break;
		case 502: return("502 Bad Gateway");break;
		case 504: return("504 Gateway Timeout");break;
		case 100: return("100 Continue");break;
   		case 101: return("101 Switching Protocals");break;
   		case 201: return("201 Created");break;
   		case 202: return("202 Accepted");break;
   		case 203: return("203 Non-authoritative Information");break;
   		case 204: return("204 No Content");break;
   		case 205: return("205 Reset Content");break;
   		case 206: return("206 Partial Content");break;
   		case 300: return("300 Multiple Choices");break;
   		case 303: return("303 See Other");break;
   		case 304: return("304 Not Modified");break;
   		case 305: return("305 Use Proxy");break;
   		case 306: return("306 Unused");break;
   		case 307: return("307 Temporary Redirect");break;
   		case 400: return("400 Bad Request");break;
   		case 401: return("401 Unauthorized");break;
   		case 402: return("402 Payment Required");break;
   		case 403: return("403 Forbidden");break;   		
   		case 405: return("405 Method Not Allowed");break;
   		case 406: return("406 Not Acceptable");break;
   		case 407: return("407 Proxy Authentication Required");break;
   		case 408: return("408 Request Timeout");break;
   		case 409: return("409 Conflict");break;
   		case 410: return("410 Gone");break;
   		case 411: return("411 Length Required");break;
		case 412: return("412 Precondition Failed");break;
   		case 413: return("413 Request Entity Too Large");break;
   		case 414: return("414 Request-url Too Long");break;
   		case 415: return("415 Unsupported Media Type");break;
   		case 416: return("416 Requested Range Not Satisfiable");break;
		case 417: return("417 Expectation Failed");break;   		
   		case 501: return("501 Not Implemented");break;
   		case 503: return("503 Service Unavailable");break;  		
   		case 505: return("505 HTTP Version Not Supported");break;
		default: return("200 OK");break;
	}
}
   	


char* igropyr_response(const int code, const char* content_type, const char* content) 
{
	char* status = handle_status_code(code);
	return format_http_response(status, content_type, content, -1, NULL);
}



#if defined(WIN32)
	#define snprintf _snprintf
#endif

static void handle_404(uv_stream_t* client, const char* path_info) 
{
	char* respone;
	char buffer[1024];
	snprintf(buffer, sizeof(buffer), "<html><head><title>404 Not Found</title></head><body bgcolor='white'><center><h1>404 Not Found</h1></center><hr><center>Igropyr/%s</center><p>%s</p></body></html>", IGROPYR_VERSION, path_info);
	respone = format_http_response("404 Not Found", "text/html", buffer, -1, NULL);
	write_uv_data(client, respone, -1, 0, 1);
}


static void send_file(uv_stream_t* client, const char* content_type, const char* file, const char* path_info) 
{
	int file_size, read_bytes, respone_size;
	unsigned char *file_data, *respone;
	FILE* fp = fopen(file, "rb");
	if(fp) 
	{
		fseek(fp, 0, SEEK_END);
		file_size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		file_data = (unsigned char*) malloc(file_size);
		read_bytes = fread(file_data, 1, file_size, fp);
		assert(read_bytes == file_size);
		fclose(fp);

		respone_size = 0;
		respone = format_http_response("200 OK", content_type, file_data, file_size, &respone_size);
		free(file_data);
		write_uv_data(client, respone, respone_size, 0, 1);
	} 
	else 
	{
		handle_404(client, path_info);
	}
}


static const char* handle_content_type(const char* postfix) 
{
	if(strcmp(postfix, "html") == 0 || strcmp(postfix, "htm") == 0)
		return "text/html";
	else if(strcmp(postfix, "js") == 0)
		return "text/javascript";
	else if(strcmp(postfix, "css") == 0)
		return "text/css";
	else if(strcmp(postfix, "jpeg") == 0 || strcmp(postfix, "jpg") == 0)
		return "image/jpeg";
	else if(strcmp(postfix, "png") == 0)
		return "image/png";
	else if(strcmp(postfix, "gif") == 0)
		return "image/gif";
	else if(strcmp(postfix, "txt") == 0)
		return "text/plain";
	else
		return "application/octet-stream";
}


typedef char* (*igropyr_res)(char* request_header, char* path_info, char* query_stirng); 

igropyr_res _res;

int igropyr_res_init(igropyr_res _response)
{
	_res = _response;
}


static void igropyr_on_request_get(const char* request_header, uv_stream_t* client, const char* path_info, const char* query_stirng) 
{
	char* postfix = strrchr(path_info, '.');

	if(postfix) 
	{
		postfix++;
		if(_doc_root_path) 
		{
			char file[1024];
			snprintf(file, sizeof(file), "%s%s", _doc_root_path, path_info);
			send_file(client, handle_content_type(postfix), file, path_info);
			return;
		}
		else
		{
			handle_404(client, path_info);
			return;
		}
	}
	else
	{
		char* respone = _res(request_header, path_info, query_stirng);
		write_uv_data(client, respone, -1, 0, 1);
	}
}

static void on_uv_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
	*buf = uv_buf_init(malloc(suggested_size), suggested_size);
}

static void on_uv_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
	if(nread > 0) {
		char* crln2;
		membuf_t* membuf = (membuf_t*) client->data; //see igropyr_on_connection()
		assert(membuf);
		membuf_append_data(membuf, buf->base, nread);
		if((crln2 = strstr((const char*)membuf->data, "\r\n\r\n")) != NULL) {
			const char* request_header = membuf->data;
			*crln2 = '\0';
			if(request_header[0]=='G' && request_header[1]=='E' && request_header[2]=='T') {
				char *query_stirng, *end;
				const char* path_info = request_header + 3;
				while(isspace(*path_info)) path_info++;
				end = strchr(path_info, ' ');
				if(end) *end = '\0';
				query_stirng = strchr(path_info, '?'); 
				if(query_stirng) {
					*query_stirng = '\0';
					query_stirng++;
				}
				igropyr_on_request_get(request_header, client, path_info, query_stirng);

			} else {
				igropyr_close_client(client);
			}
		}
	}
	else if(nread == -1) 
	{
		igropyr_close_client(client);
	}

	if(buf->base)
	{
		free(buf->base);
	}
		
}

static void igropyr_on_connection(uv_stream_t* server, int status) 
{
	assert(server == (uv_stream_t*)&_server);
	if(status == 0) 
	{
		uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
		client->data = malloc(sizeof(membuf_t));
		membuf_init((membuf_t*)client->data, 128);
		uv_tcp_init(uv_default_loop(), client);
		uv_accept((uv_stream_t*)&_server, (uv_stream_t*)client);
		uv_read_start((uv_stream_t*)client, on_uv_alloc, on_uv_read);
	}
}

int par(char* router_info, char* path_info)
{
	char* p1 = router_info + 1;
	char* p2 = path_info + 1;

	for(;;)
	{
		if(*p1 != '*')
		{
			if(*p1 != *p2)
			{
				p1 = NULL;
				p2 = NULL;
				return 0;
				break;
			}
			else
			{
				if(*p1 == '\0')
				{
				p1 = NULL;
				p2 = NULL;
				return 1;
				break;
				}
				else
				{
				p1++;
				p2++;
				}
			}
		}
		else
		{
			p1++;
			if(*p1 == '\0')
			{
				p1 = NULL;
				p2 = NULL;
				return 1;
				break;
			}
			else
			{
				for(;*p2 != '/'; p2++)
	 			{
	 			}
			}
		}
	}

}




