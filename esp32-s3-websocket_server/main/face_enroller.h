#ifndef FACE_ENROLLER_H
#define FACE_ENROLLER_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <vector> 

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates an embedding and adds a face record into the database.
 * Accepts the incoming image, with bounding box, and keypoints for feature extraction.
 *
 * @param image_buffer Pointer to the raw image data (RGB888).
 * @param image_len The size of the image buffer in Bytes.
 * @param width The width of the image in pixels
 * @param height The height of the image in pixels.
 * @param face_x X-coordinate of the detected face's top-left corner.
 * @param face_y Y-coordinate of the detected face's top-left corner.
 * @param face_w Width of the detected face's bounding box.
 * @param face_h Height of the detected face's bounding box.
 * @param keypoints A std::vector<int> with facial landmarks (e.g., 10 integers for 5 points).
 * @return esp_err_t ESP_OK on success, or error code otherwise.
 */
esp_err_t enroll_new_face(uint8_t *image_buffer, size_t image_len, int width, int height,
                          int face_x, int face_y, int face_w, int face_h,
                          const std::vector<int>& keypoints); 

#ifdef __cplusplus
}
#endif

#endif // FACE_ENROLLER_H