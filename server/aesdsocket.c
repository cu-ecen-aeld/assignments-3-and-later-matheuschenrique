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

#define SOCKET_PORT "9000"
#define BACKLOG 10
#define MAX_DATA_SIZE 100
#define MIN_ALLOC_SIZE 100
#define FILE_PATH "/var/tmp/aesdsocketdata"

bool caught_signal = false;
int sockfd;
int new_fd;
FILE *pFile;

static void signal_handler(int number) {
    int errno_saved = errno;
    if (number == SIGINT || number == SIGTERM) {
        syslog(LOG_DEBUG, "Caught signal, exiting");
        remove(FILE_PATH);
        if (sockfd > 0) {
            close(sockfd);
        }
        if (new_fd > 0) {
            close(new_fd);
        }
        closelog();
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

int main(int argc, char *argv[]) {

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
    char write_buffer[MAX_DATA_SIZE] = "";
    char read_buffer[MAX_DATA_SIZE] = "";
    char client_ip[INET_ADDRSTRLEN] = "";
    int bytes_received = 0;
    int bytes_sent;
    int receive_status = 0;
    int current_size = 0;

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

    if (listen(sockfd, BACKLOG)!= 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(res);

    addr_size = sizeof(their_addr);
    while(1) {
        current_size = 0;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);

        if (new_fd == -1) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        // get the ip address from the client
        inet_ntop(AF_INET, &their_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);
        printf("Accepted connection from %s\n", client_ip);

        pFile = fopen(FILE_PATH, "a");
        if (pFile == NULL) {
            perror("fopen");
            exit(EXIT_FAILURE);
        }
        while(1) {
            bytes_received = recv(new_fd, write_buffer, MAX_DATA_SIZE, 0);
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
        fclose(pFile);

        pFile = fopen(FILE_PATH, "rb");
        if (pFile == NULL) {
            perror("fopen");
            exit(EXIT_FAILURE);
        }

        while(!feof(pFile)) {
            size_t bytes = fread(&read_buffer, sizeof(char), MAX_DATA_SIZE, pFile);
            bytes_sent = send(new_fd, (void*)read_buffer, bytes, 0);
            if (bytes_sent == -1) {
                perror("send");
                exit(EXIT_FAILURE);
            }
        }
        fclose(pFile);

        close(new_fd);

        syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
        printf("Closed connection from %s\n", client_ip);
    }
    return 0;
}