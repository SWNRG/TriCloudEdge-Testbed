/**
 * @file config.h
 * @brief Configuration for ESP32 application modules
 * 
 * Contains configuration flags to enable/disable various modules
 * and their respective settings for debugging purposes
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "../certificates/secret.h"  // Include sensitive configuration. HAS TO BE CREATED!

#define EXTRA_HEAP_LOGGING 0 // print extensive logs for crash debugging purposes. TOO MUCH PRINTING!

/* MODULE ENABLE FLAGS */
#define MQTT_ENABLED 0
#define WIFI_ENABLED 1
#define WIFI_MAXIMUM_RETRY 5
#define WEBSOCKET_ENABLED 1
#define WEBSOCKET_PORT 80 


#define SAMPLING_INTERVAL_MS 120000  // 2 minutes for publishing interval

#endif // CONFIG_H
