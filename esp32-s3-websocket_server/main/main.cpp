/**
 * @file main.c
 * @version 31
 * @brief Main application
 */
#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_netif.h"

#include "config.h"
#include "storage_manager.h"
#include "image_processor.h" 
#include "websocket_server.h" 

static const char* TAG = "MAIN";

// Wrap C headers for correct linkage
extern "C" {
#include "wifi.h" 
#if MQTT_ENABLED
#include "mqtt.h" 
#endif
}

// Wrap C headers for correct linkage
extern "C" void app_main(void) {
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "Starting application...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    if (storage_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize storage manager. STOP!");
        while(1);
    }

    if (WIFI_ENABLED) {
        wifi_init_sta();
        if (!wifi_is_connected()) {
            ESP_LOGE(TAG, "WiFi failed! Most services will fail!");
        }
    }

#if WEBSOCKET_ENABLED
    if (WIFI_ENABLED && wifi_is_connected()) {
        if (start_websocket_server() == ESP_OK) {
            esp_netif_ip_info_t ip_info;
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif) {
                esp_netif_get_ip_info(netif, &ip_info);
                ESP_LOGI(TAG, "\033[1;36m===== WEBSOCKET UP TO IP ADDRESS =====\033[0m");
                ESP_LOGI(TAG, "\033[1;36m   ws://" IPSTR ":%d/ws \033[0m", IP2STR(&ip_info.ip), WEBSOCKET_PORT);
                ESP_LOGI(TAG, "\033[1;36m======================================\033[0m");
            }
        } else {
            ESP_LOGE(TAG, "WebSocket server failed to start.");
        }
    }
#endif

#if MQTT_ENABLED
    esp_mqtt_client_handle_t client = mqtt_aws_init();
    if (client) {
        if (mqtt_start(client) == ESP_OK) {
             //ESP_LOGI(TAG, "AWS IoT MQTT sub started..."); // Msg already in mqtt_start
             
        }
    }
#endif

// whie(true) is not currently doing much, can be extended...
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}