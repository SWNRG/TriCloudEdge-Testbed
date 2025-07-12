#ifndef MESSAGE_HANDLER_H
#define MESSAGE_HANDLER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse incoming messages from the WebSocket server.
 * @param message null-terminated string received.
 * @param event_group The application's event group handle to 
 * signal events (e.g., frame ACKs).
 * Decodes incoming messages, act upon different response 
 * types, i.e., connection info, frame acknowledgements, 
 * and face recognition results.
 */
void message_handler_process(const char *message, EventGroupHandle_t event_group);

#ifdef __cplusplus
}
#endif

#endif // MESSAGE_HANDLER_H