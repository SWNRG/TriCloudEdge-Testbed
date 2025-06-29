#include "who_camera.h"
#include "who_human_face_detection.hpp" // George added custom struct for the image
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "wifi.h"
#include "websocket_client.h" 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <stdlib.h> 
#include <string.h> 
#include <vector> 
#include <inttypes.h> // PRIu32
#include <algorithm> // std::max, std::min

static EventGroupHandle_t s_app_event_group;
const static int WIFI_CONNECTED_BIT = (1 << 0);
const static int WEBSOCKET_CONNECTED_BIT = (1 << 1);
const static int FRAME_ACK_BIT = (1 << 2);
const static int FRAME_QUEUE_SIZE = 2;

static QueueHandle_t xQueueAIFrame = NULL;
static QueueHandle_t xQueueFaceFrame = NULL;
static const char* TAG_APP_MAIN = "MAIN_APP";

#define HEARTBEAT_INTERVAL_S 300 // Started with 30 sec. Is it needed? What about 5 min?
#define HEARTBEAT_ON 1
#define SERVER_ACK_TIMEOUT_MS 100 // Increased timeout for server ACK. Started with 30 sec.
#define POST_DETECTION_COOLDOWN_S 10 // Stop camera for XX seconds after detection (cooldown)

/* 
 * Currently, the server cosine matching score for consequentially taken 
 * pictures is rather low! 
 * Trying to increase the face recognition score, play with these values
 * The crop_margin_pixels do not seem to make much of a difference!
 * The camera tuning seems to be good, but more difficult to detect faces!
 */
#define FACE_CROP_MARGIN_PIXELS 20 // Margin in pixels to add around the detected face (crop less tight)
// manual camera tuning might be useful for better face detection on server side
#define MANUAL_CAMERA_TUNING 1

#if HEARTBEAT_ON
static void heartbeat_task(void* pvParameters) {
    while(true) {
        xEventGroupWaitBits(s_app_event_group, WIFI_CONNECTED_BIT | WEBSOCKET_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_S * 1000));
        websocket_send_heartbeat();
    }
}
#endif

// Convert std::vector<int> to a JSON array string
static void int_vector_to_json_string(const std::vector<int>& vec, char* buffer, size_t buffer_len) {
    size_t offset = 0;
    offset += snprintf(buffer + offset, buffer_len - offset, "[");
    for (size_t i = 0; i < vec.size(); ++i) {
        offset += snprintf(buffer + offset, buffer_len - offset, "%d", vec[i]);
        if (i < vec.size() - 1) {
            offset += snprintf(buffer + offset, buffer_len - offset, ",");
        }
    }
    snprintf(buffer + offset, buffer_len - offset, "]");
}


