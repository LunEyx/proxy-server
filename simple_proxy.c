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
int free_thread_count = 0;

int initialize_server(char* port);
int server_loop();
void* process_request(int connfd);
int send_request(int connfd, struct request_header request);
void header_struct_to_string(struct request_header request, char* output);

void signal_handler(int sig_num) {
    printf("Disconnect\n");
    close(listenfd);
    exit(0);
}


int main(int argc, char** argv) {
    /* signal(SIGPIPE, SIG_IGN); */
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
    struct addrinfo hints, *res;

    /* initial setting */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo(NULL, port, &hints, &res);

    /* initialize server socket */
    listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listenfd < 0) {
        printf("Error: init socket\n");
        return -1;
    }

    /* bind the socket to the server address */
    if (bind(listenfd, res->ai_addr, res->ai_addrlen) < 0) {
        printf("Error: bind\n");
        return -1;
    }

    /* listen to the socket */
    if (listen(listenfd, LISTENNQ) < 0) {
        printf("Error: listen\n");
        return -1;
    }

    return 0;
}

int server_loop() {
    int connfd;
    struct sockaddr_in client_addr;
    socklen_t length = sizeof(struct sockaddr_in);

    /* keep processing incoming requests */
    while (1) {
        printf("loop\n");
        /* accept an incoming connection form the remote side */
        connfd = accept(listenfd, (struct sockaddr *)&client_addr, &length);
        if (connfd < 0) {
            printf("Error: accept\n");
            return -1;
        }
        printf("accept\n");

        process_request(connfd);
    }

    return 0;
}

void* process_request(int connfd) {
    printf("processing request\n");

    struct request_header request;
    char *token;
    char receive[MAX_RESPONSE] = {0};

    /* print request */
    int n = recv(connfd, receive, MAX_RESPONSE - 1, 0);

    if(n <= 0) {
        close(connfd);
        return 0;
    }

    receive[n] = '\0';

    /* HTTP header end with "\r\n\r\n" */
    /* if(strcmp(receive + i - 4, "\r\n\r\n") == 0) { */
        /* break; */
    /* } */

    printf("after recv\n");
    /* receive[i] = '\0'; */
    printf("after recv 2\n");
    printf("%s\n", receive);
    printf("after recv 3\n");
    /* if (i == 0) { */
        /* close(connfd); */
        /* return 0; */
    /* } */

    token = strtok(receive, "\r\n");
    sscanf(token, "%s %s %s", request.method, request.url, request.version);
    printf("%s %s %s\n", request.method, request.url, request.version);

    if (!strcmp(request.method, "GET")) {
        /* HTTP */
        printf("HTTP METHOD\n");

        printf("before sscanf\n");
        sscanf(request.url, "http://%[^/]%s", request.host, request.path);
        printf("after sscanf\n");
        printf("%s %s\n", request.host, request.path);
        strcpy(request.port, "80");
    } else if (!strcmp(request.method, "CONNECT")) {
        /* TODO: SUPPORT IT */
        close(connfd);

        return 0;
        /* HTTPS */
    } else {
        printf("UNKNOWN METHOD: %s\n", request.method);

        return 0;
    }
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
    send_request(connfd, request);

    printf("Request '%s' done!\n", request.url);

    /* close the connection */
    close(connfd);

    return 0;
}

int send_request(int connfd, struct request_header request) {
    struct addrinfo hints, *res;
    int sockfd;
    char receive[MAX_RESPONSE] = {0};
    char output[MAX_RESPONSE] = {0};

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo(request.host, request.port, &hints, &res);

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    /* connect to the server */
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        printf("Error: connect\n");
        return -1;
    }

    header_struct_to_string(request, output);

    printf("%s\n", output);
    send(sockfd, output, strlen(output), 0);

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
    send(connfd, receive, strlen(receive), 0);

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
            send(connfd, receive, strlen(receive), 0);
            /* printf("%s\n", receive); */

            if (!strcmp(receive + n - 5, "0\r\n\r\n")) {
                break;
            }
        }
    } else if (type == 2) {
        i = 0;
        int buffer[MAX_RESPONSE] = {0};
        while (length > 0) {
            n = recv(sockfd, buffer, MAX_RESPONSE - 1, MSG_WAITALL);

            if(n <= 0) {
                break;
            }

            receive[n] = '\0';
            send(connfd, buffer, n, 0);

            length -= n;
            printf("%d\n", length);
        }
    } else {
        printf("Error: No length method\n");
        return -1;
    }

    printf("DONE receive response\n");


    /* close the connection */
    close(sockfd);

    return 0;
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


