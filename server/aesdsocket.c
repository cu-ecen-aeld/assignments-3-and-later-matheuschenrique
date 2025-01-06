#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <pthread.h>
#include "queue.h"
#include "time.h"

#define SOCKET_PORT "9000"
#define BACKLOG 10
#define MAX_DATA_SIZE 100
#define MIN_ALLOC_SIZE 100
#define FILE_PATH "/dev/aesdchar"

bool caught_signal = false;
int sockfd;
int new_fd;
// pthread_t timestamp_thread;

pthread_mutex_t lock;

typedef struct slist_data_s slist_data_t;
struct slist_data_s {
    pthread_t thread;
    int client_fd;
    bool thread_complete_success;
    struct sockaddr_in their_addr;
    SLIST_ENTRY(slist_data_s) entries;
};

slist_data_t *datap=NULL;

SLIST_HEAD(slisthead, slist_data_s) head;

static void signal_handler(int number) {
    int errno_saved = errno;
    if (number == SIGINT || number == SIGTERM) {
        syslog(LOG_DEBUG, "Caught signal, exiting");
        // remove(FILE_PATH);
        if (sockfd > 0) {
            close(sockfd);
        }
        if (new_fd > 0) {
            close(new_fd);
        }
        closelog();
        pthread_mutex_destroy(&lock);

        SLIST_FOREACH(datap, &head, entries) {
            pthread_join(datap->thread, NULL);
        }
        // pthread_join(timestamp_thread, NULL);
    }
    errno = errno_saved;
    exit(EXIT_SUCCESS);
}

void fork_config() {
    pid_t pid;

    pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // parent procces
        exit(EXIT_SUCCESS);
    }
}

void signal_config() {
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = signal_handler;
    if( sigaction(SIGTERM, &action, NULL) != 0 ) {
        perror("SIGTERM");
        exit(EXIT_FAILURE);
    }
    if( sigaction(SIGINT, &action, NULL) ) {
        perror("SIGINT");
        exit(EXIT_FAILURE);
    }
}

// void* putTimestampCallback(void *arg) {
//     FILE *pFile;

//     while(1) {
//         char time_str[200];
//         time_t t = time(NULL);
//         struct tm *tmp = localtime(&t);
//         if (tmp == NULL) {
//             perror("localtime");
//             exit(EXIT_FAILURE);
//         }

//         //RFC 2822-compliant date format
//         if (strftime(time_str, sizeof(time_str), "%a, %d %b %Y %T %z", tmp) == 0) {
//             perror("strftime");
//             exit(EXIT_FAILURE);
//         }
//         pthread_mutex_lock(&lock);

//         pFile = fopen(FILE_PATH, "a");
//         if (pFile == NULL) {
//             perror("fopen");
//             exit(EXIT_FAILURE);
//         }

//         printf("timestamp: %s\n", time_str);
//         fprintf(pFile, "timestamp: %s\n", time_str);
//         fflush(pFile);
//         fclose(pFile);

//         pthread_mutex_unlock(&lock);
//         sleep(10);

//     }
// }

void* threadCallback(void *arg) {
    slist_data_t *data = (slist_data_t *)arg;
    char write_buffer[MAX_DATA_SIZE] = "";
    char read_buffer[MAX_DATA_SIZE] = "";
    char client_ip[INET_ADDRSTRLEN] = "";
    FILE *pFile;
    int bytes_received = 0;
    int bytes_sent;

    // get the ip address from the client
    inet_ntop(AF_INET, &data->their_addr, client_ip, INET_ADDRSTRLEN);
    syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);
    printf("Accepted connection from %s\n", client_ip);

    pthread_mutex_lock(&lock);

    pFile = fopen(FILE_PATH, "a");
    if (pFile == NULL) {
        perror("fopen");
        close(data->client_fd);
        exit(EXIT_FAILURE);
    }
    while(1) {
        bytes_received = recv(data->client_fd, write_buffer, MAX_DATA_SIZE, 0);
        if (bytes_received < 0) {
            perror("recv");
            exit(EXIT_FAILURE);
        } else if (bytes_received > 0) {
            write_buffer[bytes_received] = '\0';
            fprintf(pFile, "%s", write_buffer);
            fflush(pFile);
            if(strchr(write_buffer, '\n')) {
                break;
            }
        } else {
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    fclose(pFile);

    pFile = fopen(FILE_PATH, "rb");
    if (pFile == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    while(!feof(pFile)) {
        size_t bytes = fread(&read_buffer, sizeof(char), MAX_DATA_SIZE, pFile);
        bytes_sent = send(data->client_fd, (void*)read_buffer, bytes, 0);
        if (bytes_sent == -1) {
            perror("send");
            exit(EXIT_FAILURE);
        }
    }
    fclose(pFile);

    close(data->client_fd);

    data->thread_complete_success = true;
    pthread_exit(NULL);
    return NULL;
}

int main(int argc, char *argv[]) {

    pthread_mutex_init(&lock, NULL);

    SLIST_INIT(&head);

    bool run_as_daemon = false;
    if (argc == 2 && !strcmp(argv[1], "-d")) {
        printf("running as daemon\n");
        run_as_daemon = true;
    }

    signal_config();

    openlog("aesdsocket", LOG_PID, LOG_USER);
    int status;
    int yes = 1; // used for setsockopt to reuse the address
    struct addrinfo hints, *res;
    struct sockaddr_in their_addr;
    socklen_t addr_size;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(NULL, SOCKET_PORT, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (run_as_daemon) {
        fork_config();
    }

    // pthread_create(&timestamp_thread, NULL, putTimestampCallback, NULL);

    if (listen(sockfd, BACKLOG)!= 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(res);

    addr_size = sizeof(their_addr);
    while(1) {
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);

        if (new_fd == -1) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        datap = (slist_data_t *)malloc(sizeof(slist_data_t));
        if (datap == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }

        datap->client_fd = new_fd;
        datap->thread_complete_success = false;
        datap->their_addr = their_addr;
        SLIST_INSERT_HEAD(&head, datap, entries);
        pthread_create(&datap->thread, NULL, threadCallback, (void *)datap);

        SLIST_FOREACH(datap, &head, entries) {
            if(datap->thread_complete_success == true) {
                pthread_join(datap->thread, NULL);
            }
        }

    }
    return 0;
}