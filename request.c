#include "io_helper.h"
#include "request.h"

//
// Some of this code stolen from Bryant/O'Halloran
// Hopefully this is not a problem ... :)
//

#define MAXBUF (8192)

void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];
    
    // Create the body of error message first (have to know its length for header)
    sprintf(body, ""
	    "<!doctype html>\r\n"
	    "<head>\r\n"
	    "  <title>WebServer C</title>\r\n"
	    "</head>\r\n"
	    "<body>\r\n"
	    "  <h2>%s: %s</h2>\r\n" 
	    "  <p>%s: %s</p>\r\n"
	    "</body>\r\n"
	    "</html>\r\n", errnum, shortmsg, longmsg, cause);
    
    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));
    
    // Write out the body last
    write_or_die(fd, body, strlen(body));
}

//
// Reads and discards everything up to an empty text line
//
void request_read_headers(int fd) {
    char buf[MAXBUF];
    
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
	    readline_or_die(fd, buf, MAXBUF);
    }
    return;
}

//
// Return 1 if static, 0 if dynamic content
// Calculates filename (and cgiargs, for dynamic) from uri
//
int request_parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    
    if (!strstr(uri, "cgi")) { 
        // static
        strcpy(cgiargs, "");
        sprintf(filename, ".%s", uri);
        if (uri[strlen(uri)-1] == '/') {
            strcat(filename, "index.html");
        }
        return 1;
    } else { 
        // dynamic
        ptr = index(uri, '?');
        if (ptr) {
            strcpy(cgiargs, ptr+1);
            *ptr = '\0';
        } else {
            strcpy(cgiargs, "");
        }
        sprintf(filename, ".%s", uri);
        return 0;
    }
}

//
// Fills in the filetype given the filename
//
void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".png")) strcpy(filetype, "image/png");
    else if (strstr(filename, ".css")) strcpy(filetype, "text/css");
    else strcpy(filetype, "text/plain");
}

void request_serve_dynamic(int fd, char *filename, char *cgiargs) {
    char buf[MAXBUF], *argv[] = { NULL };
    
    // The server does only a little bit of the header.  
    // The CGI script has to finish writing out the header.
    sprintf(buf, ""
            "HTTP/1.0 200 OK\r\n"
            "Server: Webserver C\r\n");
    
    write_or_die(fd, buf, strlen(buf));
    
    if (fork_or_die() == 0) {                        // child
        setenv_or_die("QUERY_STRING", cgiargs, 1);   // args to cgi go here
        dup2_or_die(fd, STDOUT_FILENO);              // make cgi writes go to socket (not screen)
        extern char **environ;                       // defined by libc 
        execve_or_die(filename, argv, environ);
    } else {
        wait_or_die(NULL);
    }
}

void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];
    
    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    
    // Rather than call read() to read the file into memory, 
    // which would require that we allocate a buffer, we memory-map the file
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);
    
    // put together response
    sprintf(buf, ""
	    "HTTP/1.0 200 OK\r\n"
	    "Server: Webserver C\r\n"
	    "Content-Length: %d\r\n"
	    "Content-Type: %s\r\n\r\n", 
	    filesize, filetype);
    
    write_or_die(fd, buf, strlen(buf));
    
    //  Writes out to the client socket the memory-mapped file 
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}

// Получение Content-Length из заголовков
int get_content_length(char *headers) {
    char *cl_header = strstr(headers, "Content-Length:");
    if (cl_header == NULL) {
        cl_header = strstr(headers, "content-length:");
    }
    
    if (cl_header != NULL) {
        return atoi(cl_header + 15); // 15 = длина "Content-Length:"
    }
    
    return 0;
}

// Чтение заголовков запроса в буфер
void request_parse_headers(int fd, char *headers, size_t headers_size) {
    char buf[MAXBUF];
    int header_len = 0;
    
    headers[0] = '\0';
    
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n") && header_len < headers_size - MAXBUF) {
        strcat(headers, buf);
        header_len += strlen(buf);
        readline_or_die(fd, buf, MAXBUF);
    }
}

// Чтение тела POST запроса
int request_parse_body(int fd, char *headers, char *body, size_t body_size) {
    int content_length = get_content_length(headers);
    
    if (content_length <= 0 || content_length >= body_size) {
        body[0] = '\0';
        return 0;
    }
    
    // Чтение тела запроса
    int bytes_read = 0;
    while (bytes_read < content_length) {
        int n = read(fd, body + bytes_read, content_length - bytes_read);
        if (n <= 0) break;
        bytes_read += n;
    }
    
    body[bytes_read] = '\0';
    return bytes_read;
}

void request_handle_post(int fd, char *body, int body_len) {
    char response[MAXBUF * 2];
    snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n\r\n"
        "<!DOCTYPE html>\n"
        "<html><head><title>POST Result</title></head>\n"
        "<body>\n"
        "<h1>Received POST Data</h1>\n"
        "<div style='border:1px solid #ccc; padding:10px; margin:10px;'>\n"
        "%.*s\n"  // Безопасная вставка тела запроса
        "</div>\n"
        "</body></html>",
        body_len, body);

    write_or_die(fd, response, strlen(response));
}