static void face_sending_task(void* pvParameters) {
    face_to_send_t *face_data = NULL;
    const size_t CHUNK_SIZE = 8192; // Max chunk (message parts) size for WebSocket binary frames
    char keypoints_json_str[150]; // Keypoints JSON array buffer. Adjust size if more keypoints are needed.

    // Increased buffer size for start_msg to prevent format-truncation warning.
    char start_msg[350];

    while (true) {
        if (xQueueReceive(xQueueFaceFrame, &face_data, portMAX_DELAY)) {
            if (!face_data || !face_data->fb) {
                if (face_data) { // Make sure face_data is not NULL before freeing its members
                    if (face_data->fb) {
                         esp_camera_fb_return(face_data->fb); // Return the camera buffer if it was valid
                    }
                    free(face_data);
                }
                continue;
            }

            camera_fb_t* full_frame = face_data->fb;

            ESP_LOGI(TAG_APP_MAIN, "Face detected in frame %" PRIu32 ". Stopping camera.", face_data->id);
            // Stop new frames from being queued while processing the current one.
            camera_stop();

            ESP_LOGI(TAG_APP_MAIN, "Flushing processing queues.");
            // there is a case when reseting queues whlile processing a frame. Never investigated!
            xQueueReset(xQueueAIFrame);
            xQueueReset(xQueueFaceFrame);

            uint8_t *cropped_buf = NULL;
            do {
                // Original face bounding box relative to the full frame
                int original_face_x = face_data->box.x;
                int original_face_y = face_data->box.y;
                int original_face_w = face_data->box.w;
                int original_face_h = face_data->box.h;
                uint32_t frame_id = face_data->id;

                // Find the cropped region with margins set by FACE_CROP_MARGIN_PIXELS
                // Cast full_frame->width and height to int for std::max/min
                int frame_width_int = (int)full_frame->width;
                int frame_height_int = (int)full_frame->height;

                int crop_x_start = std::max(0, original_face_x - FACE_CROP_MARGIN_PIXELS);
                int crop_y_start = std::max(0, original_face_y - FACE_CROP_MARGIN_PIXELS);
                
                int crop_x_end = std::min(frame_width_int, original_face_x + original_face_w + FACE_CROP_MARGIN_PIXELS);
                int crop_y_end = std::min(frame_height_int, original_face_y + original_face_h + FACE_CROP_MARGIN_PIXELS);

                int cropped_img_width = crop_x_end - crop_x_start;
                int cropped_img_height = crop_y_end - crop_y_start;

                // Check if width or height < = 0 after margin adjustment.
                if (cropped_img_width <= 0 || cropped_img_height <= 0) {
                    ESP_LOGE(TAG_APP_MAIN, "Invalid crop dimensions after margin adjustment: width=%d, height=%d for frame %" PRIu32, cropped_img_width, cropped_img_height, frame_id);
                    break; // Exit loop on any error
                }

                // Adjust keypoints to be relative to the *new* cropped image's top-left corner
                std::vector<int> adjusted_keypoints = face_data->keypoint;
                for (size_t i = 0; i < adjusted_keypoints.size(); i += 2) {
                    adjusted_keypoints[i] -= crop_x_start;   
                    adjusted_keypoints[i+1] -= crop_y_start; 
                }
                int_vector_to_json_string(adjusted_keypoints, keypoints_json_str, sizeof(keypoints_json_str));
                ESP_LOGI(TAG_APP_MAIN, "Keypoints JSON (adjusted): %s", keypoints_json_str);

                // Calculate cropped length based on RGB565 (2 bytes per pixel)
                size_t cropped_len = (size_t)cropped_img_width * (size_t)cropped_img_height * 2; 
                cropped_buf = (uint8_t *)malloc(cropped_len);
                if (!cropped_buf) {
                    ESP_LOGE(TAG_APP_MAIN, "Failed to allocate cropped frame memory! Size: %zu Bytes", cropped_len);
                    break; // Exit do-while loop on error
                }

                // Crop the RGB565 image (each pixel = 2 Bytes)
                // Treat full_frame->buf (uint8_t*) as uint16_t* for pixel-wise copy
                uint16_t *p_full = (uint16_t *)full_frame->buf;
                uint16_t *p_cropped = (uint16_t *)cropped_buf;
                for (int row = 0; row < cropped_img_height; ++row) {
                    // Copy 'cropped_img_width' pixels (2 Bytes) from the source row to the cropped buffer
                    memcpy(
                        p_cropped + (row * cropped_img_width),                              // Destination: start of current row in cropped_buf
                        p_full + ((crop_y_start + row) * full_frame->width) + crop_x_start, // Source: start of current row in full_frame
                        cropped_img_width * sizeof(uint16_t)                                // Number of Bytes to copy for this row
                    );
                }

                ESP_LOGI(TAG_APP_MAIN, "Waiting for WIFI and WebSocket to be connected...");
                xEventGroupWaitBits(s_app_event_group, WIFI_CONNECTED_BIT | WEBSOCKET_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

                xEventGroupClearBits(s_app_event_group, FRAME_ACK_BIT); // Clear ACK bit before sending new frame

                // Send the original face box (x,y,w,h) relative to the full frame
                // AND the new cropped image dimensions (width, height)
                ESP_LOGI(TAG_APP_MAIN, "Starting transfer for frame %" PRIu32 ", cropped size: %dx%d (%zu Bytes). Original Face Box: [x=%d, y=%d, w=%d, h=%d]. Adjusted Keypoints size: %zu",
                         frame_id, cropped_img_width, cropped_img_height, cropped_len,
                         original_face_x, original_face_y, original_face_w, original_face_h,
                         adjusted_keypoints.size());

                snprintf(start_msg, sizeof(start_msg), "{\"type\":\"frame_start\", \"size\":%zu, \"id\":%" PRIu32 ", \"width\":%d, \"height\":%d, \"box_x\":%d, \"box_y\":%d, \"box_w\":%d, \"box_h\":%d, \"keypoints\":%s}",
                         cropped_len, frame_id, cropped_img_width, cropped_img_height, // buffer being sent
                         original_face_x, original_face_y, original_face_w, original_face_h, // original face box relative to full frame
                         keypoints_json_str); // keypoints relative to the new cropped image

                if(websocket_send_text(start_msg) != ESP_OK) {
                    ESP_LOGE(TAG_APP_MAIN, "Failed to send frame_start. Aborting frame transfer.");
                    break; // Exit loop on error
                }
                vTaskDelay(pdMS_TO_TICKS(10)); // delay after text frame

                uint8_t *p_buffer = cropped_buf;
                size_t remaining = cropped_len;
                while (remaining > 0) {
                    size_t to_send = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
                    // websocket_send_frame accepts uint8_t* and size_t
                    if (websocket_send_frame(p_buffer, to_send) != ESP_OK) {
                        ESP_LOGE(TAG_APP_MAIN, "Failed to send a chunk for frame %" PRIu32 ". Aborting.", frame_id);
                        // Set remaining to 1 to show an error and break the loop.
                        remaining = 1;
                        break;
                    }
                    p_buffer += to_send;
                    remaining -= to_send;
                    vTaskDelay(pdMS_TO_TICKS(10)); // delay between chunks
                }

                if (remaining > 0) { // If remaining > 0, it means the chunk sending broke early
                    ESP_LOGE(TAG_APP_MAIN, "Binary data transfer incomplete for frame %" PRIu32 ".", frame_id);
                    break; // Exit do-while loop because of send error
                }

                ESP_LOGI(TAG_APP_MAIN, "Finished sending chunks for frame %" PRIu32, frame_id);
                if(websocket_send_text("{\"type\":\"frame_end\"}") != ESP_OK) {
                    ESP_LOGE(TAG_APP_MAIN, "Failed to send frame_end message. Aborting.");
                    break; // Exit do-while loop on any other error
                }

                ESP_LOGI(TAG_APP_MAIN, "Waiting for FRAME_ACK_BIT for frame %" PRIu32 " (timeout: %d ms)", frame_id, SERVER_ACK_TIMEOUT_MS * 1000);
                EventBits_t bits = xEventGroupWaitBits(s_app_event_group, FRAME_ACK_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(SERVER_ACK_TIMEOUT_MS * 1000));
                if (bits & FRAME_ACK_BIT) {
                    ESP_LOGI(TAG_APP_MAIN, "Got ACK for frame %" PRIu32 "!", frame_id);
                } else {
                    ESP_LOGE(TAG_APP_MAIN, "No frame %" PRIu32 " ACK before timeout. Server unrechable. (Timeout: %dms)", frame_id, SERVER_ACK_TIMEOUT_MS * 1000);
                }

            } while(0); //  runs only once, for 'break' function

            // Make sure full_frame and cropped_buf are returned/freed
            esp_camera_fb_return(full_frame); // Return camera buffer to the driver
            ESP_LOGI(TAG_APP_MAIN, "Full frame buffer returned to camera driver.");

            if (cropped_buf) {
                free(cropped_buf);
                ESP_LOGI(TAG_APP_MAIN, "Cropped frame buffer freed.");
            }
            free(face_data); // Free the face_to_send_t struct itself
            ESP_LOGI(TAG_APP_MAIN, "face_to_send_t struct freed.");

            ESP_LOGI(TAG_APP_MAIN, "Halt camera for %d sec.", POST_DETECTION_COOLDOWN_S);
            vTaskDelay(pdMS_TO_TICKS(POST_DETECTION_COOLDOWN_S * 1000));
            ESP_LOGI(TAG_APP_MAIN, "Restarting camera.");
            camera_start(); // Restart camera after cooldown period (prevent detect same face)
        } // End of xQueueReceive
    } // End of while (true)
}


static void app_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG_APP_MAIN, "WiFi started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG_APP_MAIN, "WiFi disconnected. Clearing WIFI_CONNECTED_BIT and WEBSOCKET_CONNECTED_BIT.");
        xEventGroupClearBits(s_app_event_group, WIFI_CONNECTED_BIT | WEBSOCKET_CONNECTED_BIT);
        websocket_client_stop(); // Stop WebSocket client on WiFi disconnect
        esp_wifi_connect(); // Try to reconnect if disconnected
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG_APP_MAIN, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_app_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG_APP_MAIN, "WiFi connected. Starting WebSocket client.");
        websocket_client_start(s_app_event_group); // Pass event group to client for connection status
    }
}

