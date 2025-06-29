/**
 * @file face_recognizer.cpp
 * @brief FaceRecognizer class for face embedding extraction and comparison via esp-who lib.
 */

#include "face_recognizer.hpp"

#include "esp_log.h"
#include <cmath> // std::sqrt, std::isnan, std::isinf
#include <numeric> // std::inner_product
#include <vector> // std::vector
#include <limits> // std::numeric_limits (for epsilon)
#include <algorithm> // std::min

#include "esp_heap_caps.h" 

// HumanFaceFeat, MFN, HumanFaceRecognizer, etc.
#include "human_face_recognition.hpp"

#include "dl_image_process.hpp"   // dl::image::img_t
#include "dl_detect_define.hpp"   // dl::detect::result_t
#include "dl_tensor_base.hpp"     // dl::TensorBase
#include "dl_image_define.hpp"    // dl::image::DL_IMAGE_PIX_TYPE_RGB565

static const char *TAG = "FACE_RECOGN";

/**
 * @brief FaceRecognizer Constructor.
 * This class calls multiple ESP-WHO components.
 * Internal ESP-WHO models are not initialized here, but within
 * extract_embedding_from_cropped_box
 */
FaceRecognizer::FaceRecognizer() {
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "FaceRecognizer constructor called (no internal ESP-WHO model init here).");
#endif 
    m_detector = nullptr; 
    m_feat_model = nullptr; // created/deleted per-run to avoid crashes
    m_recognizer = nullptr;
}

/**
 * @brief FaceRecognizer Destructor.
 * Deallocates any member pointers, if they were managed by this class.
 * this is a relic, remained for compatibility... There were several crashing cases
 */
FaceRecognizer::~FaceRecognizer() {
    ESP_LOGI(TAG, "FaceRecognizer destructor called (no ESP-WHO model deinit active).");
    // if you manage those here, they seemm to cause crashes.
    // delete m_detector; 
    // delete m_recognizer; 
}

/**
 * @brief Extracts a face embedding from a cropped image buffer.
 *
 * Takes a cropped face image and its bounding box and keypoints 
 * adjusted to the cropped image. It instantiates a HumanFaceFeat
 * model to generate a facial embedding, and then deallocates it. 
 * Otherwise crazy memory leaks and crashes occured...
 *
 * @param image_buffer Pointer to the RGB565 cropped image data.
 * @param cropped_img_width Width of the cropped image.
 * @param cropped_img_height Height of the cropped image.
 * @param adjusted_face_x X-coordinate of the face bounding box relative to the cropped image (should be 0).
 * @param adjusted_face_y Y-coordinate of the face bounding box relative to the cropped image (should be 0).
 * @param face_w Width of the face in the cropped image.
 * @param face_h Height of the face in the cropped image.
 * @param adjusted_keypoints Keypoints adjusted to be relative to the cropped image.
 * @return A dynamically allocated std::vector<float>* containing the embedding, or nullptr on failure.
 * The caller is responsible for deleting the returned vector.
 */
std::vector<float>* FaceRecognizer::extract_embedding_from_cropped_box(
    uint8_t *image_buffer, int cropped_img_width, int cropped_img_height,
    int adjusted_face_x, int adjusted_face_y, int face_w, int face_h,
    const std::vector<int>& adjusted_keypoints) {

    if (!image_buffer || cropped_img_width <= 0 || cropped_img_height <= 0 || face_w <= 0 || face_h <= 0) {
        ESP_LOGE(TAG, "Invalid input for extract_embedding_from_cropped_box.");
        return NULL;
    }
    if (adjusted_keypoints.empty() || adjusted_keypoints.size() != 10) {
        ESP_LOGE(TAG, "Adjusted keypoints vector is empty or has incorrect size (%zu). Expected 10 elements.", adjusted_keypoints.size());
        return NULL;
    }

#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Extracting embedding for face box: [%d,%d,%d,%d] from image %dx%d. Keypoints size: %zu",
             adjusted_face_x, adjusted_face_y, face_w, face_h, cropped_img_width, cropped_img_height, adjusted_keypoints.size());
#endif
    // Create img_t for the cropped image
    dl::image::img_t image_dl(image_buffer, cropped_img_width, cropped_img_height, dl::image::DL_IMAGE_PIX_TYPE_RGB565);

    // Create detect::result_t for the face within this cropped image
    dl::detect::result_t face_result;
    face_result.box.resize(4);
    face_result.box[0] = adjusted_face_x; // Should be 0
    face_result.box[1] = adjusted_face_y; // Should be 0
    face_result.box[2] = adjusted_face_x + face_w; // Should be face_w
    face_result.box[3] = adjusted_face_y + face_h; // Should be face_h
    face_result.keypoint = adjusted_keypoints;

#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Free heap (INTERNAL) before new HumanFaceFeat (per-run): %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Free heap (PSRAM) before new HumanFaceFeat (per-run): %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
    // Instantiate HumanFaceFeat LOCALLY for each inference. Crashes occured othewise! 
    HumanFaceFeat* local_feat_model = new HumanFaceFeat();
    if (local_feat_model == NULL) {
        ESP_LOGE(TAG, "Failed to create HumanFaceFeat model for inference!");
        return NULL;
    }
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Local HumanFaceFeat model created.");
    ESP_LOGI(TAG, "Free heap (INTERNAL) after new HumanFaceFeat (per-run): %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Free heap (PSRAM) after new HumanFaceFeat (per-run): %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Calling local_feat_model->run() for embedding extraction...");
