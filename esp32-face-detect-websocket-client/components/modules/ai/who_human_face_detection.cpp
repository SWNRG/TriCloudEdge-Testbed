/* * IMPORTANT: THIS FILE HAS BEEN HEAVILY MODIFIED FROM THE ORIGINAL VERSION      
 * * // George, May-June, 2025                                       
 * * Changes:                                                                   
 * - Added 'print_detection_result' for printf in console                   
 * of face detection results.                                             
 * - Call to 'print_detection_result' exists. Can be uncommented.                      
 * - Added several printf for debugging and                                 
 * task creation error checks.                                            
 * - Changed task_process_handler to handle frame buffer 
 * and only send frames with faces.                
 * - The 'task_process_handler' copies the bounding box coordinates 
 * from the AI library struct into a struct here
 */

#include "esp_log.h"
#include "esp_camera.h"

#include "dl_image.hpp"
#include "who_human_face_detection.hpp"
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"

#include <stdio.h>
#include <list>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <vector> // Required for std::vector operations

#define TWO_STAGE_ON 1
static const char* TAG = "human_face_detection";

static QueueHandle_t xQueueFrameI = NULL;
static QueueHandle_t xQueueEvent = NULL;
static QueueHandle_t xQueueFrameO = NULL;
static QueueHandle_t xQueueResult = NULL;

static bool gEvent = true;

// Helper to print detection results - Keeping for consistency
static void print_detection_result(std::list<dl::detect::result_t>& detect_results) {
    if (detect_results.empty()) {
        ESP_LOGI(TAG, "No face detected.");
        return;
    }
    int i = 0;
    for (auto& res : detect_results) {
        ESP_LOGI(TAG, "  Face #%d: Score=%.2f, Box=[%d,%d,%d,%d]",
                 i, res.score, res.box[0], res.box[1], res.box[2], res.box[3]);
        ESP_LOGI(TAG, "    Keypoints (%zu):", res.keypoint.size());
        for (size_t j = 0; j < res.keypoint.size(); j += 2) {
            ESP_LOGI(TAG, "      (%d, %d)", res.keypoint[j], res.keypoint[j+1]);
        }
        i++;
    }
}


void task_process_handler(void* arg)
{
    camera_fb_t* frame = NULL;
    HumanFaceDetectMSR01 detector(0.25F, 0.3F, 10, 0.3F);
#if TWO_STAGE_ON
    HumanFaceDetectMNP01 detector2(0.35F, 0.3F, 10);
#endif

    while (true)
    {
        if (gEvent)
        {
            if (xQueueReceive(xQueueFrameI, &frame, portMAX_DELAY))
            {
                bool is_detected = false;
                // Assuming frame->buf is uint16_t* for RGB565 as per register_camera call in app_main.cpp
                // and dl::image::rgb565 as input to infer.
                // NOTE: The `infer` method takes `uint16_t*` and `dl::image::rgb565` is usually the format.
                // The third parameter `3` indicates channels, for RGB565 it should implicitly handle 2 bytes/pixel.
#if TWO_STAGE_ON
                std::list<dl::detect::result_t>& detect_candidates = detector.infer((uint16_t*)frame->buf, { (int)frame->height, (int)frame->width, 3 });
                std::list<dl::detect::result_t>& detect_results = detector2.infer((uint16_t*)frame->buf, { (int)frame->height, (int)frame->width, 3 }, detect_candidates);
#else
                std::list<dl::detect::result_t>& detect_results = detector.infer((uint16_t*)frame->buf, { (int)frame->height, (int)frame->width, 3 });
#endif
                // Uncomment to print detection results
                // print_detection_result(detect_results);

                if (detect_results.size() > 0)
                {
                    is_detected = true;
                    ESP_LOGI(TAG, "Face DETECTED!");       
                    
                    if (xQueueFrameO)
                    {
                        // CRITICAL FIX: Use 'new' instead of 'malloc' for C++ structs with std::vector
                        face_to_send_t *face_data = new face_to_send_t();
                        if(face_data) 
                        {
                            dl::detect::result_t first_face = detect_results.front();
                            
                            face_data->fb = frame;
                            face_data->box.x = first_face.box[0];
                            face_data->box.y = first_face.box[1];
                            face_data->box.w = first_face.box[2] - first_face.box[0]; // Calculate width
                            face_data->box.h = first_face.box[3] - first_face.box[1]; // Calculate height

                            face_data->keypoint = first_face.keypoint; // Copy keypoint data (now safely calls std::vector::operator=)

                            if (xQueueSend(xQueueFrameO, &face_data, 0) != pdTRUE)
                            {
                                ESP_LOGW(TAG, "Output frame queue is full. Dropping frame.");
                                esp_camera_fb_return(frame);
                                delete face_data; // Use 'delete' with 'new'
                            }
                        } 
                        else 
                        {
                             ESP_LOGE(TAG, "Failed to allocate memory for face_data struct.");
                             esp_camera_fb_return(frame);
                        }
                    }
                    else // if xQueueFrameO is NULL but face detected
                    {
                        esp_camera_fb_return(frame);
                    }
                }
                else // if no face detected
                {
                    esp_camera_fb_return(frame);
                }
                
                frame = NULL; 

                if (xQueueResult)
                {
                    xQueueSend(xQueueResult, &is_detected, portMAX_DELAY);
                }
            }
        }    
        vTaskDelay(pdMS_TO_TICKS(10)); 
    } 
}

static void task_event_handler(void* arg)
{
    while (true)
    {
        xQueueReceive(xQueueEvent, &(gEvent), portMAX_DELAY);
    }
}

void register_human_face_detection(const QueueHandle_t frame_i,
    const QueueHandle_t event,
    const QueueHandle_t result,
    const QueueHandle_t frame_o)
{
    xQueueFrameI = frame_i;
    xQueueFrameO = frame_o;
    xQueueEvent = event;
    xQueueResult = result;

    xTaskCreatePinnedToCore(task_process_handler, TAG, 4 * 1024, NULL, 5, NULL, 0); // Consider increasing stack size if complex AI models are used

    if (xQueueEvent) {
        xTaskCreatePinnedToCore(task_event_handler, TAG, 4 * 1024, NULL, 5, NULL, 1);
    }
}