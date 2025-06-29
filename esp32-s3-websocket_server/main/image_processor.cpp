/**
 * @file image_processor.cpp
 * @version 31
 * @brief Main logic for handling incoming images, recognition, and enrollment.
 *
 * Current Status:
 * This module is doing the image processing on the websocket server.
 * It uses  FaceRecognizer with AI models, initialized only once at startup, 
 * as frequent new/delete operations cause errors and memory leaks.
 *
 * 1.  Receives image buffer, its cropped dimensions, original face box coordinates, 
 *     and keypoints.
 * 2.  Performs coordinate adjustments to make face box and keypoints relative
 *     to the received cropped image buffer.
 * 3.  Uses the 'FaceRecognizer' instance to extract a face embedding
 *     whch calls the 'HumanFaceFeat::run()' method.
 * 4.  Optionaly logs detailed heap memory usage before and after critical 
 *     operations to monitor stability (usually disabled).
 * 5.  Preserves existing database entries, unless enrollment and database 
 *     erase are enabled (Be careful!).
 */
#include "esp_log.h"
#include "image_processor.h"
#include "face_recognizer.hpp"
#include "face_database.h"
#include "storage_manager.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <cstring>
#include "esp_heap_caps.h" 

#define ENABLE_ENROLLMENT 0 // USE ONLY TO INSERT NEW FACES, WITH DB ERASE ON STARTUP
#define COSINE_SIMILARITY_THRESHOLD 0.65f // Threshold for face comparison. NEEDS DISCUSSION AND TUNING!

#if ENABLE_ENROLLMENT 
#include "face_enroller.h" // For enroll_new_face function
#define ERASE_DATABASE_ON_STARTUP 1 // Set to 1 to clear database on startup, 0 to preserve it
#endif

static const char* TAG = "IMG_PROCES";

// Constructor called once only, otherwise catastrophe hits (restes, etc.).
static FaceRecognizer s_face_recognizer;

/**
 * @brief Initializes the image processor module.
 * @return ESP_OK if successful, error code otherwise.
 */
esp_err_t image_processor_init(void) {
    ESP_LOGI(TAG, "Image processor init.");
#if ERASE_DATABASE_ON_STARTUP // BE VERY CAREFUL! ERASES ALL DB ENTRIES!    
    ESP_LOGI(TAG, "Initiating database cleanup on startup to prepare for new enrollments.");
    if (database_clear_all() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear existing database entries. This might lead to inconsistent data.");
    } else {
        ESP_LOGI(TAG, "Database cleanup completed successfully. Starting with empty database for enrollment.");
    }
#else
    ESP_LOGI(TAG, "Database cleanup skipped. Database contains entries"); 
#endif

#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Opening face metadata database for initial load.");
#endif
    // Load the face metadata database. Reads records from SPIFFS.
    if(database_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize face metadata database on startup!");
        return ESP_FAIL; // Return failure if database init fails
    }
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Face metadata database opened successfully for initial load.");
#endif
    // De-initialize database after initial setup. It will be re-initialized in handle_new_image if needed.
    database_deinit(); 
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Image processor init complete. Database deinitialized after startup load.");
#endif
    return ESP_OK;
}

/**
 * @brief Workes upon an incoming (face detected) image for face recognition.
 *
 * Receives a cropped image buffer and metadata (face bounding box, keypoints) from 
 * the client. It adjusts the coordinates for the esp-who AI model and 
 * extracts an embedding. The cropped image has some buffer pixels around it (20px).
 *
 * @param image_buffer Pointer to the received image data (RGB565 format, already cropped).
 * @param image_len Length of the image_buffer in Bytes.
 * @param cropped_img_width Width of the cropped face image_buffer
 * @param cropped_img_height Height of the cropped face image_buffer
 * @param original_face_x X-coordinate of the bounding box relative to the *original full camera frame*.
 * @param original_face_y Y-coordinate of the bounding box relative to the *original full camera frame*.
 * @param face_w Width of the face bounding box (same as cropped_img_width).
 * @param face_h Height of the face bounding box (same as cropped_img_height).
 * @param keypoints A vector of integers for facial keypoints, relative to the *original full camera frame*.
 * @return ESP_OK if processing is successful, error code otherwise.
 */
