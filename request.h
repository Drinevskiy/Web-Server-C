#ifndef __REQUEST_H__
#define __REQUEST_H__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <uuid/uuid.h>
#include <errno.h>
#include <ctype.h>

// Structure for POST parameters
typedef struct {
    char *key;
    char *value;
} post_param_t;

// Structure for file parts in multipart/form-data
typedef struct {
    char *name;
    char *filename;
    char *content_type;
    char *data;
    size_t data_size;
} file_part_t;

// Function declarations
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void request_read_headers(int fd);
int request_parse_uri(char *uri, char *filename, char *cgiargs);
void request_get_filetype(char *filename, char *filetype);
void request_serve_static(int fd, char *filename, int filesize);
void url_decode(char *dst, const char *src);
post_param_t* parse_post_data(const char *data, int *num_params);
void free_post_params(post_param_t *params, int num_params);
void request_handle_post(int fd, char* headers, char *body, int body_len);
int get_content_length(char *headers);
void request_parse_headers(int fd, char *headers, size_t headers_size);
int request_parse_body(int fd, char *headers, char *body, size_t body_size);
void create_upload_dir();
void generate_filename(char *buffer, const char *ext);
void serve_upload_form(int fd);
char* get_boundary(char *content_type);
void handle_multipart_upload(int fd, char *body, size_t body_size, char *boundary);
void request_handle(int fd);
#endif // __REQUEST_H__