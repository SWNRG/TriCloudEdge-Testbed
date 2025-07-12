/**
 * @file face_database.c
 * @version 31
 * @brief Implementation for face metadata management.
 */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "cJSON.h"
#include "storage_manager.h"
#include "face_database.h"
#include <sys/stat.h>
#include <errno.h> 

static const char* TAG = "FACE_DB";
static const char* METADATA_PATH = "/spiffs/faces_meta.json";

static struct {
    face_record_t* records;
    int count;
} s_db = { .records = NULL, .count = 0 };

esp_err_t database_init(void) {
    if (s_db.records) {
        // If already initialized, de-initialize first to reload fresh
        database_deinit();
    }

    char* json_string = NULL;
    size_t json_string_len = 0;
    storage_read_file(METADATA_PATH, &json_string, &json_string_len);
    
    if (!json_string || json_string_len < 2) {
        if (json_string) free(json_string);
        ESP_LOGW(TAG, "Metadata file not found or empty. Creating new one.");
        
        storage_write_file(METADATA_PATH, "[]");
        
        // Re-read after creation to ensure loading
        storage_read_file(METADATA_PATH, &json_string, &json_string_len);
    }
    
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to read/create metadata file.");
        return ESP_FAIL;
    }
    
    cJSON* root = cJSON_Parse(json_string);
    free(json_string);

    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "Metadata file is corrupted or not a JSON array. Resetting.");
        cJSON_Delete(root);
        // Try to reset to an empty array
        storage_write_file(METADATA_PATH, "[]");
        // Re-try read (recursive call, but safe)
        return database_init(); 
    }

    s_db.count = cJSON_GetArraySize(root);
    ESP_LOGI(TAG, "Found %d face metadata records.", s_db.count);

    if (s_db.count > 0) {
        s_db.records = (face_record_t*)calloc(s_db.count, sizeof(face_record_t));
        if (!s_db.records) {
            s_db.count = 0; cJSON_Delete(root); return ESP_ERR_NO_MEM;
        }

        cJSON* elem = NULL;
        int i = 0;
        cJSON_ArrayForEach(elem, root) {
            cJSON* item;
            item = cJSON_GetObjectItem(elem, "id");
            s_db.records[i].id = item ? item->valueint : -1;
            item = cJSON_GetObjectItem(elem, "access_level");
            s_db.records[i].access_level = item ? item->valueint : 0;
            item = cJSON_GetObjectItem(elem, "name");
            strncpy(s_db.records[i].name, item ? item->valuestring : "", MAX_NAME_LEN - 1);
            s_db.records[i].name[MAX_NAME_LEN - 1] = '\0';
            item = cJSON_GetObjectItem(elem, "title");
            strncpy(s_db.records[i].title, item ? item->valuestring : "", MAX_TITLE_LEN - 1);
            s_db.records[i].title[MAX_TITLE_LEN - 1] = '\0';
            item = cJSON_GetObjectItem(elem, "status");
            strncpy(s_db.records[i].status, item ? item->valuestring : "", MAX_STATUS_LEN - 1);
            s_db.records[i].status[MAX_STATUS_LEN - 1] = '\0';
            item = cJSON_GetObjectItem(elem, "embedding_file");
            strncpy(s_db.records[i].embedding_file, item ? item->valuestring : "", MAX_FILENAME_LEN - 1);
            s_db.records[i].embedding_file[MAX_FILENAME_LEN - 1] = '\0';
            i++;
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

void database_deinit(void) {
    if (s_db.records) {
        free(s_db.records);
        s_db.records = NULL;
    }
    s_db.count = 0;
    ESP_LOGD(TAG, "Database deinitialized. Memory freed."); // Added log
}

esp_err_t database_get_all_faces(face_record_t** out_faces, int* out_count) {
    // database has to be initialized before returning records
    if (!s_db.records && s_db.count > 0) { // If records is NULL but count suggests there should be data, re-init
        ESP_LOGW(TAG, "dB records NULL but count > 0. Re-initializing dB.");
        if (database_init() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to re-initialize database in get_all_faces.");
            *out_faces = NULL;
            *out_count = 0;
            return ESP_FAIL;
        }
    } else if (!s_db.records && s_db.count == 0) { // If not initialized at all, init it.
         ESP_LOGI(TAG, "dB not initialized. Initializing in get_all_faces.");
         if (database_init() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize dB in get_all_faces.");
            *out_faces = NULL;
            *out_count = 0;
            return ESP_FAIL;
        }
    }
    
    *out_faces = s_db.records;
    *out_count = s_db.count;
    return ESP_OK;
}

int database_get_next_available_id(void) {
    int max_id = -1;
    // Ensure records are loaded before checking
    if (s_db.records == NULL && database_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize database for get_next_available_id.");
        return 0; // Return 0 as a safe default if database cannot be read
    }

    for (int i = 0; i < s_db.count; i++) {
        if (s_db.records[i].id > max_id) max_id = s_db.records[i].id;
    }
    return max_id + 1;
}

esp_err_t database_add_face(const face_record_t* new_record) {
    if (!new_record) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Adding metadata for face '%s' (ID: %d).", new_record->name, new_record->id);
    
    char* json_string = NULL;
    size_t json_string_len = 0;
    storage_read_file(METADATA_PATH, &json_string, &json_string_len);
    
    cJSON* root = cJSON_Parse(json_string ? json_string : "[]");
    if (json_string) free(json_string);
    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "Corrupted metadata while adding. Recreating.");
        cJSON_Delete(root); 
        root = cJSON_CreateArray(); // Start with an empty array if corrupted
    }
    /* added here some primitive characteristics of the person in photo 
     * Remember to change the JSON bffer size if altered
     * Jus use a default value if you dont want to bother
     */
    cJSON* new_face_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(new_face_json, "id", new_record->id);
    cJSON_AddNumberToObject(new_face_json, "access_level", new_record->access_level);
    cJSON_AddStringToObject(new_face_json, "name", new_record->name);
    cJSON_AddStringToObject(new_face_json, "title", new_record->title);
    cJSON_AddStringToObject(new_face_json, "status", new_record->status);
    cJSON_AddStringToObject(new_face_json, "embedding_file", new_record->embedding_file);
    cJSON_AddItemToArray(root, new_face_json);
    
    char* new_json_string = cJSON_Print(root);
    cJSON_Delete(root);
    if (!new_json_string) return ESP_FAIL;
    
    esp_err_t err = storage_write_file(METADATA_PATH, new_json_string);
    free(new_json_string);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Metadata file updated. Re-initializing database in memory.");
        return database_init(); // Re-initialize to update in-memory state
    }
    return err;
}
/* Needless to say that this will clear the whole database! */
esp_err_t database_clear_all(void) {
    ESP_LOGI(TAG, "Starting to clear all face dB entries.");
    face_record_t* faces_to_delete = NULL;
    int count = 0;
    
    // First, load all current records to get file paths
    esp_err_t get_err = database_get_all_faces(&faces_to_delete, &count);
    if (get_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get records for clearing: %s", esp_err_to_name(get_err));
        return ESP_FAIL;
    }

    if (count == 0) {
        ESP_LOGI(TAG, "Database is empty. No files to delete.");
        // make sure metadata file is empty
        storage_write_file(METADATA_PATH, "[]");
        database_deinit(); // make sure in-memory state is empty
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Found %d entries to delete.", count);
    for (int i = 0; i < count; ++i) {
        ESP_LOGI(TAG, "Deleting embedding file: %s for ID: %d", faces_to_delete[i].embedding_file, faces_to_delete[i].id);
        esp_err_t del_err = storage_delete_file(faces_to_delete[i].embedding_file);
        if (del_err != ESP_OK) {
            ESP_LOGW(TAG, "Could not delete file %s. Continue with next...", faces_to_delete[i].embedding_file);
        }
    }

    // Write an empty JSON array to the metadata file
    ESP_LOGI(TAG, "Writing empty metadata to %s.", METADATA_PATH);
    esp_err_t write_err = storage_write_file(METADATA_PATH, "[]");
    if (write_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write empty metadata file: %s", esp_err_to_name(write_err));
        return ESP_FAIL;
    }
    
    // Done all, de-initialize the databaser in-memory
    database_deinit();
    ESP_LOGI(TAG, "DATABASE IS NOW EMPTY.");
    return ESP_OK;
}