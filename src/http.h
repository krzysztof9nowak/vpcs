
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_DELETE,
    HTTP_PUT,
} http_request_type;

char* prepare_http_response(int code, const unsigned char* content, int content_length, int *resulting_size);
int parse_http_request(char* request, http_request_type *type, char **path);




 

