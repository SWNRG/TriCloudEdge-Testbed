#pragma once

#include <cstdint>
#include <vector> // std::vector

// Include full definitions for ESP-DL/ESP-WHO types used in the header. Othewise linker errors occur
// maybe, it would be better to include libraries in idf_component.yml, never investigated...
#include "dl_image_define.hpp"      // dl::image::img_t
#include "dl_detect_define.hpp"     // dl::detect::result_t
#include "dl_recognition_define.hpp" // dl::recognition::result_t

// Forward declarations for classes used as pointers, to avoid recursive includes
class HumanFaceDetect;
class HumanFaceFeat;
class HumanFaceRecognizer;

class FaceRecognizer {
public:
    FaceRecognizer();
    ~FaceRecognizer();

    // extract embedding from a cropped image, adjusted parameters
    std::vector<float>* extract_embedding_from_cropped_box(
        uint8_t *image_buffer, int cropped_img_width, int cropped_img_height,
        int adjusted_face_x, int adjusted_face_y, int face_w, int face_h,
        const std::vector<int>& adjusted_keypoints);

    // Placeholder for face recognition from an embedding
    int recognize_face_from_embedding(const std::vector<float>& embedding);

    // Declaration of compare_embeddings to match its definition
    float compare_embeddings(const std::vector<float>& embedding1, const std::vector<float>& embedding2);

private:
    HumanFaceDetect* m_detector;       // face detection
    HumanFaceFeat* m_feat_model;       // feature extraction (embedding generation)
    HumanFaceRecognizer* m_recognizer; // recognition/comparison with database
};