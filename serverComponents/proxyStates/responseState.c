#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>

#include <arpa/inet.h>

#include "responseState.h"

#include "../connection-structure.h"
#include "../proxyStm.h"
#include "../proxyActiveHandlers.h"
#include "../../utils/buffer/buffer.h"

/* Esto va en response parser */
enum parserState {
    METHOD,
    HEADERS,
    BODY,
    PARSER_DONE,
    PARSER_ERROR
};
/* End */

int pareserResponseIsDone() {
    return 0;
}

enum parserState parser_consume(char * ptrToParse, int * bytesToParse, char * ptrFromParse, int * parsedBytes) {
    memcpy(ptrFromParse, ptrToParse, *bytesToParse);
    *parsedBytes = *bytesToParse;

    printf("parser_consume()\n");
    
    return BODY;
}

void chunkBytes(char * ptrToChunk, int * bytesToChunk, char * ptrFromChunk, int * chunkedBytes) {
    memcpy(ptrFromChunk, ptrToChunk, *bytesToChunk);
    *chunkedBytes = * bytesToChunk;

    printf("chunking bytes\n");

}

int min(int val1, int val2) {
    if(val1 < val2) {
        return val1;
    }
    return val2;
}

enum parserState copyTempToWriteBuff(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);
	uint8_t * ptrToParse, * ptrFromParse;
	size_t maxTempBuffSize, maxWriteBuffSize;
	int  parsedBytes = 0, bytesToParse = 0;
    enum parserState state;

    /* parse string from buffer_read_ptr(&conn->respTempBuffer, &maxTempBuffSize) */
    ptrToParse = buffer_read_ptr(&conn->respTempBuffer, &maxTempBuffSize);
    printf("maxTempBuffSize: %d\n", (int) maxTempBuffSize);

    /* leave parsed data in buffer_write_ptr(&conn->writeBuffer, &maxWriteBuffSize) */
    ptrFromParse = buffer_write_ptr(&conn->writeBuffer, &maxWriteBuffSize);
    printf("maxWriteBuffSize: %d\n", (int) maxWriteBuffSize);

    /* parse string size is min(maxWriteBuffSize, maxTempBuffSize) */
    /* leave bytes parsed in parsedBytes */
    bytesToParse = min(maxWriteBuffSize, maxTempBuffSize);
    printf("min: %d\n", bytesToParse);
    state = parser_consume(ptrToParse, &bytesToParse, ptrFromParse, &parsedBytes);

    printf("parsedBytes: %d\n", parsedBytes);
    printf("bytesToParse: %d\n", bytesToParse);

    /* move temp buffer pointer accoring to parsedBytes */
    buffer_read_adv(&conn->respTempBuffer, bytesToParse);

    /* move write buffer pointer according to parsedBytes */
    buffer_write_adv(&conn->writeBuffer, parsedBytes);

    /* set interest */
    /* If I wrote to writeBuffer, clientFd OP_WRITE */
    fd_interest interest = OP_NOOP;
    if(buffer_can_read(&conn->writeBuffer)) {
        printf("Set OP_WRITE to client\n");
        /* Write response to client */
        interest = OP_WRITE;
    }
    if(selector_set_interest(key->s, conn->clientFd, interest) != SELECTOR_SUCCESS) {
        return PARSER_ERROR;
    }

        
    /* If respTempBuffer is not full and response is not done originFd OP_READ */
    interest = OP_NOOP;
    if(buffer_can_write(&conn->respTempBuffer) && !pareserResponseIsDone()) {
        printf("Set OP_READ to origin\n");
        interest = OP_READ;
    }
    if(selector_set_interest(key->s, conn->originFd, interest) != SELECTOR_SUCCESS) {
        return PARSER_ERROR;
    }

    /* save parser return status */
    return state;
}

