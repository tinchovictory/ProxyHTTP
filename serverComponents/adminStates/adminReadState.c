#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "adminReadState.h"
#include "adminErrorState.h"

#include "../admin-structure.h"
#include "../adminStm.h"
#include "../adminActiveHandlers.h"
#include "../../utils/buffer/buffer.h"
#include "../../parser/request.h"

void generateAdminResponse(buffer * buff);

/* Parte del parser */
int processAdminRequest() {
    return 1;
}
/* End parte del parser */

unsigned adminRead(struct selector_key * key) {
    struct AdminConn * conn = DATA_TO_ADMIN(key);
    uint8_t *ptr;
    size_t count;
    ssize_t n;

    ptr = buffer_write_ptr(&conn->buff, &count);
    n = recv(conn->adminFd, ptr, count, 0);
    if(n <= 0) {
        /* Admin closed connection */
        return ADMIN_FATAL_ERROR;
    }
    buffer_write_adv(&conn->buff, n);

    // parse data?

    if(processAdminRequest()) {
        if(selector_set_interest_key(key, OP_WRITE) != SELECTOR_SUCCESS) {
            return ADMIN_FATAL_ERROR;
        }
        
        generateAdminResponse(&conn->buff);
        return ADMIN_WRITE;
    }

    // generate response error
    printf("admin set errror\n");
    return adminSetError(key, ADMIN_REQ_ERR);
}

void generateAdminResponse(buffer * buff) {
    buffer_reset(buff);

    // write response to buff
    uint8_t * ptr;
    size_t size;
    ptr = buffer_write_ptr(buff, &size);
    memcpy(ptr, "hola", 4);
    ptr[4] = 0;
    buffer_write_adv(buff, 5);    
}