void request_handle(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];
    char headers[MAXBUF * 8] = {0}; // Буфер для заголовков
    char body[MAXBUF * 16] = {0};   // Буфер для тела запроса
    
    // Чтение первой строки запроса
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);
    
    // Чтение всех заголовков
    request_parse_headers(fd, headers, sizeof(headers));
    
    // Обработка в зависимости от метода запроса
    if (strcasecmp(method, "GET") == 0) {
        // Обработка GET запроса (как было раньше)
        is_static = request_parse_uri(uri, filename, cgiargs);
        if (stat(filename, &sbuf) < 0) {
            request_error(fd, filename, "404", "Not found", "server could not find this file");
            return;
        }
        
        if (is_static) {
            if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
                request_error(fd, filename, "403", "Forbidden", "server could not read this file");
                return;
            }
            request_serve_static(fd, filename, sbuf.st_size);
        } else {
            if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
                request_error(fd, filename, "403", "Forbidden", "server could not run this CGI program");
                return;
            }
            request_serve_dynamic(fd, filename, cgiargs);
        }
    } 
    else if (strcasecmp(method, "POST") == 0) {
        int body_len = request_parse_body(fd, headers, body, sizeof(body));
        printf("POST body received (%d bytes)\n", body_len);
        
        request_handle_post(fd, body, body_len);
    }
    // else if (strcasecmp(method, "POST") == 0) {
    //     // Чтение тела POST запроса
    //     int body_len = request_parse_body(fd, headers, body, sizeof(body));
    //     printf("POST body received (%d bytes)\n", body_len);
        
    //     // Разбор URI и проверка файла
    //     is_static = request_parse_uri(uri, filename, cgiargs);
    //     if (stat(filename, &sbuf) < 0) {
    //         request_error(fd, filename, "404", "Not found", "server could not find this file");
    //         return;
    //     }
        
    //     // POST запросы обычно обрабатываются динамически
    //     if (is_static) {
    //         request_error(fd, uri, "405", "Method Not Allowed", "POST method not allowed for static content");
    //         return;
    //     }
        
    //     // Проверка разрешений и запуск CGI скрипта
    //     if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
    //         request_error(fd, filename, "403", "Forbidden", "server could not run this CGI program");
    //         return;
    //     }
        
    //     // Обработка POST запроса через CGI
    //     if (fork_or_die() == 0) {
    //         // Устанавливаем переменные окружения
    //         setenv_or_die("REQUEST_METHOD", "POST", 1);
    //         char content_length_str[20];
    //         sprintf(content_length_str, "%d", body_len);
    //         setenv_or_die("CONTENT_LENGTH", content_length_str, 1);
            
    //         // Ищем Content-Type в заголовках
    //         char *ct_header = strstr(headers, "Content-Type:");
    //         if (ct_header) {
    //             char content_type[100] = {0};
    //             sscanf(ct_header, "Content-Type: %99[^\r\n]", content_type);
    //             setenv_or_die("CONTENT_TYPE", content_type, 1);
    //         }
            
    //         setenv_or_die("QUERY_STRING", cgiargs, 1);
            
    //         // Создаем пайп для передачи тела запроса в STDIN CGI скрипта
    //         int pipe_fd[2];
    //         if (pipe(pipe_fd) == 0) {
    //             // Записываем тело запроса в пайп
    //             write(pipe_fd[1], body, body_len);
    //             close(pipe_fd[1]);
                
    //             // Перенаправляем STDIN и STDOUT
    //             dup2_or_die(pipe_fd[0], STDIN_FILENO);
    //             dup2_or_die(fd, STDOUT_FILENO);
    //             close(pipe_fd[0]);
                
    //             // Запускаем CGI скрипт
    //             char *argv[] = { filename, NULL };
    //             extern char **environ;
    //             execve_or_die(filename, argv, environ);
    //         }
    //         exit(1); // Если что-то пошло не так
    //     } else {
    //         wait_or_die(NULL);
    //     }
    // } 
    else {
        request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
    }
}
// handle a request
// void request_handle(int fd) {
//     int is_static;
//     struct stat sbuf;
//     char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
//     char filename[MAXBUF], cgiargs[MAXBUF];
    
//     readline_or_die(fd, buf, MAXBUF);
//     sscanf(buf, "%s %s %s", method, uri, version);
//     printf("method:%s uri:%s version:%s\n", method, uri, version);
    
//     if (strcasecmp(method, "GET")) {
//         request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
//         return;
//     }
//     request_read_headers(fd);
    
//     is_static = request_parse_uri(uri, filename, cgiargs);
//     if (stat(filename, &sbuf) < 0) {
//         request_error(fd, filename, "404", "Not found", "server could not find this file");
//         return;
//     }
    
//     if (is_static) {
//         if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
//             request_error(fd, filename, "403", "Forbidden", "server could not read this file");
//             return;
//         }
//         request_serve_static(fd, filename, sbuf.st_size);
//     } else {
// 	if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
//             request_error(fd, filename, "403", "Forbidden", "server could not run this CGI program");
//             return;
//         }
//         request_serve_dynamic(fd, filename, cgiargs);
//     }
// }