#endif
    // Use inference via the locally allocated model
    dl::TensorBase* feat_tensor = local_feat_model->run(image_dl, face_result.keypoint);


    if (feat_tensor != NULL && feat_tensor->get_size() > 0) {
        float* raw_data_ptr = feat_tensor->get_element_ptr<float>();
        if (raw_data_ptr != NULL) {
#if EXTRA_HEAP_LOGGING
            // Log the raw float values of the tensor immediately after run()
            ESP_LOGI(TAG, "Raw feat_tensor output (first 10 elements):");
            char log_buffer[512]; // A buffer for logging floating points
            int current_offset = 0;
            for (size_t i = 0; i < std::min((size_t)10, (size_t)feat_tensor->get_size()); ++i) {
                current_offset += snprintf(log_buffer + current_offset, sizeof(log_buffer) - current_offset, "%.4f ", raw_data_ptr[i]);
            }
            ESP_LOGI(TAG, "  %s", log_buffer);
#endif
            bool has_nan = false;
            bool has_inf = false;
            for (size_t i = 0; i < feat_tensor->get_size(); ++i) {
                if (std::isnan(raw_data_ptr[i])) { has_nan = true; break; }
                if (std::isinf(raw_data_ptr[i])) { has_inf = true; break; }
            }
            if (has_nan) { ESP_LOGE(TAG, "Tensor with NaN values! Did a crash happen?"); }
            if (has_inf) { ESP_LOGE(TAG, "Tensor with Inf values! Did a crash happen?"); }
        }
    }
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Free heap (INTERNAL) after local_feat_model->run(): %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Free heap (PSRAM) after local_feat_model->run(): %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
    if (feat_tensor == NULL) {
        ESP_LOGE(TAG, "NULL returned from local_feat_model->run().");
        delete local_feat_model; // local model is deleted on failure
        return NULL;
    }

    std::vector<float>* embedding = new std::vector<float>(feat_tensor->get_size());
    if (!embedding) {
        ESP_LOGE(TAG, "Failed to allocate embedding vector memory (size %zu).", feat_tensor->get_size());
        delete local_feat_model; // local model is deleted only ON failure
        return NULL;
    }

    float* raw_data = feat_tensor->get_element_ptr<float>();
    if (raw_data == NULL) {
        ESP_LOGE(TAG, "Failed to get raw float pointer from feature tensor.");
        delete embedding;
        delete local_feat_model; // Local model is deleted ONLY on failure
        return NULL;
    }

    std::copy(raw_data, raw_data + feat_tensor->get_size(), embedding->begin());

    float norm_sum_sq = 0.0f;
    for (size_t i = 0; i < embedding->size(); ++i) {
        norm_sum_sq += (*embedding)[i] * (*embedding)[i];
    }
    float norm = std::sqrt(norm_sum_sq);
    if (norm < std::numeric_limits<float>::epsilon()) {
        ESP_LOGE(TAG, "L2 Norm is too small or zero! Cannot normalize. Returning unnormalized embedding.");
    } else {
        for (size_t i = 0; i < embedding->size(); ++i) {
            (*embedding)[i] /= norm;
        }
#if EXTRA_HEAP_LOGGING
        ESP_LOGI(TAG, "Embedding L2 normalized (manually). Original norm: %.4f", norm);
#endif
    }

#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "feat_tensor (implicitly) handled by local_feat_model's destructor.");
#endif
    // Delete the local HumanFaceFeat model after use
    delete local_feat_model;
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Local HumanFaceFeat model deleted after inference.");
    ESP_LOGI(TAG, "Free heap (INTERNAL) after deleting local HumanFaceFeat: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Free heap (PSRAM) after deleting local HumanFaceFeat: %" PRIu32, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Embedding extracted successfully (size: %zu).", embedding->size());
#endif
    return embedding;
}

/**
 * @brief Placeholder for face recognition utilizing an embedding.
 * @param embedding The face embedding to recognize.
 * @return The ID of the recognized face, or -1 if unknown.
 */
int FaceRecognizer::recognize_face_from_embedding(const std::vector<float>& embedding) {
    ESP_LOGE(TAG, "Recognizer model (m_recognizer) not managed by this FaceRecognizer instance.");
    return -1;
}

/**
 * @brief Compares two face embeddings using cosine similarity.
 * @param embedding1 First embedding.
 * @param embedding2 Second embedding.
 * @return Cosine similarity score in (0.0, 1.0].
 */
float FaceRecognizer::compare_embeddings(
    const std::vector<float>& embedding1, const std::vector<float>& embedding2) {

    if (embedding1.empty() || embedding2.empty() || embedding1.size() != embedding2.size()) {
        ESP_LOGE(TAG, "Invalid embeddings for comparison (empty or size mismatch).");
        return 0.0f;
    }
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Comparing embeddings using cosine similarity.");
#endif
    float dot_product = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;

    for (size_t i = 0; i < embedding1.size(); ++i) {
        dot_product += embedding1[i] * embedding2[i];
        norm_a += embedding1[i] * embedding1[i];
        norm_b += embedding2[i] * embedding2[i];
    }

    if (norm_a == 0.0f || norm_b == 0.0f) {
        return 0.0f; // division by zero
    }

    float similarity = dot_product / (std::sqrt(norm_a) * std::sqrt(norm_b));
#if EXTRA_HEAP_LOGGING
    ESP_LOGI(TAG, "Embeddings compared. Cosine similarity: %f", similarity);
#endif
    return similarity;
}