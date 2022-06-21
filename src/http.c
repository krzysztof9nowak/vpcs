#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


char* prepare_http_response(int code, const unsigned char* content, int content_length, int *resulting_size){
    const int max_header_length = 100;
    int max_length = max_header_length + content_length;
    char *response = malloc(max_length);

    int n = snprintf(response, max_header_length, "HTTP/1.1 %d OK\r\nContent-Length: %d\r\nContent-Type: text/html\r\n\r\n", code, content_length);
    if(n < 0){
        free(response);
        return 0;
    }

    bcopy(content, response + n, content_length);
    *resulting_size = n + content_length;

    return response;
}

// return 0 - if valid http request
//       -1 - if couldn't parse
// 
int parse_http_request(char* request, http_request_type *type, char **path){
    if(strncmp("GET", request, strlen("GET")) == 0){
        *type = HTTP_GET;
    } else{
        return -1;
    }

    return 0;
}
