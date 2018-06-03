#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>

#include <arpa/inet.h>

#include "requestState.h"

#include "../connection-structure.h"
#include "../proxyStm.h"
#include "../proxyActiveHandlers.h"
#include "../../utils/buffer/buffer.h"
#include "../../parser/request.h"

//int hasOrigin = 0, isDone = 0; /* DEBBUGING */

int parserHasOrigin(struct request_parser parser) {
    return parser.hasDestination;
}

int parserIsRequestDone(struct request_parser parser) {
    return parser.state == request_done;
}

/* Returns 0 on error */
int connectToOrigin(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);

    if( (conn->originFd = socket(conn->originDomain, SOCK_STREAM, 0)) == -1) {
        //handle error
        goto handle_errors;
    }

    //setsockopt(conn->originFd, SOL_SOCKET, SO_NOSIGPIPE, &(int){ 1 }, sizeof(int));

    if(selector_fd_set_nio(conn->originFd) == -1) {
        // handle error
        goto handle_errors;
    }
printf("connecting to origin server\n");
    if (connect(conn->originFd, (const struct sockaddr *) &conn->originAddr, conn->originAddrLen) == -1) {
        if(errno == EINPROGRESS) { /* Wait until connection is established */
            
            if(selector_register(key->s, conn->originFd, &connectionHandler, OP_WRITE, key->data) != SELECTOR_SUCCESS) {
                // handle error
                goto handle_errors;
            }
printf("connection will be done when I can write to it\n");
            conn->references += 1;

        } else {
            // handle errors
            goto handle_errors;
        }
    }

    return 1;


    handle_errors:
        printf("error while connecting to origin\n");
        if(conn->originFd != -1) {
            close(conn->originFd);
        }
        return 0;
}

void * solveDNS(void * data) {
    struct selector_key * key = (struct selector_key *) data;
    struct Connection * conn = DATA_TO_CONN(key);

    pthread_detach(pthread_self());

    conn->origin_resolution = 0;

    /* Taken from socks5nio.c */
    struct addrinfo hints = {
        .ai_family    = AF_UNSPEC,    /* Allow IPv4 or IPv6 */
        .ai_socktype  = SOCK_STREAM,  /* Datagram socket */
        .ai_flags     = AI_PASSIVE,   /* For wildcard IP address */
        .ai_protocol  = 0,            /* Any protocol */
        .ai_canonname = NULL,
        .ai_addr      = NULL,
        .ai_next      = NULL,
    };

    char buff[7];
    snprintf(buff, sizeof(buff), "%d", ntohs(conn->requestParser.reqParser.destintation.destPort));

    printf("[NEW THREAD] getAddrInfo\n");

    getaddrinfo(conn->requestParser.reqParser.destintation.destAddr.fqdn, buff, &hints, &conn->origin_resolution);


    printf("[NEW THREAD] notify block");
    selector_notify_block(key->s, key->fd);

    free(data);

    return 0;
}

/* returns 0 on error */
int startOriginConnection(struct selector_key * key) {
    pthread_t tid;
    struct Connection * conn = DATA_TO_CONN(key);
    struct requestData * rData = &(conn->requestParser.reqParser.destintation);
    int ret = 0;
    struct selector_key * k;

    conn->isConnectingOrigin = 1;

    switch(rData->destAddrType) {
        case IPv4:
            conn->originDomain = AF_INET;
            rData->destAddr.ipv4.sin_port = rData->destPort;
            conn->originAddrLen = sizeof(rData->destAddr.ipv4);
            memcpy(&conn->originAddr, &rData->destAddr, conn->originAddrLen);
            ret = connectToOrigin(key);
            break;
        case IPv6:
            conn->originDomain = AF_INET6;
            rData->destAddr.ipv6.sin6_port = rData->destPort;
            conn->originAddrLen = sizeof(rData->destAddr.ipv6);
            memcpy(&conn->originAddr, &rData->destAddr, conn->originAddrLen);
            ret = connectToOrigin(key);
            break;
        case DOMAIN:
            k = malloc(sizeof(*k));
            if(k == NULL) {
                return 0;
            } else {
                memcpy(k, key, sizeof(*k));
                if(pthread_create(&tid, 0, solveDNS, k) == -1) {
                    ret = 0;
                } else {
                    ret = 1;
                }
            }
            break;
    }
    
    return ret;
}

