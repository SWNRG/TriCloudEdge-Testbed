/**
 * @file face_enroller.cpp
 * @version 31
 * @brief Handles the enrollment of a new face.
 */
#include "face_enroller.h"
#include "face_recognizer.hpp"
#include "face_database.h"
#include "storage_manager.h"
#include "esp_log.h"
#include <cstdio>
#include <string>
#include <cstring>
#include <vector>

static const char* TAG = "FACE_ENROLLER";

esp_err_t enroll_new_face(uint8_t *image_buffer, size_t image_len, int width, int height,
                          int face_x, int face_y, int face_w, int face_h,
                          const std::vector<int>& keypoints) {
    ESP_LOGI(TAG, "Starting new face enrollment process for image with Box: [%d,%d,%d,%d], Keypoints size: %zu",
             face_x, face_y, face_w, face_h, keypoints.size());

    // Local instance, will create/delete HumanFaceFeat internally (avoid crashes)
    FaceRecognizer enroller_recognizer_client;

    // The image_buffer passed here is already cropped (from the initiating camera)
    // Face is at 0 of both x,y. face_w and face_h are ok
    int adjusted_face_x = 0;
    int adjusted_face_y = 0; 

    std::vector<int> adjusted_keypoints = keypoints;
    // Keypoints total 10 elements for 5 points.
    // X and Y coordinates start at the top-left corner of the face box.
    for (size_t i = 0; i < adjusted_keypoints.size(); i += 2) {
        adjusted_keypoints[i] -= face_x;   
        adjusted_keypoints[i+1] -= face_y; 
    }

    // Pass width and height of the cropped image
    std::vector<float>* new_face_embedding = 
            enroller_recognizer_client.extract_embedding_from_cropped_box(
        image_buffer, width, height, // dimensions of the received image
        // box and dimensions alligned with the cropped image
        adjusted_face_x, adjusted_face_y, face_w, face_h, 
        adjusted_keypoints);

    if (!new_face_embedding) {
        ESP_LOGE(TAG, "Embedding failed. No face detected or extraction failed.");
        return ESP_FAIL;
    }

    int new_id = database_get_next_available_id();
    ESP_LOGI(TAG, "Assign new metadata ID: %d", new_id);

    char new_embedding_path[MAX_FILENAME_LEN];
    snprintf(new_embedding_path, sizeof(new_embedding_path), "/spiffs/person_%d.db", new_id);

    esp_err_t write_err = storage_write_file_binary(
        new_embedding_path,
        (const uint8_t*)new_face_embedding->data(),
        new_face_embedding->size() * sizeof(float)
    );
    delete new_face_embedding;

    if (write_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save new embedding to %s. Aborting.", new_embedding_path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Saved new embedding to %s", new_embedding_path);

    face_record_t new_face_meta;
    new_face_meta.id = new_id;
    new_face_meta.access_level = 1;
    snprintf(new_face_meta.name, MAX_NAME_LEN, "Person %d", new_id);
    snprintf(new_face_meta.title, MAX_TITLE_LEN, "New User");
    snprintf(new_face_meta.status, MAX_STATUS_LEN, "Active");
    strncpy(new_face_meta.embedding_file, new_embedding_path, MAX_FILENAME_LEN - 1);
    new_face_meta.embedding_file[MAX_NAME_LEN - 1] = '\0'; // Ensure null termination

    if (database_add_face(&new_face_meta) == ESP_OK) {
        ESP_LOGI(TAG, "**********************************************");
        ESP_LOGI(TAG, "    NEW FACE ENROLLED! ID: %d (%s) *", new_face_meta.id, new_face_meta.name);
        ESP_LOGI(TAG, "**********************************************");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to add new face metadata. Cleaning up embedding file.");
        remove(new_embedding_path);
        return ESP_FAIL;
    }
}