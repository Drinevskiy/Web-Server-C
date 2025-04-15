#include "io_helper.h"
#include "request.h"


#define MAXBUF (8192)
#define UPLOAD_DIR "uploads"
#define MAX_FILE_SIZE (10 * 1024 * 1024) // 10MB
#define BOUNDARY_PREFIX "--"



// Implementation of error response
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];

    // Create the body of error message first (have to know its length for header)
    sprintf(body, ""
        "<!doctype html>\r\n"
        "<head>\r\n"
        "  <title>WebServer Error</title>\r\n"
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

// Reads and discards everything up to an empty text line
void request_read_headers(int fd) {
    char buf[MAXBUF];
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
        readline_or_die(fd, buf, MAXBUF);
    }
    return;
}

// Return 1 if static, 0 if dynamic content
// Calculates filename (and cgiargs, for dynamic) from uri
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

// Fills in the filetype given the filename
void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".jpeg")) strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".png")) strcpy(filetype, "image/png");
    else if (strstr(filename, ".css")) strcpy(filetype, "text/css");
    else if (strstr(filename, ".js")) strcpy(filetype, "application/javascript");
    else strcpy(filetype, "text/plain");
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

// URL decode function
void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if (*src == '%' && (a = src[1]) && (b = src[2]) && isxdigit(a) && isxdigit(b)) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Parse POST data
post_param_t* parse_post_data(const char *data, int *num_params) {
    if (!data || *data == '\0') {
        *num_params = 0;
        return NULL;
    }

    // Count parameters
    int count = 1;
    const char *p = data;
    while (*p) if (*p++ == '&') count++;
    *num_params = count;

    post_param_t *params = malloc(count * sizeof(post_param_t));
    if (!params) return NULL;

    char *copy = strdup(data);
    char *token = strtok(copy, "&");
    int i = 0;

    while (token != NULL && i < count) {
        char *sep = strchr(token, '=');
        if (sep) {
            *sep = '\0';
            params[i].key = strdup(token);
            
            // Allocate extra memory for decoding
            char *decoded = malloc(strlen(sep + 1) * 3 + 1);
            url_decode(decoded, sep + 1);
            params[i].value = decoded;
        } else {
            params[i].key = strdup(token);
            params[i].value = strdup("");
        }
        i++;
        token = strtok(NULL, "&");
    }
    free(copy);
    return params;
}

// Free post parameters
void free_post_params(post_param_t *params, int num_params) {
    for (int i = 0; i < num_params; i++) {
        free(params[i].key);
        free(params[i].value);
    }
    free(params);
}

