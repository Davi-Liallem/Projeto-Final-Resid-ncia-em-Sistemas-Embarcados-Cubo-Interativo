#ifndef CUBO_MQTT_CLIENT_H
#define CUBO_MQTT_CLIENT_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

#include "lwip/ip_addr.h"
#include "lwip/dns.h"
#include "lwip/apps/mqtt.h"

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

// Debug (se quiser silenciar: #define DEBUG_printf(...) ((void)0))
#define DEBUG_printf printf

// ==========================
// ThingsBoard Cloud MQTT
// ==========================
#define MQTT_SERVER_HOST "mqtt.thingsboard.cloud"
#define MQTT_SERVER_PORT 1883

// Telemetria
#define BUFFER_SIZE   512
#define PUB_DELAY_MS  5000

// Reconexão
#define MQTT_KEEPALIVE_S         30
#define MQTT_RECONNECT_MIN_MS   1000
#define MQTT_RECONNECT_MAX_MS  15000
#define DNS_REFRESH_MS        60000

// Topics ThingsBoard
#define TB_TOPIC_ATTR_UPDATES        "v1/devices/me/attributes"
#define TB_TOPIC_ATTR_RESP_WILDCARD  "v1/devices/me/attributes/response/+"
#define TB_TOPIC_ATTR_REQ_1          "v1/devices/me/attributes/request/1"

typedef struct MQTT_CLIENT_T {
    ip_addr_t       remote_addr;
    mqtt_client_t  *mqtt_client;

    const char     *publish_topic;

    volatile bool   connected;     // true só quando ACCEPTED
    volatile bool   connecting;    // true enquanto handshake acontece
    volatile bool   pub_inflight;  // true quando já tem publish pendente

    uint32_t        last_publish_ms;
    uint32_t        next_reconnect_ms;
    uint32_t        backoff_ms;
    uint32_t        last_dns_ms;
    uint32_t        last_attr_req_ms;

    uint32_t        last_pub_err_ms; // evita spam de log
} MQTT_CLIENT_T;

typedef void (*GetDataCallback)(char *buffer, size_t buffer_size);

// API principal: essa função roda um loop interno (não retorna)
void mqtt_start_application(const char *publish_topic,
                            const char *client_id,
                            GetDataCallback get_data_cb);

// Lê active_user recebido do ThingsBoard
bool mqtt_get_active_user(char *out, size_t out_sz);

#endif // CUBO_MQTT_CLIENT_H
