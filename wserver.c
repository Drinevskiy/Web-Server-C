// #include <stdio.h>
// #include "request.h"
// #include "io_helper.h"

#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include "request.h"
#include "io_helper.h"

char default_root[] = ".";
volatile int keep_running = 1;

// Обработчик сигналов для завершения сервера
void handle_signal(int sig) {
    printf("Caught signal %d. Shutting down...\n", sig);
    keep_running = 0;
}

// Структура для передачи данных потоку
typedef struct {
    int fd;
} thread_args_t;

// Функция потока для обработки запроса
void* handle_request_thread(void* args) {
    thread_args_t* thread_args = (thread_args_t*)args;
    int fd = thread_args->fd;
    
    // Освобождаем память аргументов
    free(args);
    
    // Устанавливаем режим отсоединенного потока
    pthread_detach(pthread_self());
    
    // Обрабатываем запрос
    request_handle(fd);
    
    // Закрываем соединение
    close_or_die(fd);
    
    return NULL;
}

//
// ./wserver [-d <basedir>] [-p <portnum>] [-t <threads>]
// 
int main(int argc, char *argv[]) {
    int c;
    char *root_dir = default_root;
    int port = 10000;
    int num_threads = 1; // По умолчанию однопоточный режим
    
    while ((c = getopt(argc, argv, "d:p:t:")) != -1)
    switch (c) {
    case 'd':
        root_dir = optarg;
        break;
    case 'p':
        port = atoi(optarg);
        break;
    case 't':
        num_threads = atoi(optarg);
        if (num_threads <= 0) num_threads = 1;
        break;
    default:
        fprintf(stderr, "usage: wserver [-d basedir] [-p port] [-t threads]\n");
        exit(1);
    }

    // Регистрация обработчика сигналов
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Смена рабочего каталога
    chdir_or_die(root_dir);

    // Запуск сервера
    printf("Starting server on port %d with %d threads\n", port, num_threads);
    printf("Serving documents from directory: %s\n", root_dir);
    
    int listen_fd = open_listen_fd_or_die(port);
    
    while (keep_running) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        
        // Использование select для неблокирующего accept
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        if (select(listen_fd + 1, &read_fds, NULL, NULL, &tv) > 0) {
            int conn_fd = accept(listen_fd, (sockaddr_t *) &client_addr, (socklen_t *) &client_len);
            
            if (conn_fd >= 0) {
                if (num_threads > 1) {
                    // Многопоточная обработка
                    pthread_t thread;
                    thread_args_t* args = (thread_args_t*)malloc(sizeof(thread_args_t));
                    args->fd = conn_fd;
                    
                    if (pthread_create(&thread, NULL, handle_request_thread, args) != 0) {
                        // Если не удалось создать поток, обрабатываем запрос в основном потоке
                        request_handle(conn_fd);
                        close_or_die(conn_fd);
                    }
                } else {
                    // Однопоточная обработка
                    request_handle(conn_fd);
                    close_or_die(conn_fd);
                }
            }
        }
    }
    
    // Закрываем слушающий сокет перед выходом
    close(listen_fd);
    printf("Server stopped\n");
    
    return 0;
}


    


 