// Handle standard POST requests
void request_handle_post(int fd, char* headers, char *body, int body_len) {
    // Check Content-Type
    char content_type[100] = {0};
    if (strstr(headers, "Content-Type:")) {
        sscanf(strstr(headers, "Content-Type:"), "Content-Type: %99[^\r\n]", content_type);
    }

    if (strcmp(content_type, "application/x-www-form-urlencoded") != 0) {
        request_error(fd, "POST", "400", "Bad Request", 
            "Unsupported content type");
        return;
    }

    // Parse parameters
    int num_params = 0;
    post_param_t *params = parse_post_data(body, &num_params);

    // Create HTML response
    char response_head[MAXBUF];
    snprintf(response_head, sizeof(response_head),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n\r\n");
    write_or_die(fd, response_head, strlen(response_head));

    // Write response body
    char *html_head = "<!DOCTYPE html><html><head><title>POST Data</title></head><body>"
                     "<h1>Parsed POST Parameters</h1><table border='1'>";
    write_or_die(fd, html_head, strlen(html_head));

    for (int i = 0; i < num_params; i++) {
        char row[MAXBUF * 2];
        snprintf(row, sizeof(row),
            "<tr><td><strong>%.100s</strong></td><td>%.500s</td></tr>",
            params[i].key, params[i].value);
        write_or_die(fd, row, strlen(row));
    }

    char *html_tail = "</table></body></html>";
    write_or_die(fd, html_tail, strlen(html_tail));

    free_post_params(params, num_params);
}

// Get Content-Length from headers
int get_content_length(char *headers) {
    char *cl_header = strstr(headers, "Content-Length:");
    if (cl_header == NULL) {
        cl_header = strstr(headers, "content-length:");
    }

    if (cl_header != NULL) {
        return atoi(cl_header + 15); // 15 = length of "Content-Length:"
    }

    return 0;
}

// Parse request headers
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

// Parse request body
int request_parse_body(int fd, char *headers, char *body, size_t body_size) {
    int content_length = get_content_length(headers);
    
    if (content_length <= 0 || content_length >= body_size) {
        fprintf(stderr, "Invalid content length: %d (max: %zu)\n", content_length, body_size);
        body[0] = '\0';
        return 0;
    }

    // Read request body
    int bytes_read = 0;
    while (bytes_read < content_length) {
        int n = read(fd, body + bytes_read, content_length - bytes_read);
        if (n <= 0) break;
        bytes_read += n;
    }

    body[bytes_read] = '\0';
    return bytes_read;
}

// Create upload directory
void create_upload_dir() {
    struct stat st = {0};
    if (stat(UPLOAD_DIR, &st) == -1) {
        mkdir(UPLOAD_DIR, 0755);  // Changed permissions to be more permissive
        fprintf(stderr, "Created upload directory: %s\n", UPLOAD_DIR);
    }
}

// Generate unique filename
void generate_filename(char *buffer, const char *ext) {
    uuid_t uuid;
    uuid_generate_random(uuid);
    char uuid_str[37];
    uuid_unparse(uuid, uuid_str);
    snprintf(buffer, 256, "%s/%s.%s", UPLOAD_DIR, uuid_str, ext);
}

// Serve the upload form
void serve_upload_form(int fd) {
    char buf[MAXBUF];
    
    // HTTP header
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Type: text/html\r\n\r\n");
    write_or_die(fd, buf, strlen(buf));
    
    // HTML content with file upload form
    char *html = "<!DOCTYPE html>\n"
                 "<html>\n"
                 "<head>\n"
                 "    <title>Photo Upload</title>\n"
                 "    <style>\n"
                 "        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }\n"
                 "        h1 { color: #333; }\n"
                 "        form { margin: 20px 0; border: 1px solid #ddd; padding: 20px; border-radius: 5px; }\n"
                 "        .form-group { margin-bottom: 15px; }\n"
                 "        label { display: block; margin-bottom: 5px; font-weight: bold; }\n"
                 "        input[type=file] { padding: 10px; border: 1px solid #ddd; width: 100%; box-sizing: border-box; }\n"
                 "        button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; cursor: pointer; }\n"
                 "        button:hover { background-color: #45a049; }\n"
                 "    </style>\n"
                 "</head>\n"
                 "<body>\n"
                 "    <h1>Upload Photos</h1>\n"
                 "    <form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">\n"
                 "        <div class=\"form-group\">\n"
                 "            <label for=\"photo\">Select photo to upload:</label>\n"
                 "            <input type=\"file\" id=\"photo\" name=\"photo\" accept=\"image/*\" required>\n"
                 "        </div>\n"
                 "        <button type=\"submit\">Upload Photo</button>\n"
                 "    </form>\n"
                 "</body>\n"
                 "</html>";
    
    write_or_die(fd, html, strlen(html));
}

// Extract boundary from Content-Type header
char* get_boundary(char *content_type) {
    char *boundary = NULL;
    char *boundary_start = strstr(content_type, "boundary=");
    
    if (boundary_start) {
        boundary_start += 9; // Skip "boundary="
        
        // Handle quoted boundary
        if (*boundary_start == '"') {
            boundary_start++;
            char *end = strchr(boundary_start, '"');
            if (end) {
                size_t len = end - boundary_start;
                boundary = malloc(len + 3); // +3 for "--" prefix and null terminator
                sprintf(boundary, "--%s", boundary_start);
                boundary[len + 2] = '\0';
            }
        } else {
            // Handle unquoted boundary
            char *end = strpbrk(boundary_start, " \t\r\n;");
            size_t len = end ? (size_t)(end - boundary_start) : strlen(boundary_start);
            boundary = malloc(len + 3); // +3 for "--" prefix and null terminator
            sprintf(boundary, "--%s", boundary_start);
            boundary[len + 2] = '\0';
        }
    }
    
    return boundary;
}

void normalize_content_type(char *content_type) {
    if (!content_type) return;
    
    // Обрезаем пробелы в начале и конце
    char *start = content_type;
    while (*start && isspace(*start)) start++;
    
    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) end--;
    *(end + 1) = '\0';
    
    // Обрезаем параметры после ;
    char *semicolon = strchr(start, ';');
    if (semicolon) *semicolon = '\0';
    
    // Переносим результат обратно в начало буфера
    if (start != content_type) {
        memmove(content_type, start, strlen(start) + 1);
    }
}

