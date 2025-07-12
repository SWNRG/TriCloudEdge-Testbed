#ifndef S3_UPLOADER_H
#define S3_UPLOADER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Gets a pre-signed URL for uploading a file to S3.
 *
 * @param filename The name of the file to be uploaded.
 * @param presigned_url Buffer to store the fetched pre-signed URL.
 * @param max_url_len The maximum size of the presigned_url buffer.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t s3_uploader_get_presigned_url(const char *filename, char *presigned_url, size_t max_url_len);

/**
 * @brief Uploads data to S3 using a pre-signed URL.
 *
 * @param s3_url The pre-signed URL for the PUT request.
 * @param data Pointer to the data to upload.
 * @param data_len Length of the data in bytes.
 * @param content_type The MIME type of the content (e.g., "image/jpeg").
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t s3_uploader_upload_by_url(const char* s3_url, const uint8_t* data, size_t data_len, const char* content_type);

/**
 * @brief Tests connectivity to the S3 service by requesting a pre-signed URL.
 *
 * @return ESP_OK if the connection is successful, otherwise an error code.
 */
esp_err_t s3_uploader_test_connectivity(void);

/**
 * @brief Performs a full test by uploading a dummy file to S3.
 *
 * @return ESP_OK on successful upload, otherwise an error code.
 */
esp_err_t s3_uploader_test_upload(void);


#ifdef __cplusplus
}
#endif

#endif // S3_UPLOADER_H