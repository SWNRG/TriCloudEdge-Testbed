#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the WebSocket client.
 * @param event_group Handler for signaling connection status.
 */
void websocket_client_start(EventGroupHandle_t event_group);

/**
 * @brief Stop & destroy the active WebSocket client instance
 */
void websocket_client_stop(void);

/**
 * @brief Send a predefined heartbeat message.
 * @return ESP_OK on success, or ESP_FAIL on failure.
 */
esp_err_t websocket_send_heartbeat(void);

/**
 * @brief Sends a binary data frame over the WebSocket connection.
 * @param data Pointer to the data buffer to send.
 * @param len Bytes length of the data.
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t websocket_send_frame(const uint8_t *data, size_t len);

/**
 * @brief Send a text message over the WebSocket connection.
 * @param text A null-terminated string to send.
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t websocket_send_text(const char* text); 

/**
 * @brief Check if the WebSocket client is currently connected.
 * @return True if connected, false otherwise.
 */
bool is_websocket_connected(void); 

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_CLIENT_H