// Handle multipart form data upload
void handle_multipart_upload(int fd, char *body, size_t body_size, char *boundary) {
    create_upload_dir();
    
    size_t boundary_len = strlen(boundary);
    char *end_boundary = malloc(boundary_len + 3); // --boundary--
    sprintf(end_boundary, "%s--", boundary);
    
    char *current = body;
    char *body_end = body + body_size;
    int files_uploaded = 0;
    
    // Start HTML response
    char response[MAXBUF];
    sprintf(response, 
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>Upload Results</title>\n"
        "    <style>\n"
        "        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }\n"
        "        h1 { color: #333; }\n"
        "        .success { color: green; }\n"
        "        .error { color: red; }\n"
        "        .file-container { margin-top: 20px; border: 1px solid #ddd; padding: 15px; border-radius: 5px; }\n"
        "        .file-link { margin-top: 10px; }\n"
        "        a { color: #0066cc; text-decoration: none; }\n"
        "        a:hover { text-decoration: underline; }\n"
        "        img { max-width: 300px; border: 1px solid #ddd; padding: 5px; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>Upload Results</h1>\n"
    );
    write_or_die(fd, response, strlen(response));
    
    while (current < body_end) {
        // Find the next boundary
        char *next_boundary = memmem(current, body_end - current, boundary, boundary_len);
        if (!next_boundary) {
            break; // No more boundaries found
        }
        
        // Move past this boundary
        current = next_boundary + boundary_len + 2; // +2 for CRLF
        
        // Check if we've reached the end boundary
        if (current >= body_end || 
            memmem(next_boundary, body_end - next_boundary, end_boundary, strlen(end_boundary)) == next_boundary) {
            break;
        }
        
        // Find the end of headers (double CRLF)
        char *headers_end = memmem(current, body_end - current, "\r\n\r\n", 4);
        if (!headers_end) {
            continue; // No headers end found, malformed
        }
        
        // Extract headers into a temporary buffer for easy processing
        size_t headers_len = headers_end - current;
        char *headers = malloc(headers_len + 1);
        memcpy(headers, current, headers_len);
        headers[headers_len] = '\0';
        
        // Find next boundary to determine content length
        char *part_end = memmem(headers_end + 4, body_end - (headers_end + 4), boundary, boundary_len);
        if (!part_end) {
            free(headers);
            break; // Malformed, no closing boundary
        }
        
        // Calculate content length (subtract 2 for CRLF before boundary)
        size_t content_len = part_end - (headers_end + 4) - 2;
        char *content = headers_end + 4;
        
        // Parse Content-Disposition to get filename
        char *filename = NULL;
        char *content_type = NULL;
        char *disp = strstr(headers, "Content-Disposition:");
        if (disp) {
            char *filename_start = strstr(disp, "filename=\"");
            if (filename_start) {
                filename_start += 10; // Skip 'filename="'
                char *filename_end = strchr(filename_start, '"');
                if (filename_end) {
                    size_t name_len = filename_end - filename_start;
                    filename = malloc(name_len + 1);
                    memcpy(filename, filename_start, name_len);
                    filename[name_len] = '\0';
                }
            }
        }
        
        char *ct = strstr(headers, "Content-Type:");
        if (ct) {
            char *ct_start = ct + 13;  // "Content-Type:" длина 13 символов
            while (*ct_start == ' ' || *ct_start == '\t') ct_start++;
            
            char *ct_end = strstr(ct_start, "\r\n");
            if (!ct_end) ct_end = strchr(ct_start, '\0');
            
            size_t ct_len = ct_end - ct_start;
            content_type = malloc(ct_len + 1);
            memcpy(content_type, ct_start, ct_len);
            content_type[ct_len] = '\0';
            
            // Нормализуем Content-Type
            normalize_content_type(content_type);
        }
        
        // Process file if we have a filename and content
        if (filename && content_len > 0) {
            const char *ext = "bin";
            if (content_type) {
                if (strcasecmp(content_type, "image/jpeg") == 0) ext = "jpg";
                else if (strcasecmp(content_type, "image/pjpeg") == 0) ext = "jpg";
                else if (strcasecmp(content_type, "image/png") == 0) ext = "png";
                else if (strcasecmp(content_type, "image/gif") == 0) ext = "gif";
            }
            
            // Generate unique filename
            char new_filename[256];
            generate_filename(new_filename, ext);
            
            // Save file
            FILE *fp = fopen(new_filename, "wb");
            if (fp) {
                size_t written = fwrite(content, 1, content_len, fp);
                fclose(fp);
                
                if (written == content_len) {
                    // Success
                    files_uploaded++;
                    char success_msg[MAXBUF];
                    sprintf(success_msg, 
                        "<div class=\"file-container\">\n"
                        "    <p class=\"success\">File '%s' uploaded successfully</p>\n"
                        "    <img src=\"/%s\" alt=\"Uploaded Image\">\n"
                        "    <p class=\"file-link\"><a href=\"/%s\" target=\"_blank\">View full size</a></p>\n"
                        "</div>\n",
                        filename, new_filename, new_filename);
                    write_or_die(fd, success_msg, strlen(success_msg));
                } else {
                    // Write error
                    char error_msg[MAXBUF];
                    sprintf(error_msg, 
                        "<div class=\"file-container\">\n"
                        "    <p class=\"error\">Error writing file '%s': Only %zu of %zu bytes written</p>\n"
                        "</div>\n",
                        filename, written, content_len);
                    write_or_die(fd, error_msg, strlen(error_msg));
                }
            } else {
                // File open error
                char error_msg[MAXBUF];
                sprintf(error_msg, 
                    "<div class=\"file-container\">\n"
                    "    <p class=\"error\">Error saving file '%s': %s</p>\n"
                    "</div>\n",
                    filename, strerror(errno));
                write_or_die(fd, error_msg, strlen(error_msg));
            }
        }
        
        // Free allocated memory
        free(headers);
        if (filename) free(filename);
        if (content_type) free(content_type);
        
        // Move to next part
        current = part_end;
    }
    
    // No files uploaded
    if (files_uploaded == 0) {
        char no_files_msg[] = 
            "<p class=\"error\">No valid files were found in the upload.</p>\n"
            "<p><a href=\"/upload\">Try again</a></p>\n";
        write_or_die(fd, no_files_msg, strlen(no_files_msg));
    } 
    
    // Close HTML
    char html_close[] = "</body>\n</html>";
    write_or_die(fd, html_close, strlen(html_close));
    
    free(end_boundary);
}