extern "C" void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_app_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &app_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &app_event_handler, NULL));

    wifi_init_sta();

    xQueueAIFrame = xQueueCreate(FRAME_QUEUE_SIZE, sizeof(camera_fb_t*));
    xQueueFaceFrame = xQueueCreate(FRAME_QUEUE_SIZE, sizeof(face_to_send_t *));

    register_camera(PIXFORMAT_RGB565, FRAMESIZE_QVGA, 2, xQueueAIFrame);

    // Get the camera sensor object to set custom settings
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
#if MANUAL_CAMERA_TUNING
        ESP_LOGI(TAG_APP_MAIN, "Applying manual camera settings.");
        /* DISABLE AUTOMATIC CONTROLS */
        /* Use with caution, ugly things seem to happen, e.g., no face detection */
        s->set_whitebal(s, 0);       // Disable Auto White Balance (AWB)
        s->set_awb_gain(s, 0);       // Disable AWB Gain Control (to allow manual R/G/B gains if desired, though not setting them here)
        s->set_exposure_ctrl(s, 0);  // Disable Auto Exposure Control (AEC)
        s->set_gain_ctrl(s, 0);      // Disable Auto Gain Control (AGC)

        /* Manual Settings (Experiment CAREFULLY with these values) */
        s->set_brightness(s, 0); // -2 to 2 (0 is default)
        s->set_contrast(s, 0); // -2 to 2 (0 is default)
        s->set_saturation(s, 0); // -2 to 2 (0 is default)

        // Exposure Value (AEC Value): 0 to 1200 (higher = brighter). Common values: 300, 600, 900.
        /* IMPORTANT FOR LIGHTING. 600 is ok for a typical indoor setup. */
        s->set_aec_value(s, 600);
        // TODO: Need to create variables to hold values for brihghtness, contrast, saturation, etc..
        ESP_LOGI(TAG_APP_MAIN, "Camera settings: AWB OFF, AEC OFF, Brightness=0, Contrast=0, Saturation=0, AEC Value=600");
