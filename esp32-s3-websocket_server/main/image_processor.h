#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <vector> // Required for std::vector

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t image_processor_init(void);

/**
 * @brief Handles ane image received, with pre-detected face box and keypoints.
 *
 * @param image_buffer Pointer to the raw image data (RGB565).
 * @param image_len The total size of the image buffer in bytes.
 * @param width The width of the image in pixels.
 * @param height The height of the image in pixels.
 * @param face_x X-coordinate of the detected face's top-left corner.
 * @param face_y Y-coordinate of the detected face's top-left corner.
 * @param face_w Width of the detected face's bounding box.
 * @param face_h Height of the detected face's bounding box.
 * @param keypoints A std::vector<int> containing the facial landmarks (e.g., 10 integers for 5 points).
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t image_processor_handle_new_image(uint8_t *image_buffer, size_t image_len, int width, int height,
                                           int face_x, int face_y, int face_w, int face_h,
                                           const std::vector<int>& keypoints); // Added keypoints

/**
 * @brief Placeholder to "process" an image embedding loaded from the database.
 * For now, it only logs its existence.
 *
 * @param embedding_data Pointer to the raw embedding data.
 * @param embedding_len Length of the embedding data in bytes.
 * @param record_id The ID of the face record from which this embedding was loaded.
 * @param record_name The name of the face record.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t image_processor_handle_database_image(const uint8_t *embedding_data, size_t embedding_len, int record_id, const char* record_name); // NEW FUNCTION DECLARATION

#ifdef __cplusplus
}
#endif