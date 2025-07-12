#ifndef FACE_DATABASE_H
#define FACE_DATABASE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NAME_LEN 32
#define MAX_TITLE_LEN 32
#define MAX_STATUS_LEN 16
#define MAX_FILENAME_LEN 64

// metadata for each enrolled face. CAN BE EXTENDED AS YOU WISH
typedef struct {
    int id;
    int access_level;
    char name[MAX_NAME_LEN];
    char title[MAX_TITLE_LEN];
    char status[MAX_STATUS_LEN];
    char embedding_file[MAX_FILENAME_LEN];
} face_record_t;

esp_err_t database_init(void);
void database_deinit(void);
esp_err_t database_get_all_faces(face_record_t** out_faces, int* out_count);
esp_err_t database_add_face(const face_record_t* new_record);
int database_get_next_available_id(void);

// Clear all entries and files in database. USE WITH CAUTION!
esp_err_t database_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif // FACE_DATABASE_H