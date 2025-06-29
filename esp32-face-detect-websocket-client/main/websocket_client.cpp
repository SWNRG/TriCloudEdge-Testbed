#include "websocket_client.h"
#include "esp_websocket_client.h" // ESP-IDF WebSocket client (inherited)
#include "esp_log.h"
#include "secret.h" // WEBSOCKET_URI
#include "freertos/event_groups.h" // EventGroupHandle_t
#include <string.h> 

static const char* TAG = "WEBSOCKET_CLIENT";

static esp_websocket_client_handle_t client = NULL;
static EventGroupHandle_t s_client_event_group = NULL; // report connection status and ACK

// Default timeout for WebSocket
#define ESP_WEBSOCKET_CLIENT_RETRY_MS_DEFAULT (10000)
#define ESP_WEBSOCKET_CLIENT_SEND_TIMEOUT_MS_DEFAULT (10000)

// Bits for the event group
#define WEBSOCKET_CONNECTED_BIT (1 << 1) // Already defined in app_main.cpp, align this!
#define FRAME_ACK_BIT (1 << 2)           // Already defined in app_main.cpp, align this!


static void websocket_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;
    switch (event_id)
    {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        if (s_client_event_group) {
            xEventGroupSetBits(s_client_event_group, WEBSOCKET_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        if (s_client_event_group) {
            xEventGroupClearBits(s_client_event_group, WEBSOCKET_CONNECTED_BIT | FRAME_ACK_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DATA: // low level ping/pong. Too many printouts, currenlty disabled below
        //ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA received");
        //ESP_LOGI(TAG, "Received opcode=%d, data_len=%d", data->op_code, data->data_len);
        if (data->op_code == 0x08 && data->data_len == 2) { // Close frame
            ESP_LOGW(TAG, "Received closed message with code=%d", (data->data_ptr[0] << 8) | data->data_ptr[1]);
        }
        else if (data->op_code == 1 && data->data_ptr) { // Text frame
            if (strstr((const char*)data->data_ptr, "frame_ack") != NULL) {
                ESP_LOGI(TAG, "Got frame ACK.");
                if (s_client_event_group) {
                    xEventGroupSetBits(s_client_event_group, FRAME_ACK_BIT); // Signal ACK received
                }
            } else {
                ESP_LOGW(TAG, "Received '%.*s'", data->data_len, (char*)data->data_ptr);
            }
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WEBSOCKET_EVENT_ERROR");
        if (s_client_event_group) {
            xEventGroupClearBits(s_client_event_group, WEBSOCKET_CONNECTED_BIT | FRAME_ACK_BIT);
        }
        break;
    default:
        ESP_LOGI(TAG, "Unhandled WebSocket event (ID: %ld)", event_id);
        break;
    }
}

// accept EventGroupHandle_t
void websocket_client_start(EventGroupHandle_t event_group)
{
    if (client) {
        ESP_LOGW(TAG, "WebSocket client is already active. Stop & restart...");
        websocket_client_stop();
    }

    s_client_event_group = event_group;

    ESP_LOGI(TAG, "Starting WebSocket client on %s", WEBSOCKET_URI);

    // memset to zero all fields to fix "missing initializer" warnings and "designator order" errors
    esp_websocket_client_config_t websocket_cfg;
    memset(&websocket_cfg, 0, sizeof(esp_websocket_client_config_t));

    websocket_cfg.uri = WEBSOCKET_URI;
    websocket_cfg.reconnect_timeout_ms = ESP_WEBSOCKET_CLIENT_RETRY_MS_DEFAULT;
    websocket_cfg.network_timeout_ms = ESP_WEBSOCKET_CLIENT_SEND_TIMEOUT_MS_DEFAULT;
    websocket_cfg.buffer_size = 160 * 1024; // buffer for image data, was increased to avoid errors. Do exmperiment

    client = esp_websocket_client_init(&websocket_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "WebSocket client initialization failed");
        if (s_client_event_group) {
            xEventGroupClearBits(s_client_event_group, WEBSOCKET_CONNECTED_BIT);
        }
        return;
    }

    esp_err_t err = esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void*)client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WebSocket events: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(client);
        client = NULL;
        if (s_client_event_group) {
            xEventGroupClearBits(s_client_event_group, WEBSOCKET_CONNECTED_BIT);
        }
        return;
    }

    err = esp_websocket_client_start(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s. Ensure network is up and URI is correct.", esp_err_to_name(err));
        if (s_client_event_group) {
            xEventGroupClearBits(s_client_event_group, WEBSOCKET_CONNECTED_BIT);
        }
    }
    else {
        ESP_LOGI(TAG, "WebSocket client start issued.");
    }
}

void websocket_client_stop(void)
{
    if (client)
    {
        ESP_LOGI(TAG, "Stopping WebSocket client...");
        esp_err_t err = esp_websocket_client_stop(client); //Gracefully stop the client
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop WebSocket client: %s", esp_err_to_name(err));
        }
        err = esp_websocket_client_destroy(client); //Destroy the client instance
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to destroy WebSocket client: %s", esp_err_to_name(err));
        }
        client = NULL;
        if (s_client_event_group) {
            xEventGroupClearBits(s_client_event_group, WEBSOCKET_CONNECTED_BIT | FRAME_ACK_BIT); // Clear all bits on stop
        }
        ESP_LOGI(TAG, "WebSocket client stopped and destroyed.");
    }
    else {
        ESP_LOGI(TAG, "No active webSocket client exists.");
    }
}