unsigned requestRead(struct selector_key * key) {
	struct Connection * conn = DATA_TO_CONN(key);
	uint8_t *ptr;
	size_t count;
	ssize_t  n;

    /* Read request and keep it in readBuffer */
	ptr = buffer_write_ptr(&conn->readBuffer, &count);
	n = recv(key->fd, ptr, count, 0);
	if(n <= 0) { // client closed connection
        return ERROR;
	}

    buffer_write_adv(&conn->readBuffer, n);
    
    request_parser_consume(&conn->requestParser.reqParser, (char *)ptr, n);

/* DEBUGING *
ptr[n] = 0;
printf("read from client:%s \n", ptr);
conn->requestParser.requestData.destAddrType = DOMAIN;
//conn->requestParser.requestData.destAddr.fqdn;
strncpy(conn->requestParser.requestData.destAddr.fqdn, "google.com", strlen("google.com")+1);
printf("FQDN is: %s\n", conn->requestParser.requestData.destAddr.fqdn);
conn->requestParser.requestData.destPort = htons(80);
hasOrigin = 1;
if(strstr(ptr, "\r\n\r\n") != NULL) {
    printf("found end of request\n");
    isDone = 1;
}
 END DEBUGING */

    if(!parserHasOrigin(conn->requestParser.reqParser)) { // si no tengo el origin y no lo puedo tener de la request
        printf("Parser needs origin\n");
        if(buffer_can_write(&conn->readBuffer)) {
            return REQUEST;
        } else {
            return ERROR;
        }
    }

    if(conn->originFd == -1 && !conn->isConnectingOrigin) { // y no estoy resolviendo todavia
    printf("attempt connection to the origin server\n");
        /* New thread to solve DNS */
        if(startOriginConnection(key) == 0) {
            printf("[ERROR] failed to connect to origin server\n");
            return ERROR;
        }
/*
        fd_interest interest = OP_NOOP;
        printf("check if client can continue sending\n");
        if(buffer_can_write(&conn->readBuffer) && !parserIsRequestDone()) { // buffer is not empty
            interest = OP_READ; // keep reading from client
            printf("read from client\n");
        }

        if(selector_set_interest_key(key, interest) != SELECTOR_SUCCESS) {
            printf("[ERROR] failed to set interest\n");
			return ERROR;
		}

        return REQUEST;
        */
    }

    /* Check if I should still listen to the client. Only if buffer is not full and the request is not done */
    fd_interest cliInterest = OP_NOOP;
    if(buffer_can_write(&conn->readBuffer) && !parserIsRequestDone(conn->requestParser.reqParser)) {
        cliInterest = OP_READ;
    }

    /* Set client interest */
    if(selector_set_interest_key(key, cliInterest) != SELECTOR_SUCCESS) { 
        return ERROR;
    }

    /* Set origin interest */
    if(conn->originFd != -1) {
        if(selector_set_interest(key->s, conn->originFd, OP_WRITE) != SELECTOR_SUCCESS) { 
            return ERROR;
        }
    }




/*

    if(parserIsRequestDone()) { // if the client send the full request
        if(selector_set_interest_key(key, OP_NOOP) != SELECTOR_SUCCESS) { // stop listenting to client
			return ERROR;
		}

        if(conn->originFd != -1) { // if I have origin connected
            // ESTO NO PASA NUNCA, ACABO DE ESCRIBIR EL BUFFER 
            if(!buffer_can_read(&conn->readBuffer)) { // buffer is empty
                if(selector_set_interest(key->s, conn->originFd, OP_READ) != SELECTOR_SUCCESS) { // prepare to read response from origin
                    return ERROR;
                }
                return RESPONSE; //change state
            
            } else { // buffer is not empty
                if(selector_set_interest(key->s, conn->originFd, OP_WRITE) != SELECTOR_SUCCESS) { // continue wrtiting to origin
                    return ERROR;
                }
            }
        }
        return REQUEST;
    }

    printf("request is not done\n");
    // If request is not done //
    // ESTO NO PASA NUNCA, ACABO DE ESCRIBIR EL BUFFER //
    if(!buffer_can_read(&conn->readBuffer)) { //buffer is empty
        if(selector_set_interest_key(key, OP_READ) != SELECTOR_SUCCESS) { // read from client
			return ERROR;
		}
        if(conn->originFd != -1) {
            if(selector_set_interest(key->s, conn->originFd, OP_NOOP) != SELECTOR_SUCCESS) { // nothing to origin
                return ERROR;
            }
        }
    } else { // buffer not empty
printf("buffer is not empty\n");
        if(conn->originFd != -1) {
            if(selector_set_interest(key->s, conn->originFd, OP_WRITE) != SELECTOR_SUCCESS) { // write to origin
                return ERROR;
            }
        }

printf("check if client can continue sending\n");
        fd_interest interest = OP_NOOP;
        if(buffer_can_write(&conn->readBuffer)) { // buffer not full
            interest = OP_READ;    
            printf("read from client\n");
        }

        if(selector_set_interest_key(key, interest) != SELECTOR_SUCCESS) { // nothing from client
            return ERROR;
        }
    }
*/
	return REQUEST;
}



