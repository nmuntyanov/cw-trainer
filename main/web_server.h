#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

/**
 * @brief Start the HTTP server.
 * Serves the dashboard and the REST API.
 */
esp_err_t web_server_init(void);

/**
 * @brief Broadcast a message to all connected WebSocket clients.
 */
void web_server_broadcast(const char *msg);

#endif // WEB_SERVER_H
