#ifndef __REQUEST_H__
#define __REQUEST_H__

void request_handle(int fd);
void request_serve_dynamic(int fd, char *filename, char *cgiargs);
void request_parse_headers(int fd, char *headers, size_t headers_size);
int request_parse_body(int fd, char *headers, char *body, size_t body_size);

#endif // __REQUEST_H__