unsigned requestWrite(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);
	uint8_t *ptr;
	size_t count;
	ssize_t  n;
    unsigned ret = REQUEST;

printf("request write called\n");

    /* Send bufferd data to the origin server */
    ptr = buffer_read_ptr(&conn->readBuffer, &count);
	n = send(conn->originFd, ptr, count, MSG_NOSIGNAL);
    printf("send %d\n", (int)n);

	if(n <= 0) { // origin closed connection
        printf("origin closed connection\n");
        return ERROR;
	}

    buffer_read_adv(&conn->readBuffer, n);

    printf("[SERVER] writing to origin server\n");


    /* If the request is not done enable client to read */
    if(!parserIsRequestDone(conn->requestParser.reqParser)) {
        if(selector_set_interest(key->s, conn->clientFd, OP_READ) != SELECTOR_SUCCESS) { 
            return ERROR;
        }
    }

    /* If the buffer is not empty continue writing */
    fd_interest originInterst = OP_NOOP;
    if(buffer_can_read(&conn->readBuffer)) {
        originInterst = OP_WRITE;
    }

    /* If the buffer is empty and the request is done move to response state */
    if(!buffer_can_read(&conn->readBuffer) && parserIsRequestDone(conn->requestParser.reqParser)) {
        originInterst = OP_READ;
        ret = RESPONSE;
    }

    /* Set origin fd interests */
    if(selector_set_interest_key(key, originInterst) != SELECTOR_SUCCESS) { 
        return ERROR;
    }

    return ret;
/*

    if(!buffer_can_read(&conn->readBuffer)) { //buffer is empty
        if(parserIsRequestDone()) {
            if(selector_set_interest(key->s, conn->originFd, OP_READ) != SELECTOR_SUCCESS) { // prepare to read response from origin
                return ERROR;
            }
            printf("[STATE] change to response\n");
            return RESPONSE; // change state
        }
        
        // request is not done
        if(selector_set_interest_key(key, OP_READ) != SELECTOR_SUCCESS) { // read from client the request
            return ERROR;
        }
        if(selector_set_interest(key->s, conn->originFd, OP_NOOP) != SELECTOR_SUCCESS) { // wait until there is something in the buffer to send to origin
            return ERROR;
        }
    
    } else { // buffer is not empty
        if(selector_set_interest(key->s, conn->originFd, OP_WRITE) != SELECTOR_SUCCESS) { // write again to the origin server
            return ERROR;
        }

        fd_interest interest = OP_NOOP;
        if(!parserIsRequestDone()) { // request not done
            interest = OP_READ; // continue reading the request
        }

        if(selector_set_interest_key(key, interest) != SELECTOR_SUCCESS) { // read from client the request
            return ERROR;
        }
    }

    return REQUEST;*/
}

unsigned requestBlockReady(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);

    printf("Block notified\n");

    if(conn->origin_resolution == 0) {
        // resolution failed
        printf("DNS failed\n");
        return ERROR;
    }

    /* Save resolution */
    conn->originDomain = conn->origin_resolution->ai_family;
    conn->originAddrLen = conn->origin_resolution->ai_addrlen;
    memcpy(&conn->originAddr, conn->origin_resolution->ai_addr, conn->originAddrLen);
    freeaddrinfo(conn->origin_resolution);
    conn->origin_resolution = 0;

printf("attempt connect to origin\n");

    if( !connectToOrigin(key) ) {
        printf("[ERROR] connection to origin failed\n");
        return ERROR;
    }
    
    /* What to do next? */

    /* Write buffered request to the origin server */
    if(selector_set_interest(key->s, conn->originFd, OP_WRITE) != SELECTOR_SUCCESS) {
        printf("[ERROR] failed to set interest to origin fd\n");
        return ERROR;
    }

    /* If there is more request to read from the client, and I have space */
/*    if(!parserIsRequestDone() && buffer_can_write(&conn->readBuffer)) {
        if(selector_set_interest(key->s, conn->clientFd, OP_READ) != SELECTOR_SUCCESS) {
            return ERROR;
        }
    }
*/
printf("still in request waiting to connect to origin server\n");

    return REQUEST;
}

void requestArrival(const unsigned state, struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);
    request_parser_init(&conn->requestParser.reqParser);
}

void requestDeparture(const unsigned state, struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);
    request_parser_close(&conn->requestParser.reqParser);
}