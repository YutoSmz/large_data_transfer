/*
    *  client.c
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
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#define BUFSIZE 4096
#define FILE_PATH "./1MB_file"
#define HEADER_SIZE 16  // same size as the header in server.c

// prototype declaration
int client_socket(const char *hostnm, const char *portnm);
ssize_t send_file(int s, FILE *fp, size_t fp_size);
ssize_t send_header(int s, size_t file_size);
size_t get_file_size(const char *path);


// main function
int main(int argc, char *argv[]) {
    int soc;
    ssize_t sent_file_size;
    FILE *fp;
    size_t file_size;

    if (argc <= 2 || strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "-h") == 0) {
        (void) fprintf(stderr, "[USAGE] $ ./client [server-hostname] [server-portnumber]\n");
        exit(EX_USAGE);
    }

    if ((soc = client_socket(argv[1], argv[2])) == -1) {
        (void) fprintf(stderr, "[ERROR] client_socket()\n");
        exit(EX_UNAVAILABLE);
    }

    if ((fp = fopen(FILE_PATH, "r")) == NULL) {
        (void) fprintf(stderr, "[ERROR] fopen(): %s\n", strerror(errno));
        exit(EX_NOINPUT);
    }
    file_size = get_file_size(FILE_PATH);
    
    // send header information
    send_header(soc, file_size);


    if ((sent_file_size = send_file(soc, fp, file_size)) == -1) {
        (void) fprintf(stderr, "[ERROR] send_file()\n");
    }

    fclose(fp);
    (void) close(soc);
    return (0);
}



int client_socket(const char *hostnm, const char *portnm) {
    char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct addrinfo hints, *res;
    int soc, errcode;

    (void) memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((errcode = getaddrinfo(hostnm, portnm, &hints, &res)) != 0) {
        (void) fprintf(stderr, "[ERROR] getaddrinfo(): %s\n", gai_strerror(errcode));
        return (-1);
    }
    if ((errcode = getnameinfo(res->ai_addr, res->ai_addrlen, nbuf, sizeof(nbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
        (void) fprintf(stderr, "[ERROR] getnameinfo(): %s\n", gai_strerror(errcode));
        freeaddrinfo(res);
        return (-1);
    }

    if ((soc = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
        (void) fprintf(stderr, "[ERROR] socket(): %s\n", strerror(errno));
        freeaddrinfo(res);
        return (-1);
    }

    if (connect(soc, res->ai_addr, res->ai_addrlen) == -1) {
        (void) fprintf(stderr, "[ERROR] connect(): %s\n", strerror(errno));
        (void) close(soc);
        freeaddrinfo(res);
        return (-1);
    }
    freeaddrinfo(res);
    return (soc);
}



ssize_t send_file(int soc, FILE *fp, size_t fp_size) {
    char buf[BUFSIZE];
    size_t len, size, nmemb;
    ssize_t total = 0, sent;

    (void) memset(buf, '\0', sizeof(buf));
    size = sizeof(buf[0]);
    
    while (total < fp_size) {
        nmemb = MIN( sizeof(buf)/size, (fp_size - total)/size );

        if ((len = fread(buf, size, nmemb, fp)) != 0) { 
            if ((sent = send(soc, buf, len*size, 0)) == -1) {
                (void) fprintf(stderr, "[ERROR] send(): %s\n", strerror(errno));
                return (-1);
            }
        } else {
            if ( ferror(fp) ) {
                (void) fprintf(stderr, "[ERROR] Error reading file: %s\n", strerror(errno));
                return (-1);
            } else if ( feof(fp) ) {
                (void) fprintf(stdout, "[INFO] EOF found\n");
            }
        }

        total += sent;
    }
    (void) fprintf(stdout, "[INFO] Sent %ld bytes\n", total);
    return (total);
}


ssize_t send_header(int soc, size_t file_size) {
    char header[HEADER_SIZE];
    size_t size;
    ssize_t total = 0, sent;
    char format[20];

    snprintf(format, sizeof(format), "%%0%dd", HEADER_SIZE - 1);
    sprintf(header, format, file_size);

     while (total < HEADER_SIZE) {
        size = MIN(HEADER_SIZE, HEADER_SIZE - total);
        if ((sent = send(soc, header, size, 0)) == -1) {
            (void) fprintf(stderr, "[ERROR] send(): %s\n", strerror(errno));
            return (-1);
        }
        total += sent;
     }
    (void) fprintf(stdout, "[INFO] Sent header\n");
    return (total);
}



size_t get_file_size(const char *path) {
    struct stat st;

    if (stat(path, &st) == -1) {
        (void) fprintf(stderr, "[ERROR] stat(): %s\n", strerror(errno));
        return (0);
    }
    return ((size_t)st.st_size);
}