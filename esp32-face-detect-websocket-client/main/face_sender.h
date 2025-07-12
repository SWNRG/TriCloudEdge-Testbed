#ifndef FACE_SENDER_H
#define FACE_SENDER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and provide handles to the face sender module.
 * @param app_event_group Handle to the main app event group.
 * @param ai_queue Handle to the incoming AI frames queue.
 * @param face_queue Handle to the detected faces to be sent queue.
 */
void face_sender_init(EventGroupHandle_t app_event_group, QueueHandle_t ai_queue, QueueHandle_t face_queue);

/**
 * @brief Processing and sending face data.
 * @param pvParameters Unused task parameters.
 */
void face_sending_task(void* pvParameters);

#ifdef __cplusplus
}
#endif

#endif // FACE_SENDER_H