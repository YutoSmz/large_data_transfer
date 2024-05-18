/*
    *  server.c
    *
    *  Created on: 2024/5/18
    *  Written by: Yuto Shimizu
*/

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#define BUFSIZE 4096
#define FILE_EXTENSION ".dat"
#define FILE_PATH "./received_files"
#define HEADER_SIZE 16  // same size as the header in client.c

// prototype declaration
int server_socket(const char *portnm);
void accept_loop(int soc);
ssize_t recv_file(int soc, FILE *fp, size_t file_size);
ssize_t recv_header(int acc, size_t *file_size);
char *make_file_path(int file_num);
static void make_dir(const char *dir_name);


// main function
int main(int argc, char *argv[]) {
    int soc;

    if (argc <= 1 || strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "-h") == 0) {
        (void) fprintf(stderr, "[USAGE] $ ./server [portnumber]\n");
        (void) fprintf(stderr, "[EXAMPLE] $ ./server 50000\n");
        (void) fprintf(stderr, "[NOTE]\n    The server must be run on another terminal before the client can be run.\n");
        exit(EX_USAGE);
    }

    make_dir(FILE_PATH);

    if ((soc = server_socket(argv[1])) == -1) {
        (void) fprintf(stderr, "[ERROR] server_socket()\n");
        return (EX_UNAVAILABLE);
    }
    (void) fprintf(stderr, "ready for accept\n");

    accept_loop(soc);

    (void) close(soc);
    return (EX_OK);
}