enum parserState copyTempToTransformBuff(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);
	uint8_t * ptrToParse, * ptrFromParse;
	size_t maxTempBuffSize, maxWriteBuffSize;
	int  parsedBytes = 0, bytesToParse = 0;
    enum parserState state;

    printf("copyTempToTransformBuff()\n");

    /* parse string from buffer_read_ptr(&conn->respTempBuffer, &maxTempBuffSize) */
    ptrToParse = buffer_read_ptr(&conn->respTempBuffer, &maxTempBuffSize);
    printf("maxTempBuffSize: %d\n", (int) maxTempBuffSize);

    /* leave parsed data in buffer_write_ptr(&conn->inTransformBuffer, &maxWriteBuffSize) */
    ptrFromParse = buffer_write_ptr(&conn->inTransformBuffer, &maxWriteBuffSize);
    printf("maxWriteBuffSize: %d\n", (int) maxWriteBuffSize);

    /* parse string size is min(maxWriteBuffSize, maxTempBuffSize) */
    /* leave bytes parsed in parsedBytes */
    bytesToParse = min(maxTempBuffSize, maxWriteBuffSize);
    printf("min: %d\n", bytesToParse);
    state = parser_consume(ptrToParse, &bytesToParse, ptrFromParse, &parsedBytes);

    /* move temp buffer pointer accoring to parsedBytes */
    buffer_read_adv(&conn->respTempBuffer, bytesToParse);

    /* move transform buffer pointer according to parsedBytes */
    buffer_write_adv(&conn->inTransformBuffer, parsedBytes);

    /* set interest */
    /* If I wrote to transformBuffer, writeTransformFd OP_WRITE */
    fd_interest interest = OP_NOOP;
    if(buffer_can_read(&conn->inTransformBuffer)) {
        printf("parsed bytes > 1, set OP_WRITE to writeTransformFd\n");
        /* Write response to transform */
        interest = OP_WRITE;
    }
    if(selector_set_interest(key->s, conn->writeTransformFd, interest) != SELECTOR_SUCCESS) {
        return PARSER_ERROR;
    }
        
    /* If respTempBuffer is not full and response is not done originFd OP_READ */
    interest = OP_NOOP;
    if(buffer_can_write(&conn->respTempBuffer) && !pareserResponseIsDone()) {
        printf("respTempBuff not full, set OP_READ to originFd\n");
        interest = OP_READ;
    }
    if(selector_set_interest(key->s, conn->originFd, interest) != SELECTOR_SUCCESS) {
        return PARSER_ERROR;
    }
    
    /* save parser return status */
    return state;
}

/* return 0 on error */
int copyTransformToWriteBuffer(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);
	uint8_t * ptrToChunk, * ptrFromChunk;
	size_t maxTransformBuffSize, maxWriteBuffSize;
	int  chunkedBytes = 0, bytesToChunk = 0;

    printf("copyTransformToWriteBuffer()\n");

    /* chunk string from buffer_read_ptr(&conn->outTransformBuff, &maxTransformBuffSize) */
    ptrToChunk = buffer_read_ptr(&conn->outTransformBuffer, &maxTransformBuffSize);

    /* leave chunked data in buffer_write_ptr(&conn->writeBuffer, &maxWriteBuffSize) */
    ptrFromChunk = buffer_write_ptr(&conn->writeBuffer, &maxWriteBuffSize);

    /* chunk string size is min(maxTransformBuffSize, maxWriteBuffSize) */
    /* leave amount of bytes chunked in chunkedBytes */
    bytesToChunk = min(maxTransformBuffSize, maxWriteBuffSize);
    chunkBytes(ptrToChunk, &bytesToChunk, ptrFromChunk, &chunkedBytes);

    /* move temp buffer pointer accoring to chunkedBytes */
    buffer_read_adv(&conn->outTransformBuffer, bytesToChunk);

    /* move transform buffer pointer according to chunkedBytes */
    buffer_write_adv(&conn->writeBuffer, chunkedBytes);

    /* set interest */
    /* If I wrote to writeBuffer, clientFd OP_WRITE */
    fd_interest interest = OP_NOOP;
    if(buffer_can_read(&conn->writeBuffer)) {
        /* Write response to transform */
        printf("Write transform to response: set clientFd OP_WRITE\n");
        interest = OP_WRITE;
    }
    if(selector_set_interest(key->s, conn->clientFd, interest) != SELECTOR_SUCCESS) {
        return 0;
    }
        
    /* If outTransformBuffer is not full and missing bytes readTransformFd OP_READ */
    interest = OP_NOOP;
    if(buffer_can_write(&conn->outTransformBuffer) && conn->readTransformFd != -1) {
        printf("set readTransformFd OP_READ\n");
        interest = OP_READ;
    }
    if(selector_set_interest(key->s, conn->readTransformFd, interest) != SELECTOR_SUCCESS) {
        return 0;
    }

    return 1;
}