#else
        ESP_LOGI(TAG_APP_MAIN, "Automatic camera settings enabled.");
        // Ensure auto controls are enabled if MANUAL_CAMERA_TUNING is 0
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_gain_ctrl(s, 1);
#endif
        /* Found on the net, never tested extensively 
         * Set fixed AWB modes if manual AWB is off and the sensor supports it
         * OV2640 typically has AWBM_AUTO, AWBM_SUNNY, AWBM_CLOUDY, AWBM_OFF
         * If set_whitebal(s, 0) works, you might not need set_awb_mode.
         * For example, to try a fixed daylight mode:
         * s->set_awb_mode(s, AWBM_DAYLIGHT); // Requires specific enum, check esp_camera.h
         */
    } else {
        ESP_LOGE(TAG_APP_MAIN, "Failed to get camera sensor object for tuning.");
    }


    register_human_face_detection(xQueueAIFrame, NULL, NULL, xQueueFaceFrame);

    xTaskCreate(face_sending_task, "face_sender_task", 8192, NULL, 5, NULL);
#if HEARTBEAT_ON
    ESP_LOGI(TAG_APP_MAIN, "Heartbeat ON, every %d sec.", HEARTBEAT_INTERVAL_S);
    xTaskCreate(heartbeat_task, "heartbeat_task", 3072, NULL, 5, NULL);
#endif
    ESP_LOGI(TAG_APP_MAIN, "App started all workers.");
}