// Changed to directly accept uint8_t* and size_t for generic data sending
esp_err_t websocket_send_frame(const uint8_t* data, size_t len)
{
    if (!client) {
        ESP_LOGE(TAG, "WebSocket client not initialized.");
        return ESP_FAIL;
    }
    // Check connection status via event group
    EventBits_t uxBits = xEventGroupGetBits(s_client_event_group);
    if (!(uxBits & WEBSOCKET_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WebSocket client not connected.");
        return ESP_FAIL;
    }

    if (data == NULL || len == 0) {
        ESP_LOGE(TAG, "Invalid data buffer or length provided.");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending binary data frame chunk, size %zu", len);

    int bytes_sent = esp_websocket_client_send_bin(client, (const char*)data, len, ESP_WEBSOCKET_CLIENT_SEND_TIMEOUT_MS_DEFAULT);
    if (bytes_sent < 0) {
        ESP_LOGE(TAG, "Error sending frame chunk via WebSocket: %d)", bytes_sent);
        return ESP_FAIL;
    }
    else if ((size_t)bytes_sent < len) {
        ESP_LOGW(TAG, "Could not send full frame chunk. Sent %d of %zu bytes.", bytes_sent, len);
        return ESP_FAIL;
    }

    // ESP_LOGI(TAG, "Frame chunk sent successfully (%d bytes).", bytes_sent); // Too verbose for chunks
    return ESP_OK;
}

// send text messages, e.g., heartbeat or frame ACK
esp_err_t websocket_send_text(const char* text) {
    if (!client) {
        ESP_LOGE(TAG, "WebSocket client not initialized.");
        return ESP_FAIL;
    }
    EventBits_t uxBits = xEventGroupGetBits(s_client_event_group);
    if (!(uxBits & WEBSOCKET_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WebSocket client not connected.");
        return ESP_FAIL;
    }

    if (text == NULL) {
        ESP_LOGE(TAG, "Invalid text buffer provided.");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending text message: %s", text);
    int bytes_sent = esp_websocket_client_send_text(client, text, strlen(text), ESP_WEBSOCKET_CLIENT_SEND_TIMEOUT_MS_DEFAULT);
    if (bytes_sent < 0) {
        ESP_LOGE(TAG, "Error sending text message via WebSocket (error code: %d)", bytes_sent);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Heartbeat: do experiment with intervals, and if it is needed at all!
esp_err_t websocket_send_heartbeat(void) {
    return websocket_send_text("{\"type\":\"heartbeat\"}");
}

// Simplified check based on event group. Never investigated
bool is_websocket_connected(void) {
    if (s_client_event_group == NULL) return false;
    EventBits_t uxBits = xEventGroupGetBits(s_client_event_group);
    return (uxBits & WEBSOCKET_CONNECTED_BIT) != 0;
}