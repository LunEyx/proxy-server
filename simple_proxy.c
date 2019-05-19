#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>

#define LISTENNQ 5
#define MAX_RESPONSE 8192
#define MAX_LINE 2048
#define MAX_THREAD 10
#define SERVER_PORT "12345"

struct arg_struct {
    int connfd;
    int index;
};

struct request_header {
    /* request line */
    char port[5];
    char method[MAX_LINE];
    char url[MAX_LINE];
    char host[MAX_LINE];
    char path[MAX_LINE];
    char version[MAX_LINE];

    /* header line */
    int field_counter;
    char field_names[20][MAX_LINE];
    char values[20][MAX_LINE];
};

int listenfd;
int connfd;
pthread_t threads[MAX_THREAD];

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int initialize_server(char* port);
int server_loop();
int send_all(int fd, char* msg, int byte_left);
void* thread_process(void* args);
void process_request(int connfd);
void header_struct_to_string(struct request_header request, char* output);
int http_request(int connfd, struct request_header request);
void https_request(int connfd, struct request_header request);

int send_all(int fd, char* msg, int byte_left) {
    int total = 0;
    int n;

    while (byte_left) {
        n = send(fd, msg + total, byte_left, 0);
        total += n;
        byte_left -= n;

        if (n <= 0) {
            break;
        }
    }

    return byte_left ? -1 : 0;
}

void signal_handler(int sig_num) {
    printf("Disconnect\n");
    shutdown(listenfd, SHUT_RDWR);
    close(listenfd);
    exit(0);
}

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);

    if (argc != 1) {
        if (initialize_server(argv[1]) != 0) {
            return -1;
        }
    } else {
        if (initialize_server(SERVER_PORT) != 0) {
            return -1;
        }
    }

    return server_loop();
}

int initialize_server(char* port) {
    struct addrinfo hints, *res, *p;

    /* initial setting */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, port, &hints, &res)) {
        printf("Error: get address info\n");
        return -1;
    }

    /* initialize server socket */
    for (p = res; p != NULL; p = p->ai_next) {
        listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (listenfd < 0) {
            printf("Error: init socket\n");
            continue;
        }

        int yes = 1;
        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockop");
            continue;
        }

        /* bind the socket to the server address */
        if (bind(listenfd, res->ai_addr, res->ai_addrlen) < 0) {
            printf("Error: bind\n");
            close(listenfd);
            continue;
        }

        break;
    }

    if (p == NULL) {
        printf("Failed to bind socket\n");
        return -1;
    }

    freeaddrinfo(res);

    /* listen to the socket */
    if (listen(listenfd, LISTENNQ) < 0) {
        printf("Error: listen\n");
        return -1;
    }

    return 0;
}

