#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"

// Function prototypes for web server
httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);
void web_server_start(void);

#endif // WEB_SERVER_H