esp_err_t image_processor_handle_new_image(
        uint8_t *image_buffer, size_t image_len, int cropped_img_width, int cropped_img_height,
        int original_face_x, int original_face_y, int face_w, int face_h,
        const std::vector<int>& keypoints) {

    // Log the initial received data for verification
    ESP_LOGI(TAG, "New image  Buffer Address: %p", image_buffer);
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "  Buffer Length (bytes): %zu", image_len);
    ESP_LOGI(TAG, "  Cropped Image Width x Height: %d x %d", cropped_img_width, cropped_img_height);
    ESP_LOGI(TAG, "  Original Face Box (relative to full frame) - X:%d, Y:%d, W:%d, H:%d",
             original_face_x, original_face_y, face_w, face_h);
    ESP_LOGI(TAG, "  Keypoints received (count: %zu):", keypoints.size());
#endif
    char kp_buf[200]; // Buffer for keypoints string
    int offset = snprintf(kp_buf, sizeof(kp_buf) - 1, "[");
    for (size_t i = 0; i < keypoints.size(); ++i) {
        offset += snprintf(kp_buf + offset, sizeof(kp_buf) - offset -1, "%d", keypoints[i]);
        if (i < keypoints.size() - 1) {
            offset += snprintf(kp_buf + offset, sizeof(kp_buf) - offset -1, ",");
        }
    }
#if EXTRA_HEAP_LOGGING
    snprintf(kp_buf + offset, sizeof(kp_buf) - offset, "]");
    ESP_LOGI(TAG, "    %s", kp_buf);
    ESP_LOGI(TAG, "--- End Received Data Log for Incoming Image ---");
#endif

#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Starting AI model feature extraction for incoming image.");
#endif
    // Adjust face box and keypoints to the cropped image, not the original frame.
    int adjusted_face_x = 0;
    int adjusted_face_y = 0;

    std::vector<int> adjusted_keypoints = keypoints;
    for (size_t i = 0; i < adjusted_keypoints.size(); i += 2) {
        adjusted_keypoints[i] -= original_face_x;
        adjusted_keypoints[i+1] -= original_face_y;
    }
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Adjusted Face Box for FaceRecognizer: X:%d, Y:%d, W:%d, H:%d",
             adjusted_face_x, adjusted_face_y, face_w, face_h);
    ESP_LOGI(TAG, "Adjusted Keypoints (count: %zu):", adjusted_keypoints.size());