unsigned readFromOrigin(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);
	uint8_t *ptr;
	size_t count;
	ssize_t  n;
    enum parserState state = BODY;// buscarlo con funcion del parser
    
    /* Read from origin and save in temp buffer */
	ptr = buffer_write_ptr(&conn->respTempBuffer, &count);
	n = recv(key->fd, ptr, count, 0);
	if(n <= 0) {
        /* origin close connection */
        printf("[ERROR] {response} recv got 0 bytes\n");
        return ERROR;
	}
    buffer_write_adv(&conn->respTempBuffer, n);

    printf("[RESPONSE] got response from origin. Size: %d\n", (int) n);

    /* If im not in body write to writeBuffer */
    /* Always write to writeBuffer if there is no transformation */
    if(conn->trasformationType == NO_TRANSFORM || state == METHOD || state == HEADERS) {
        printf("NO_TRANSFROM -> copyTempToWrite()\n");
        state = copyTempToWriteBuff(key);
        if(state == PARSER_ERROR) {
            return ERROR;
        }
    }

    /* If im in body write to inTransformBuffer */
    if(state == BODY) {
        if(conn->trasformationType == NO_TRANSFORM) {
            state = copyTempToWriteBuff(key);
        } else {
            state = copyTempToTransformBuff(key);
        }
        if(state == PARSER_ERROR) {
            return ERROR;
        }
    }
    printf("Done origin read\n");

    return RESPONSE;
}

unsigned readFromTranformation(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);
	uint8_t *ptr;
	size_t count;
	ssize_t  n;

    printf("start read from transformation\n");

    /* Read from transformation and save in out transform buffer */
	ptr = buffer_write_ptr(&conn->outTransformBuffer, &count);
	n = read(key->fd, ptr, count);
    if(n == 0) {
        /* transformation closed connection */
        close(conn->readTransformFd);
        conn->readTransformFd = -1;
    }
	if(n < 0) {
        printf("[ERROR] {transformation} recv got 0 bytes\n");
        return ERROR;
	}
    buffer_write_adv(&conn->outTransformBuffer, n);

    printf("read from transformation %d bytes\n", (int) n);

    printf("[RESPONSE] got response from origin. Size: %d\n", (int) n);

    if(!copyTransformToWriteBuffer(key)) {
        return ERROR;
    }

    return RESPONSE;
}

unsigned responseRead(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);

    printf("RESPONSE READ\n");

    /* Check which fd is ready to read (originFd, readTransformFd) */
    if(key->fd == conn->originFd) {
        /* There is data to read form the origin server */
        return readFromOrigin(key);
    } else if(key->fd == conn->readTransformFd) {
        /* Thers is data to read form the transformation */
        return readFromTranformation(key);
    } else {
        /* Error */
        return ERROR;
    }
}

