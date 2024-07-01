#include "asgn4_helper_funcs.h"
#include "connection.h"
#include "debug.h"
#include "request.h"
#include "response.h"
#include "queue.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/file.h>

#include <sys/stat.h>

void handle_connection(int);
void handle_get(conn_t *);
void handle_put(conn_t *);
void handle_unsupported(conn_t *);
void *work(void *args);
queue_t *queue;
pthread_mutex_t bocchithelock = PTHREAD_MUTEX_INITIALIZER;

void *work(void *args) {
    (void) args;
    void *thing;
    uintptr_t fd;
    while (1) {
        queue_pop(queue, &thing);
        fd = (uintptr_t) (thing);
        handle_connection(fd);
        close(fd);
    }
}
int main(int argc, char **argv) {
    if (argc < 2) {
        warnx("wrong arguments: %s port_num", argv[0]);
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    size_t port = (size_t) strtoull(argv[argc - 1], &endptr, 10);

    if (endptr && *endptr != '\0') {
        warnx("invalid port number: %s", argv[1]);
        return EXIT_FAILURE;
    }
    //
    // getopt
    //
    int optin;
    int numthreads = 4;
    while ((optin = getopt(argc, argv, "t:")) != -1) {
        switch (optin) {
        case 't': numthreads = strtol(optarg, NULL, 10); break;
        default: fprintf(stderr, "wrong arguments\n"); return EXIT_FAILURE;
        }
    }
    //
    queue = queue_new(numthreads);

    signal(SIGPIPE, SIG_IGN);
    Listener_Socket sock;
    //listener_init(&sock, port);
    if ((listener_init(&sock, port)) < 0) {
        warnx("listener failed: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    //treads
    //dispatcher thread and worker threads
    pthread_t workers[numthreads];
    for (int i = 0; i < numthreads; i++) {
        pthread_create(&workers[i], NULL, &work, NULL);
    }

    while (1) {
        uintptr_t connfd = (uintptr_t) listener_accept(&sock);
        //handle_connection(connfd);
        //close(connfd);
        queue_push(queue, (void *) connfd);
    }
    //delet
    for (int i = 0; i < numthreads; i++) {
        pthread_join(workers[i], NULL);
    }
    queue_delete(&queue);

    return EXIT_SUCCESS;
}

void handle_connection(int connfd) {
    conn_t *conn = conn_new(connfd);
    const Response_t *res = conn_parse(conn);

    if (res != NULL) {
        conn_send_response(conn, res);
    } else {
        //debug("%s", conn_str(conn));
        const Request_t *req = conn_get_request(conn);

        if (req == &REQUEST_GET) {
            handle_get(conn);
        } else if (req == &REQUEST_PUT) {
            handle_put(conn);
        } else {
            handle_unsupported(conn);
        }
    }

    conn_delete(&conn);
    return;
}

void handle_get(conn_t *conn) {
    // TODO: Implement GET
    const Response_t *get_response = NULL;
    char *uri = conn_get_uri(conn);
    //request id
    //char *conn_get_header(conn_t *conn, char *header);
    char *requestid = conn_get_header(conn, "Request-Id");
    if (requestid == NULL) {
        requestid = "0";
    }
    pthread_mutex_lock(&bocchithelock);
    int fd = open(uri, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            get_response = &RESPONSE_NOT_FOUND;
        } else if (errno == EACCES) {
            get_response = &RESPONSE_FORBIDDEN;
        } else {
            get_response = &RESPONSE_INTERNAL_SERVER_ERROR;
        }
        pthread_mutex_unlock(&bocchithelock);
        fprintf(stderr, "GET,/%s,%d,%s\n", uri, response_get_code(get_response), requestid);
        conn_send_response(conn, get_response);
        //close(fd);
        return;
    }
    flock(fd, LOCK_SH);
    pthread_mutex_unlock(&bocchithelock);
    //debug("GET %s", uri);
    struct stat fileStat;
    fstat(fd, &fileStat);
    if (S_ISDIR(fileStat.st_mode) != 0) { //bad
        //if file is a directory, return forbidden
        get_response = &RESPONSE_FORBIDDEN;
        fprintf(stderr, "GET,/%s,%d,%s\n", uri, response_get_code(get_response), requestid);
        conn_send_response(conn, get_response);
        //close(fd);
        return;
    }
    //size of file
    off_t filesize = fileStat.st_size;
    // send a message body from the file (fd)
    //const Response_t *conn_send_file(conn_t *conn, int fd, uint64_t count);
    get_response = conn_send_file(conn, fd, filesize);
    if (get_response == NULL) {
        get_response = &RESPONSE_OK;
    }
    //
    //audit log part
    //
    //uint16_t response_get_code(const Response_t *);
    fprintf(stderr, "GET,/%s,%d,%s\n", uri, response_get_code(get_response), requestid);
    //conn_send_response(conn, get_response);//maybe do
    close(fd);
    return;
}

void handle_put(conn_t *conn) {
    // TODO: Implement PUT

    char *uri = conn_get_uri(conn);
    //debug("PUT %s", uri);
    //
    char *requestid = conn_get_header(conn, "Request-Id");
    if (requestid == NULL) {
        requestid = "0";
    }
    //
    int flag = 0; // if file exists
    const Response_t *put_response = NULL;
    pthread_mutex_lock(&bocchithelock);
    int fd = open(uri, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        if (errno == ENOENT) {
            flag = 1;
            fd = open(uri, O_CREAT | O_WRONLY | O_TRUNC, 0664);
            // put_response = &RESPONSE_NOT_FOUND;
        } else if (errno == EACCES || errno == EISDIR) {
            put_response = &RESPONSE_FORBIDDEN;
            pthread_mutex_unlock(&bocchithelock);
            fprintf(stderr, "PUT,/%s,%d,%s\n", uri, response_get_code(put_response), requestid);
            conn_send_response(conn, put_response);
            close(fd);
            return;
        } else {
            put_response = &RESPONSE_INTERNAL_SERVER_ERROR;
            pthread_mutex_unlock(&bocchithelock);
            fprintf(stderr, "PUT,/%s,%d,%s\n", uri, response_get_code(put_response), requestid);
            conn_send_response(conn, put_response);
            close(fd);
            return;
        }
    }
    flock(fd, LOCK_EX);
    pthread_mutex_unlock(&bocchithelock);
    //const Response_t *conn_recv_file(conn_t *conn, int fd);
    put_response = conn_recv_file(conn, fd);
    if (put_response == NULL && flag == 0) {
        put_response = &RESPONSE_OK;
    } else if (put_response == NULL && flag == 1) {
        put_response = &RESPONSE_CREATED;
    }
    //
    //audit log part
    //
    //uint16_t response_get_code(const Response_t *);
    //const Response_t *conn_send_response(conn_t *conn, const Response_t *res);
    fprintf(stderr, "PUT,/%s,%d,%s\n", uri, response_get_code(put_response), requestid);
    conn_send_response(conn, put_response);
    //hi
    close(fd);
    return;
}

void handle_unsupported(conn_t *conn) {
    //debug("Unsupported request");
    conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
    char *requestid = conn_get_header(conn, "Request-Id");
    if (requestid == NULL) {
        requestid = "0";
    }
    //const char *request_get_str(const Request_t *);
    //const Request_t *conn_get_request(conn_t *conn);
    //const Response_t *conn_send_response(conn_t *conn, const Response_t *res);
    fprintf(stderr, "%s,/%s,%d,%s\n", request_get_str(conn_get_request(conn)), conn_get_uri(conn),
        response_get_code(&RESPONSE_NOT_IMPLEMENTED), requestid);
    return;
}
