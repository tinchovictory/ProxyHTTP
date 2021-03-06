//
// Created by Segundo Fariña on 31/5/18.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "response.h"
#include "headerGroup.h"
#include "body.h"

void consume(struct response_parser * p,char * string){
    size_t i;
    enum response_state state;
    for(i =0 ; i< strlen(string) ;i++){
        state = response_parser_feed(string[i],p);
        char letter = string[i];
        if(letter == '\n'){
            letter = 'N';
        }else if (letter == '\r'){
            letter = 'R';
        }
        printf("feeded %c state now is %s\n",letter,response_state_string(state));
    }

    //printf("Header value is %s",p->value);
    printf("Printing list....\n");
    if(p->headerList == NULL){
        printf("LIST IS NULL\n");
    }
    header_list_print(p->headerList);

}


void consume_wrapper(struct response_parser * p, char * response){
    char *headers;
    int headersWritten =0;
    char *body;
    int bodyWritten=0;
    int len,len2;

    headers = calloc(1024, sizeof(char));
    body= calloc(1024, sizeof(char));


    len =strlen(response);
    //response_parser_consume(p,response,&len,headers,&headersWritten);
    printf("headers wirtten bytes is %d\n",headersWritten);
    printf("parsed headers is\n%s",headers);


    len2=strlen(response+len);
    //response_parser_consume(p,response+len,&len2,body,&bodyWritten);

    printf("body wirtten bytes is %d\n",bodyWritten);
    body[bodyWritten]=0;
    printf("parsed body is\n%s\n",body);


    free(headers);
    free(body);
}


int main(){

    


    struct response_parser  p;


    char * r302 =
            "HTTP/1.1 302 Found\r\n"
            "Date: Fri, 01 Jun 2018 01:17:16 GMT\r\n"
            "Server: Apache-Coyote/1.1\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "X-XSS-Protection: 1; mode=block\r\n"
            "Cache-Control: no-cache, no-store, max-age=0, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Expires: 0\r\n"
            "X-Frame-Options: DENY\r\n"
            "Location: /paw-2018a-4/searchResults?st=1&cty=2\r\n"
            "Content-Language: en-GB\r\n"
            "Content-Length: 0\r\n"
            "Keep-Alive: timeout=5, max=100\r\n"
            "Connection: Keep-Alive\r\n"
            "\r\n";

    printf("====================302 Response!!!=====================\n");

    response_parser_init(&p,METHOD_GET);

    consume(&p,r302);

    response_parser_close(&p);


    char * r200 = "HTTP/1.1 200 OK\r\n"
                  "Date: Fri, 01 Jun 2018 01:17:17 GMT\r\n"
                  "Server: Apache-Coyote/1.1\r\n"
                  "Last-Modified: Wed, 09 May 2018 18:34:56 GMT\r\n"
                  "Accept-Ranges: bytes\r\n"
                  "Content-Type: application/javascript\r\n"
                  "Vary: Accept-Encoding\r\n"
                  "Content-Encoding: gzip\r\n"
                  "Content-Length: 761\r\n"
                  "Keep-Alive: timeout=5, max=99\r\n"
                  "Connection: Keep-Alive\r\n"
                  "\r\n"
                  "function generateStars() {\n"
                  "    $('.dyn-stars').each(function(){\n"
                  "        var elem = $(this);\n"
                  "        var rating = elem.data('rating');\n"
                  "        fillStars(elem, rating);\n"
                  "    });\n"
                  "}\n"
                  "\n"
                  "function fillStars(container, rating) {\n"
                  "    var done = 0;\n"
                  "\n"
                  "    for(var i = 1; i <= 5; i++) {\n"
                  "        if(rating < (i - 0.25) ) {\n"
                  "            //veo si es media\n"
                  "            if(!done && rating >= (i-0.75) ) {\n"
                  "                //es media\n"
                  "                container.append('<i class=\"fa fa-star-half-o\"></i>');\n"
                  "            } else {\n"
                  "                //agrego vacia\n"
                  "                container.append('<i class=\"fa fa-star-o\"></i>');\n"
                  "            }\n"
                  "            done = 1;\n"
                  "        } else {\n"
                  "            //agrego una entera\n"
                  "            container.append('<i class=\"fa fa-star\"></i>');\n"
                  "        }\n"
                  "    }\n"
                  "}";


    printf("====================200 response=====================\n");

    response_parser_init(&p,METHOD_GET);

    consume(&p,r200);

    response_parser_close(&p);


    char * r301 = "HTTP/1.1 301 Moved Permanently\r\n"
                  "Location: http://www.google.com/\r\n"
                  "Content-Type: text/html; charset=UTF-8\r\n"
                  "Date: Sun, 03 Jun 2018 04:35:48 GMT\r\n"
                  "Expires: Tue, 03 Jul 2018 04:35:48 GMT\r\n"
                  "Cache-Control: public, max-age=2592000\r\n"
                  "Server: gws\r\n"
                  "Content-Length: 219\r\n"
                  "X-XSS-Protection: 1; mode=block\r\n"
                  "X-Frame-Options: SAMEORIGIN\r\n"
                  "\r\n"
                  "<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n"
                  "<TITLE>301 Moved</TITLE></HEAD><BODY>\n"
                  "<H1>301 Moved</H1>\n"
                  "The document has moved\n"
                  "<A HREF=\"http://www.google.com/\">here</A>.\n"
                  "</BODY></HTML>\n"
                  "\r\n";


    response_parser_init(&p,METHOD_GET);

    consume(&p,r301);

    response_parser_close(&p);


    char * chunked ="HTTP/1.1 301 Moved Permanently\r\n"
                    "Location: http://www.google.com/\r\n"
                    "Content-Type: text/html; charset=UTF-8\r\n"
                    "Date: Sun, 03 Jun 2018 04:35:48 GMT\r\n"
                    "Expires: Tue, 03 Jul 2018 04:35:48 GMT\r\n"
                    "Cache-Control: public, max-age=2592000\r\n"
                    "Server: gws\r\n"
                    "Transfer-Encoding: Chunked\r\n"
                    "X-XSS-Protection: 1; mode=block\r\n"
                    "X-Frame-Options: SAMEORIGIN\r\n"
                    "\r\n"
                    "4\r\n"
                    "Wiki\r\n"
                    "5\r\n"
                    "pedia\r\n"
                    "E\r\n"
                    " in\r\n"
                    "\r\n"
                    "chunks.\r\n"
                    "0\r\n"
                    "\r\n";


    response_parser_init(&p,METHOD_GET);

    consume(&p,chunked);

    response_parser_close(&p);


}