#endif
    offset = snprintf(kp_buf, sizeof(kp_buf) - 1, "[");
    for (size_t i = 0; i < adjusted_keypoints.size(); ++i) {
        offset += snprintf(kp_buf + offset, sizeof(kp_buf) - offset - 1, "%d", adjusted_keypoints[i]);
        if (i < keypoints.size() - 1) {
            offset += snprintf(kp_buf + offset, sizeof(kp_buf) - offset - 1, ",");
        }
    }
    snprintf(kp_buf + offset, sizeof(kp_buf) - offset, "]");
    ESP_LOGI(TAG, "    %s", kp_buf);

    // Use the global/static s_face_recognizer instance to extract the embedding. Be careful with re-initializations!
    std::vector<float>* incoming_embedding = s_face_recognizer.extract_embedding_from_cropped_box(
        image_buffer, cropped_img_width, cropped_img_height,
        adjusted_face_x, adjusted_face_y, face_w, face_h,
        adjusted_keypoints
    );

    if (!incoming_embedding) {
        ESP_LOGE(TAG, "Incoming image feature extraction error...");
        //ESP_LOGW(TAG, "* RESULT: EMBEDDING EXTRACTION FAILED *");
    #if EXTRA_HEAP_LOGGING
        ESP_LOGI(TAG, "--- Image processing complete. Returning from image_processor_handle_new_image ---");
    #endif
        return ESP_FAIL; 
    }
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Successfully extracted embedding from incoming image (size: %zu).", incoming_embedding->size());
#endif
    // Database Comparison Loop
    face_record_t* db_faces_ptr = NULL;
    int db_face_count = 0;
    int recognized_id = -1;
    const char* recognized_name = "Unknown";
    float max_similarity = 0.0f;
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Starting database comparison for incoming image.");
    ESP_LOGI(TAG, "Free heap (INTERNAL) before database re-init for comparison: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Free heap (PSRAM) before database re-init for comparison: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
    // Re-initialize the fresh database
    if (database_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to re-initialize database.");
        delete incoming_embedding;
        return ESP_FAIL;
    }
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Database re-initialized for comparison loop.");
    ESP_LOGI(TAG, "Free heap (INTERNAL) after database re-init: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Free heap (PSRAM) after database re-init: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif

    if (database_get_all_faces(&db_faces_ptr, &db_face_count) == ESP_OK) {
        ESP_LOGI(TAG, "%d face records in dB.", db_face_count);
        if (db_face_count == 0) {
            ESP_LOGW(TAG, "Empty dB!");
        } else {
            for (int i = 0; i < db_face_count; i++) {
                const face_record_t* current_face = &db_faces_ptr[i];
#if EXTRA_HEAP_LOGGING
                ESP_LOGI(TAG, "--- Comparing with database face #%d (ID: %d, Name: %s) from file: %s ---",
                         i, current_face->id, current_face->name, current_face->embedding_file);
#endif
                char* stored_embedding_bytes_raw = NULL;
                size_t stored_embedding_len = 0;
#if EXTRA_HEAP_LOGGING
                ESP_LOGI(TAG, "Free heap (INTERNAL) before reading stored embedding: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                ESP_LOGI(TAG, "Free heap (PSRAM) before reading stored embedding: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
                esp_err_t read_err = storage_read_file(current_face->embedding_file, &stored_embedding_bytes_raw, &stored_embedding_len);
#if EXTRA_HEAP_LOGGING                
                ESP_LOGI(TAG, "Free heap (INTERNAL) after reading stored embedding: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                ESP_LOGI(TAG, "Free heap (PSRAM) after reading stored embedding: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
                if (read_err == ESP_OK && stored_embedding_bytes_raw != NULL) {
#if EXTRA_HEAP_LOGGING
                    ESP_LOGI(TAG, "Stored embedding for %s, size: %zu bytes.", current_face->name, stored_embedding_len);
#endif
                    size_t expected_embedding_dim = incoming_embedding->size();

                    if (stored_embedding_len != (expected_embedding_dim * sizeof(float))) {
                        ESP_LOGE(TAG, "Embedding file %s has unexpected size (actual: %zu bytes, expected: %zu bytes). Skipping comparison.",
                                 current_face->embedding_file, stored_embedding_len, expected_embedding_dim * sizeof(float));
                        free(stored_embedding_bytes_raw); // Free even if size mismatch
                        continue; // Skip the next dB entry
                    }

                    std::vector<float> stored_embedding(expected_embedding_dim);
                    memcpy(stored_embedding.data(), stored_embedding_bytes_raw, expected_embedding_dim * sizeof(float));
                    free(stored_embedding_bytes_raw); // Free the buffer for the stored embedding after copying
#if EXTRA_HEAP_LOGGING
                    ESP_LOGI(TAG, "Performing cosine similarity with %s (ID %d).", current_face->name, current_face->id);
#endif
                    float similarity = s_face_recognizer.compare_embeddings(*incoming_embedding, stored_embedding);
#if EXTRA_HEAP_LOGGING                    
                    ESP_LOGI(TAG, "Cosine similarity with %s (ID %d): %f", current_face->name, current_face->id, similarity);
#endif
                    if (similarity > max_similarity) {
                        max_similarity = similarity;
                        recognized_id = current_face->id;
                        recognized_name = current_face->name;
                        ESP_LOGI(TAG, "New best match! ID: %d (%s), Similarity: %f", recognized_id, recognized_name, max_similarity);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to read embedding file %s. Error: %s.", current_face->embedding_file, esp_err_to_name(read_err));
                    if(stored_embedding_bytes_raw) free(stored_embedding_bytes_raw);
                }
#if EXTRA_HEAP_LOGGING                
                ESP_LOGI(TAG, "Free heap (INTERNAL) after processing stored embedding: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                ESP_LOGI(TAG, "Free heap (PSRAM) after processing stored embedding: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif            
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to get records from dB.");
    }
#if EXTRA_HEAP_LOGGING  
    ESP_LOGI(TAG, "Comparison completed. Best similarity found: %f", max_similarity);
#endif
    // Final decision 
    if (recognized_id >= 0 && max_similarity >= COSINE_SIMILARITY_THRESHOLD) {

        ESP_LOGI(TAG, "\033[1;32m********************************************\033[0m");
        ESP_LOGI(TAG, "\033[1;32m* RESULT: FACE RECOGNIZED! ID: %d (%s) *\033[0m", recognized_id, recognized_name);
        ESP_LOGI(TAG, "\033[1;32m********************************************\033[0m");
    } else {
        ESP_LOGW(TAG, "********************************");
        ESP_LOGW(TAG, "* RESULT: UNKNOWN FACE (Best Similarity: %f) *", max_similarity);
        ESP_LOGW(TAG, "********************************");
    }

#if EXTRA_HEAP_LOGGING  
    ESP_LOGI(TAG, "De-initializing database after comparison loop.");
#endif
    // Always free the `s_db.records` memory after comparison
    database_deinit();
#if EXTRA_HEAP_LOGGING  
    ESP_LOGI(TAG, "Database de-initialized.");
#endif
    
    // Enrollment of new incoming faces. USE WITH CAUTION!
#if ENABLE_ENROLLMENT 
    ESP_LOGI(TAG, "Enrollment is ENABLED. Proceeding to enroll new incoming face.");
    esp_err_t enroll_res = enroll_new_face(
                            image_buffer, image_len, cropped_img_width, cropped_img_height,
                            adjusted_face_x, adjusted_face_y, face_w, face_h,
                            adjusted_keypoints
                           );
    if (enroll_res == ESP_OK) {
        ESP_LOGI(TAG, "New face enrollment process for incoming image initiated successfully.");
    } else {
        ESP_LOGE(TAG, "Failed to enroll new incoming face. Error: %s", esp_err_to_name(enroll_res));
    }
#else // when ENABLE_ENROLLMENT == 0
    ESP_LOGW(TAG, "Enrollment is DISABLED. No face enrollments...");
#endif

    // Free the incoming embedding vector.
    delete incoming_embedding;
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Image processing complete. Return from image_processor_handle_new_image");
#endif
    return ESP_OK;
}

/**
 * @brief Placeholder function to "process" an image embedding loaded from the database.
 * It only logs. It is a relic, left for compatibility!
 *
 * @param embedding_data Pointer to the raw embedding data.
 * @param embedding_len Length of the embedding data in bytes.
 * @param record_id The ID of the face record from which this embedding was loaded.
 * @param record_name The name of the face record.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t image_processor_handle_database_image(const uint8_t *embedding_data, size_t embedding_len, int record_id, const char* record_name) {
#if EXTRA_HEAP_LOGGING  
    ESP_LOGI(TAG, "--- Processing Database Image ---");
    ESP_LOGI(TAG, "  Database Image: ID %d, Name '%s', Embedding Length: %zu bytes", record_id, record_name, embedding_len);
    ESP_LOGI(TAG, "--- Finished Processing Database Image ---");
#endif  
    return ESP_OK;
}