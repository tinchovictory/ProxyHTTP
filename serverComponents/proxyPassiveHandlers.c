#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#include "proxyPassiveHandlers.h"
#include "proxyActiveHandlers.h"
#include "proxyStm.h"
#include "maxFdHandler.h"
#include "transformationManager.h"

#include "../utils/buffer/buffer.h"
#include "../logger/logger.h"
#include "metrics.h"
#include "transformationFork.h"


#define MAX_POOL 50
#define LOG_BUFF_SIZE 256

static int poolSize = 0;
static struct Connection * pool = NULL;

struct Connection * new_connection(const int clientFd) {
	struct Connection * connection;

	if(pool == NULL) {
		connection = malloc(sizeof(* connection));
	} else {
		connection = pool;
		pool = pool->next;
		poolSize--;
		connection->next = NULL;
	}

	if(connection == NULL) {
		return NULL;
	}

	/* Initialize connection to 0 */
	memset(connection, 0x00, sizeof(*connection));

	connection->clientFd = clientFd;
	connection->originFd = -1;

	connection->stm.initial = REQUEST;
	connection->stm.max_state = FATAL_ERROR;
	connection->stm.states = getProxyStates();
	stm_init(&connection->stm);

	buffer_init(&connection->readBuffer,  N(connection->rawBuff_a), connection->rawBuff_a);
    buffer_init(&connection->writeBuffer, N(connection->rawBuff_b), connection->rawBuff_b);

	buffer_init(&connection->respTempBuffer, N(connection->rawBuff_c), connection->rawBuff_c);
	
	/* transformation */
	if(hasTransformation()) {
		connection->transformationType = TRANSFORM;
	} else {
		connection->transformationType = NO_TRANSFORM;
	}
	
	connection->transformationPid = -1;
	connection->readTransformFd = -1;
	connection->writeTransformFd = -1;

	if(connection->transformationType != NO_TRANSFORM) {
		buffer_init(&connection->inTransformBuffer, N(connection->rawBuff_d), connection->rawBuff_d);
		buffer_init(&connection->outTransformBuffer, N(connection->rawBuff_e), connection->rawBuff_e);

		connection->mediaTypesList = getMediaTypesList();

		/* Fork transformation process and create pipes */
		loggerWrite(DEBUG, "Fork transformation process\n");

		connection->transformationPid = forkTransformation(&connection->readTransformFd, &connection->writeTransformFd);
		if(connection->transformationPid == -1) {
			return NULL;
		}

		loggerWrite(DEBUG, "Done fork\n");
	}

	/* Still no error */
	connection->errorCode = NO_ERROR;
	
	connection->references = 1;

	/* Save to metrics */
	addClient();

	return connection;
}

void freeConnection(struct Connection * connection) {
	// free origin_resolution
	free(connection);
}

void closeAndUnregister(struct selector_key * key, int fd) {
	selector_unregister_fd(key->s, fd);
	/* fd is closed by connection_close handler */
}

/* removes a connection, if there are < MAX_POOL its added to the pool, otherwise it frees the memory */
void destroy_connection(struct selector_key * key) {
	struct Connection * connection = DATA_TO_CONN(key);
	if(connection == NULL) {
		return;
	}

	/* Save to metrics */
	removeClient();

	// Close transformation process 
	if(connection->transformationPid != -1) {
		kill(connection->transformationPid, SIGKILL);
	}

	if(connection->clientFd != -1) {
		closeAndUnregister(key, connection->clientFd);
		connection->clientFd = -1;
	}

	if(connection->originFd != -1) {
		closeAndUnregister(key, connection->originFd);
		connection->originFd = -1;
	}

	if(connection->readTransformFd != -1) {
		closeAndUnregister(key, connection->readTransformFd);
		connection->readTransformFd = -1;
	}

	if(connection->writeTransformFd != -1) {
		closeAndUnregister(key, connection->writeTransformFd);
		connection->writeTransformFd = -1;
	}

	/* Free media types list */
	if(connection->mediaTypesList != NULL) {
		free(connection->mediaTypesList);
	}

	if(connection->references > 1) {
		connection->references -= 1;
		return;
	}

	if(poolSize < MAX_POOL) {
		connection->next = pool;
		pool = connection;
		poolSize++;
	} else {
		freeConnection(connection);
	}
}

void logNewConnection(struct Connection * conn) {
    char buff[LOG_BUFF_SIZE + 1] = {0};
    char addr[INET_ADDRSTRLEN + 1] = {0};

    /* Get addres */
    inet_ntop(conn->clientAddr.ss_family, &(((struct sockaddr_in *)&conn->clientAddr)->sin_addr), addr, INET_ADDRSTRLEN);

	sprintf(buff, "\x1b[32m[INFO]\x1b[0m New client connection - client: %s\n", addr);
    loggerWrite(PRODUCTION, buff);
}


void proxyPassiveAccept(struct selector_key *key) {
	struct sockaddr_storage clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
	struct Connection * connection = NULL;
	struct selector_key errorKey;

	keepSpareFd();

	const int clientFd = accept(key->fd, (struct sockaddr*) &clientAddr, &clientAddrLen);
    if(clientFd == -1) {
		if(errno == EMFILE) {
			/* There are no available fd so close connection */
			handleAcceptEmfile(key->fd, (struct sockaddr*) &clientAddr, &clientAddrLen);
		}
        goto handle_errors;
    }
    if(selector_fd_set_nio(clientFd) == -1) {
        goto handle_errors;
    }

	connection = new_connection(clientFd);
	if(connection == NULL) {
		goto handle_errors;
	}
	memcpy(&connection->clientAddr, &clientAddr, clientAddrLen);
    connection->clientAddrLen = clientAddrLen;

	/* register client fd in selector */
	loggerWrite(DEBUG, "Register clientFd\n");

	if(selector_register(key->s, clientFd, &connectionHandler, OP_READ, connection) != SELECTOR_SUCCESS) {
		goto handle_errors;
	}
	/* register transformation fds */
	if(connection->transformationType != NO_TRANSFORM) {
		if(selector_fd_set_nio(connection->readTransformFd) == -1 || selector_fd_set_nio(connection->writeTransformFd) == -1) {
			goto handle_errors;
		}

		if(selector_register(key->s, connection->readTransformFd, &connectionHandler, OP_NOOP, connection) != SELECTOR_SUCCESS) {
			goto handle_errors;
		}
		
		selector_status status;
		if((status = selector_register(key->s, connection->writeTransformFd, &connectionHandler, OP_NOOP, connection)) != SELECTOR_SUCCESS) {
			goto handle_errors;
		}
	}

	/* Log client connection */
	logNewConnection(connection);

	return;

	/* Used only as exception handler */
	handle_errors:

		if(connection == NULL && clientFd != -1) {
			//it was never registerd to the selector
			close(clientFd);
			return;
		}

		errorKey.s = key->s;
		errorKey.fd = key->fd;
		errorKey.data = connection;

		destroy_connection(&errorKey);
}