int server_socket(const char *portnm) {
    char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct addrinfo hints, *res;
    int soc, opt, errcode;
    socklen_t opt_len;

    (void) memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((errcode = getaddrinfo(NULL, portnm, &hints, &res)) != 0) { 
        (void) fprintf(stderr, "[ERROR] getaddrinfo():%s\n", gai_strerror(errcode));
        return (-1);
    }
    if ((errcode = getnameinfo(res->ai_addr, res->ai_addrlen, nbuf, sizeof(nbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
        (void) fprintf(stderr, "[ERROR] getnameinfo():%s\n", gai_strerror(errcode));
        freeaddrinfo(res);
        return (-1);
    }

    if ((soc = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
        (void) fprintf(stderr, "[ERROR] socket(): %s\n", strerror(errno));
        freeaddrinfo(res);
        return (-1);
    }

    opt = 1;
    opt_len = sizeof(opt);
    if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR, &opt, opt_len) == -1) {
        (void) fprintf(stderr, "[ERROR] setsockopt(): %s\n", strerror(errno));
        (void) close(soc);
        freeaddrinfo(res);
        return (-1);
    }

    if (bind(soc, res->ai_addr, res->ai_addrlen) == -1) {
        (void) fprintf(stderr, "[ERROR] bind(): %s\n", strerror(errno));
        (void) close(soc);
        freeaddrinfo(res);
        return (-1);
    }

    if (listen(soc, SOMAXCONN) == -1) {
        (void) fprintf(stderr, "[ERROR] listen(): %s\n", strerror(errno));
        (void) close(soc);
        freeaddrinfo(res);
        return (-1);
    }
    freeaddrinfo(res);
    return (soc);
}



void accept_loop(int soc) {
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct sockaddr_storage from;
    int acc;
    socklen_t len;
    FILE *fp;
    int file_num;
    size_t file_size;
    ssize_t received_file_size;


    for (file_num = 1 ;;) { // endless loop
        len = (socklen_t) sizeof(from);
        (void) fprintf(stderr, "# waiting for connection ...\n");
        if ((acc = accept(soc, (struct sockaddr *) &from, &len)) == -1) {
            if (errno != EINTR) {
                perror("accept");
            }
        }else{
            (void) getnameinfo((struct sockaddr *) &from, len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
            (void) fprintf(stderr, "client addr: %s, port: %s\n", hbuf, sbuf);

            // receive the header information
            recv_header(acc, &file_size);
            if (file_size == 0) {
                (void) fprintf(stderr, "[ERROR] The file is not sent by the client.\n");
                (void) close(acc);
                acc = 0;
                continue;
            }

            // open the file discriptor
            if ((fp = fopen(make_file_path(file_num), "w")) == NULL) {
                (void) fprintf(stderr, "[ERROR] fopen(): %s\n", strerror(errno));
                (void) close(acc);
                acc = 0;
                continue;
            }

            // receive the file
            if ((received_file_size = recv_file(acc, fp, file_size)) == -1) {
                (void) fprintf(stderr, "[ERROR] recv_file()\n");
                (void) close(acc);
                fclose(fp);
                acc = 0;
                continue;
            } else if (received_file_size != file_size) {
                (void)
                (void) fprintf(stderr, "[ERROR] The file size (%zd) is not correct (file number: %03d).\n", received_file_size, file_num);
                // [NOTE 20240518]
                // recv_file()でのfwrite()で、要求したバイト数分の書き込みができなかった時などに、このエラーになる（と思う）。
                // 本当は、fwite()での書き込みバイト数に不備があった場合は、再度その部分だけ書き込みする必要がある。
                // 暇があれば、その処理を追加する。
            } else {
                (void) fprintf(stderr, "[INFO] The file is received successfully (file number: %03d).\n", file_num);
            }

            (void) close(acc);
            fclose(fp);
            file_num++;
            acc = 0;
        }
    }
}


ssize_t recv_file(int soc, FILE *fp, size_t file_size) {
    char buf[BUFSIZE];
    size_t len, size;
    ssize_t total = 0, received, written;

    (void) memset(buf, '\0', sizeof(buf));
    size = sizeof(buf[0]);

    while (total < file_size) {
        len = MIN(BUFSIZE, (file_size - total));

        if ((received = recv(soc, buf, len, 0)) == -1) {
            (void) fprintf(stderr, "[ERROR] recv(): %s\n", strerror(errno));
            return (-1);
        }
        if (received == 0) {
            break;
        }
        written = fwrite(buf, size, received, fp);
        if (written == 0) {
            (void) fprintf(stderr, "[ERROR] fwrite(): %s\n", strerror(errno));
            return (-1);
        }
        total += received;
    }
    return (total);
}



ssize_t recv_header(int acc, size_t *file_size) {
    char buf[HEADER_SIZE];
    size_t size;
    ssize_t total = 0, received;

    (void) memset(buf, '\0', sizeof(buf));

    while (total < HEADER_SIZE) {
        size = MIN(HEADER_SIZE, HEADER_SIZE - total);
        if ((received = recv(acc, buf, size, 0)) == -1) {
            (void) fprintf(stderr, "[ERROR] recv(): %s\n", strerror(errno));
            return (-1);
        }
        if (received == 0) { // client closes the connection
            *file_size = 0;
            break;
        }
        total += received;
    }
    *file_size = atoi(buf); // If input data cannot be converted to an int value, atoi() returns 0
    fprintf(stderr, "[INFO] file size: %ld bytes\n", *file_size);
    return (total);
}



char *make_file_path(int file_num) {
    char *file_name = (char *)calloc(256, sizeof(char));
    if (snprintf(file_name, 256, "%s/file_%03d%s", FILE_PATH, file_num, FILE_EXTENSION) < 0) {
        (void) fprintf(stderr, "[ERROR] snprintf(): %s\n", strerror(errno));
        snprintf(file_name, 256, "./file_000.txt");
    }
    return file_name;
}



static void make_dir(const char *dir_name) {
    DIR *dir;

    if ((dir = opendir(dir_name)) == NULL) {
        if (errno == ENOENT) { // No such file or directory
            if (mkdir(dir_name, 0777) == -1) {
                (void) fprintf(stderr, "[ERROR] mkdir(): %s\n", strerror(errno));
                exit(EX_UNAVAILABLE);
            }
        } else {
            (void) fprintf(stderr, "[ERROR] opendir(): %s\n", strerror(errno));
            exit(EX_UNAVAILABLE);
        }
    } else {
        (void) closedir(dir);
    }
}