unsigned writeToClient(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);
	uint8_t *ptr;
	size_t count;
	ssize_t  n;
    enum parserState state = HEADERS;// buscarlo con funcion del parser
    enum parserState originalState = state;

    printf("Write to client\n");

    /* Send bufferd data to the client */
	ptr = buffer_read_ptr(&conn->writeBuffer, &count);
	n = send(key->fd, ptr, count, MSG_NOSIGNAL);
	if(n <= 0) { // transformation closed connection
        printf("[ERROR] {response} send got 0 bytes\n");
        return ERROR;
	}
    buffer_read_adv(&conn->writeBuffer, n);

    printf("%d bytes send to client\n", (int) n);

    /* Copy from temp if it's on headers or no transformation */
    if(conn->trasformationType == NO_TRANSFORM || state == HEADERS || state == METHOD) {

        printf("CopyTempToWriteBuff()\n");

        state = copyTempToWriteBuff(key);
        if(state == PARSER_ERROR) {
            return PARSER_ERROR;
        }
    }

    /* I have not enterd in the above if and I'm in the body */
    if(conn->trasformationType != NO_TRANSFORM && originalState == BODY) {
        printf("copyTransformToWriteBuffer() since im in body\n");
        if(!copyTransformToWriteBuffer(key)) {
            return ERROR;
        }
    }

    /* If the parser changed state to body and there is no transformation */
    if(conn->trasformationType != NO_TRANSFORM && originalState != BODY && state == BODY) {
        if(copyTempToTransformBuff(key) == PARSER_ERROR) {
            return ERROR;
        }
    }

    /* Fix parser stop reading when changing to body */
    if(conn->trasformationType == NO_TRANSFORM && state == BODY) {
        printf("fix parser stop when changing to body \n");
        if(copyTempToWriteBuff(key) == PARSER_ERROR) {
            return ERROR;
        }
    }

    /* Set interests */
    /* If writeBuffer is not empty set me on WRITE */
    fd_interest interest = OP_NOOP;
    if(buffer_can_read(&conn->writeBuffer)) {
        printf("writeBuff is not empty set OP_WRITE to client\n");
        interest = OP_WRITE;
    }
    if(selector_set_interest_key(key, interest) != SELECTOR_SUCCESS) {
        return ERROR;
    }

    /* Check if it's done */
    if(pareserResponseIsDone() && !buffer_can_read(&conn->writeBuffer) && conn->readTransformFd == -1) {
        return DONE;
    }
    
    return RESPONSE;
}

unsigned writeToTransformation(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);
	uint8_t *ptr;
	size_t count;
	ssize_t  n;

    printf("write to transformation\n");

    /* Send bufferd data to the client */
	ptr = buffer_read_ptr(&conn->inTransformBuffer, &count);
	//n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    printf("count is: %d\n", (int) count);
    if(count == 0) {
        //abort();
        printf("ACAAAA\n");
        //exit(0);
    }
    n = write(key->fd, ptr, count);
	if(n <= 0) { // transformation closed connection
        printf("[ERROR] {response} send got 0 bytes\n");
        return ERROR;
	}
    buffer_read_adv(&conn->inTransformBuffer, n);

    printf("%d bytes sent to transformation\n", (int) n);

    /* I have free space in buffer, copy tempBuffer if it's not empty */
    if(copyTempToTransformBuff(key) == PARSER_ERROR) {
        return ERROR;
    }

    /* If response is done and buffer is empty close writeTransformFd */
    if(pareserResponseIsDone() && !buffer_can_read(&conn->inTransformBuffer)) {
        close(conn->writeTransformFd);
        conn->writeTransformFd = -1;
    }

    /* Set interests */
    /* readTransformFd can read */
    if(selector_set_interest(key->s, conn->readTransformFd, OP_READ) != SELECTOR_SUCCESS) {
        return ERROR;
    }

    /* if buffer is not empty I can write to transform */
    fd_interest interest = OP_NOOP;
    if(buffer_can_read(&conn->inTransformBuffer)) {
        printf("set OP_WRITE if outTransformBuffer is not empty\n");
        interest = OP_WRITE;
    }
    if(selector_set_interest_key(key, interest) != SELECTOR_SUCCESS) {
        return ERROR;
    }

    return RESPONSE;
}

unsigned responseWrite(struct selector_key * key) {
    struct Connection * conn = DATA_TO_CONN(key);

    printf("RESPONSE WRITE\n");

    /* Check wich f I'm writing to */
    if(key->fd == conn->clientFd) {
        /* Write to client */
        return writeToClient(key);
    } else if(key->fd == conn->writeTransformFd) {
        /* Write to the transformation */
        return writeToTransformation(key);
    } else {
        /* Error */
        return ERROR;
    }
}