// Main request handler
void request_handle(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];
    char headers[MAXBUF * 8] = {0}; // Headers buffer
    char *body = NULL;              // Body buffer (dynamically allocated)

    // Read first line of request
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);

    // Read all headers
    request_parse_headers(fd, headers, sizeof(headers));

    // Handle upload form request
    if (strcasecmp(method, "GET") == 0 && strcmp(uri, "/upload") == 0) {
        serve_upload_form(fd);
        return;
    }

    // Handle based on request method
    if (strcasecmp(method, "GET") == 0) {
        // Handle GET request
        is_static = request_parse_uri(uri, filename, cgiargs);
        if (stat(filename, &sbuf) < 0) {
            request_error(fd, filename, "404", "Not found", "Server could not find this file");
            return;
        }
        
        if (is_static) {
            if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
                request_error(fd, filename, "403", "Forbidden", "Server could not read this file");
                return;
            }
            request_serve_static(fd, filename, sbuf.st_size);
        } else {
            // Handle dynamic content if needed
            request_error(fd, uri, "501", "Not Implemented", "CGI not implemented");
        }
    } 
    else if (strcasecmp(method, "POST") == 0) {
        // Get Content-Length
        int content_length = get_content_length(headers);
        if (content_length <= 0) {
            request_error(fd, method, "411", "Length Required", "Content-Length header is required for POST requests");
            return;
        }
        
        // Allocate memory for body
        body = malloc(content_length + 1);
        if (!body) {
            request_error(fd, method, "500", "Internal Server Error", "Failed to allocate memory for request body");
            return;
        }
        
        // Read body
        int body_len = 0;
        int bytes_remaining = content_length;
        while (bytes_remaining > 0) {
            int n = read(fd, body + body_len, bytes_remaining);
            if (n <= 0) break;
            body_len += n;
            bytes_remaining -= n;
        }
        body[body_len] = '\0';
        
        // Extract Content-Type
        char content_type[256] = {0};
        if (strstr(headers, "Content-Type:")) {
            char *ct_start = strstr(headers, "Content-Type:") + 13;
            // Skip whitespace
            while (*ct_start == ' ' || *ct_start == '\t') ct_start++;
            
            char *ct_end = strstr(ct_start, "\r\n");
            if (ct_end) {
                size_t ct_len = ct_end - ct_start;
                if (ct_len < sizeof(content_type) - 1) {
                    memcpy(content_type, ct_start, ct_len);
                    content_type[ct_len] = '\0';
                }
            }
        }
        
        // Process upload form submission
        if (strcmp(uri, "/upload") == 0) {
            if (strstr(content_type, "multipart/form-data")) {
                char *boundary = get_boundary(content_type);
                if (boundary) {
                    handle_multipart_upload(fd, body, body_len, boundary);
                    free(boundary);
                } else {
                    request_error(fd, "POST", "400", "Bad Request", "Missing or invalid boundary in multipart/form-data");
                }
            } else {
                request_error(fd, "POST", "400", "Bad Request", "File uploads must use multipart/form-data");
            }
        }
        else if (strstr(content_type, "application/x-www-form-urlencoded")) {
            // Handle standard form submission
            request_handle_post(fd, headers, body, body_len);
        }
        else {
            request_error(fd, content_type, "415", "Unsupported Media Type", "Content type not supported");
        }
        
        free(body);
    }
    else {
        request_error(fd, method, "501", "Not Implemented", "Server does not implement this method");
    }
}