int server_loop() {
    for (int i = 0; i < MAX_THREAD; i++) {
        pthread_create(&(threads[i]), NULL, thread_process, NULL);
    }

    for (int i = 0; i < MAX_THREAD; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}

void* thread_process() {
    int connfd;
    struct sockaddr_in client_addr;
    socklen_t length = sizeof(struct sockaddr_in);

    while (1) {
        /* accept an incoming connection form the remote side */
        connfd = accept(listenfd, (struct sockaddr *)&client_addr, &length);
        if (connfd < 0) {
            printf("Error: accept\n");
            return 0;
        }
        printf("accept\n");

        process_request(connfd);
    }
}

void process_request(int connfd) {
    printf("processing request\n");

    struct request_header request;
    char *token;
    char receive[MAX_RESPONSE] = {0};

    /* print request */
    int n = 0;
    while (1) {
        n += recv(connfd, receive + n, MAX_RESPONSE - 1 - n, 0);

        if(n <= 0) {
            close(connfd);
            return;
        }

        receive[n] = '\0';

        /* HTTP header end with "\r\n\r\n" */
        if(strcmp(receive + n - 4, "\r\n\r\n") == 0) {
            break;
        }
    }

    printf("%s\n", receive);

    token = strtok(receive, "\r\n");
    sscanf(token, "%s %s %s", request.method, request.url, request.version);
    printf("%s %s %s\n", request.method, request.url, request.version);

    if (!strcmp(request.method, "GET")) {
        /* HTTP */
        printf("HTTP METHOD\n");

        sscanf(request.url, "http://%[^/]%s", request.host, request.path);
        printf("%s %s\n", request.host, request.path);
        strcpy(request.port, "80");
        request.field_counter = 0;

        token = strtok(NULL, "\r\n");
        while (token != NULL) {
            int n = sscanf(token, "%[^:]: %[^\n]", request.field_names[request.field_counter], request.values[request.field_counter]);
            printf("%s: %s\n", request.field_names[request.field_counter], request.values[request.field_counter]);
            if (n <= 0) {
                break;
            }

            request.field_counter++;

            token = strtok(NULL, "\r\n");
        }
        printf("http request\n");
        http_request(connfd, request);
    } else if (!strcmp(request.method, "CONNECT")) {
        /* HTTPS */
        printf("HTTPS METHOD\n");

        sscanf(request.url, "%[^:]:%s", request.host, request.port);
        printf("%s:%s\n", request.host, request.port);
        request.field_counter = 0;

        token = strtok(NULL, "\r\n");
        while (token != NULL) {
            int n = sscanf(token, "%[^:]: %[^\n]", request.field_names[request.field_counter], request.values[request.field_counter]);
            if (n <= 0) {
                break;
            }

            request.field_counter++;

            token = strtok(NULL, "\r\n");
        }
        https_request(connfd, request);
    } else {
        printf("UNKNOWN METHOD: %s\n", request.method);

        return;
    }

    printf("Request '%s' done!\n", request.url);

    /* close the connection */
    close(connfd);

    return;
}

void header_struct_to_string(struct request_header request, char* output) {
    char line[MAX_LINE] = {0};
    sprintf(output, "%s %s %s\r\n", request.method, request.path, request.version);

    for (int i = 0; i < request.field_counter; i++) {
        if (!strcmp(request.field_names[i], "Accept-Encoding")) {
        } else if (!strcmp(request.field_names[i], "Proxy-Connection")) {
            sprintf(line, "%s: %s\r\n", "Connection", request.values[i]);
        } else {
            sprintf(line, "%s: %s\r\n", request.field_names[i], request.values[i]);
        }
        strcat(output, line);
    }

    strcat(output, "\r\n\r\n");
}

int http_request(int connfd, struct request_header request) {
    struct addrinfo hints, *res, *p;
    int sockfd;
    char receive[MAX_RESPONSE] = {0};
    char output[MAX_RESPONSE] = {0};

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(request.host, request.port, &hints, &res)) {
        printf("Error: get address info\n");
        char msg[] = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
        printf("Send message: %s\n", msg);
        send_all(connfd, msg, strlen(msg));
        return 0;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sockfd < 0) {
            continue;
        }

        /* connect to the server */
        if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
            continue;
        }

        break;
    }

    if (p == NULL) {
        char msg[] = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
        printf("Send message: %s\n", msg);
        send_all(connfd, msg, strlen(msg));
        return 0;
    }

    freeaddrinfo(res);

    header_struct_to_string(request, output);

    printf("%s\n", output);
    send_all(sockfd, output, strlen(output));

    /* read the response */
    int n;
    char buff = 0;
    int i = 0;
    while (i < MAX_RESPONSE) {
        n = recv(sockfd, &buff, 1, 0);

        if(n <= 0) {
            break;
        }

        receive[i++] = buff;

        /* HTTP header end with "\r\n\r\n" */
        if(strcmp(receive + i - 4, "\r\n\r\n") == 0) {
            break;
        }
    }

    receive[i] = '\0';
    printf("%s", receive);
    send_all(connfd, receive, strlen(receive));

    int type = 0; /* 1: chunk; 2: length */
    int length = 0;
    char* token;
    char temp[MAX_RESPONSE];
    strcpy(temp, receive);
    token = strtok(temp, "\r\n");
    token = strtok(NULL, "\r\n");
    while (token != NULL) {
        int n = sscanf(token, "%[^:]: %[^\n]", request.field_names[request.field_counter], request.values[request.field_counter]);
        if (n <= 0) {
            break;
        }

        if (!strcmp(request.field_names[request.field_counter], "Transfer-Encoding")) {
            type = 1;
            break;
        } else if (!strcmp(request.field_names[request.field_counter], "Content-Length")) {
            type = 2;
            length = atoi(request.values[request.field_counter]);
            break;
        }

        request.field_counter++;

        token = strtok(NULL, "\r\n");
    }

    i = 0;
    if (type == 1) {
        while (1) {
            n = recv(sockfd, receive, MAX_RESPONSE - 1, 0);

            if(n <= 0) {
                break;
            }

            receive[n] = '\0';
            send_all(connfd, receive, n);

            if (!strcmp(receive + n - 5, "0\r\n\r\n")) {
                break;
            }
        }
    } else if (type == 2) {
        i = 0;
        while (length > 0) {
            n = recv(sockfd, receive, MAX_RESPONSE - 1, 0);

            if(n <= 0) {
                break;
            }

            receive[n] = '\0';
            send_all(connfd, receive, n);

            length -= n;
        }
    } else {
        printf("No length method\n");
        while (1) {
            n = recv(sockfd, receive, MAX_RESPONSE - 1, 0);

            if(n <= 0) {
                break;
            }

            receive[n] = '\0';
            send_all(connfd, receive, n);

            if (!strcmp(receive + n - 5, "0\r\n\r\n")) {
                break;
            }
        }
    }

    printf("DONE receive response\n");


    /* close the connection */
    close(sockfd);

    return 0;
}

void https_request(int connfd, struct request_header request) {
}

