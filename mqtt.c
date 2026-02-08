#include "mqtt.h"
#include "secrets.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static MQTT_CLIENT_T g_state;
static GetDataCallback g_get_data_cb = NULL;

static char g_active_user[16] = "";   // vindo do TB (atributo)
static volatile bool g_have_user = false;

static void set_active_user(const char *u) {
    if (!u) return;
    size_t n = strnlen(u, sizeof(g_active_user)-1);
    if (n == 0) return;
    memset(g_active_user, 0, sizeof(g_active_user));
    memcpy(g_active_user, u, n);
    g_active_user[n] = '\0';
    g_have_user = true;
}

bool mqtt_get_active_user(char *out, size_t out_sz) {
    if (!out || out_sz == 0) return false;
    out[0] = '\0';
    if (!g_have_user || g_active_user[0] == '\0') return false;
    strncpy(out, g_active_user, out_sz - 1);
    out[out_sz - 1] = '\0';
    return true;
}

// --------------------------
// Helpers de tempo
// --------------------------
static inline uint32_t now_ms(void) {
    return (uint32_t)to_ms_since_boot(get_absolute_time());
}

static uint32_t clamp_u32(uint32_t v, uint32_t a, uint32_t b) {
    if (v < a) return a;
    if (v > b) return b;
    return v;
}

// --------------------------
// MQTT callbacks
// --------------------------
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len) {
    (void)arg; (void)tot_len;
    DEBUG_printf("[MQTT] incoming topic=%s\n", topic ? topic : "(null)");
}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    (void)arg;

    static char rx[256];
    static size_t idx = 0;

    if (!data || len == 0) return;

    size_t can = sizeof(rx) - 1 - idx;
    size_t cp = (len < can) ? len : can;
    memcpy(&rx[idx], data, cp);
    idx += cp;
    rx[idx] = '\0';

    if (flags & MQTT_DATA_FLAG_LAST) {
        // Esperado: payload JSON de atributos (ex: {"active_user":"Davi"})
        // Parse simples: procura "active_user"
        const char *p = strstr(rx, "\"active_user\"");
        if (p) {
            const char *q = strchr(p, ':');
            if (q) {
                q++;
                while (*q == ' ' || *q == '\t') q++;
                if (*q == '\"') q++;
                char tmp[16] = {0};
                size_t j = 0;
                while (*q && *q != '\"' && j < sizeof(tmp)-1) {
                    tmp[j++] = *q++;
                }
                tmp[j] = '\0';
                if (tmp[0] != '\0') {
                    set_active_user(tmp);
                    DEBUG_printf("[MQTT] active_user=%s\n", tmp);
                }
            }
        }

        idx = 0;
        rx[0] = '\0';
    }
}

static void mqtt_request_active_user(void) {
    // Requisita atributo active_user via request/1
    // Exemplo TB: publish em v1/devices/me/attributes/request/1 com {"sharedKeys":"active_user"}
    const char *topic = TB_TOPIC_ATTR_REQ_1;
    const char *payload = "{\"sharedKeys\":\"active_user\"}";

    if (!g_state.connected) return;

    err_t e = mqtt_publish(g_state.mqtt_client, topic, payload, (u16_t)strlen(payload), 0, 0, NULL, NULL);
    DEBUG_printf("[MQTT] attr req -> %s (e=%d)\n", topic, (int)e);
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    (void)client;
    MQTT_CLIENT_T *s = (MQTT_CLIENT_T *)arg;

    s->connecting = false;

    if (status == MQTT_CONNECT_ACCEPTED) {
        s->connected = true;
        s->backoff_ms = MQTT_RECONNECT_MIN_MS;
        DEBUG_printf("[MQTT] conectado (ACCEPTED)\n");

        mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, s);

        // subscribe em atributos
        mqtt_subscribe(client, TB_TOPIC_ATTR_UPDATES, 0, NULL, NULL);
        mqtt_subscribe(client, TB_TOPIC_ATTR_RESP_WILDCARD, 0, NULL, NULL);

        // pede active_user imediatamente
        s->last_attr_req_ms = 0;
        mqtt_request_active_user();

    } else {
        s->connected = false;
        DEBUG_printf("[MQTT] falha conexão status=%d\n", (int)status);
        s->next_reconnect_ms = now_ms() + s->backoff_ms;
        s->backoff_ms = clamp_u32(s->backoff_ms * 2, MQTT_RECONNECT_MIN_MS, MQTT_RECONNECT_MAX_MS);
    }
}

static void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    MQTT_CLIENT_T *s = (MQTT_CLIENT_T *)callback_arg;

    if (ipaddr) {
        s->remote_addr = *ipaddr;
        DEBUG_printf("[DNS] %s -> %s\n", name, ipaddr_ntoa(ipaddr));
    } else {
        DEBUG_printf("[DNS] falhou p/ %s\n", name);
    }
}

// --------------------------
// Conexão/reconexão
// --------------------------
static void mqtt_try_connect(MQTT_CLIENT_T *s, const char *client_id) {
    if (!s || s->connecting || s->connected) return;

    // resolve DNS se necessário
    uint32_t t = now_ms();
    if (s->last_dns_ms == 0 || (t - s->last_dns_ms) > DNS_REFRESH_MS) {
        s->last_dns_ms = t;
        DEBUG_printf("[DNS] resolvendo %s...\n", MQTT_SERVER_HOST);

        cyw43_arch_lwip_begin();
        dns_gethostbyname(MQTT_SERVER_HOST, &s->remote_addr, dns_found_cb, s);
        cyw43_arch_lwip_end();
    }

    // tenta conectar
    struct mqtt_connect_client_info_t ci = {0};
    ci.client_id = client_id;
    ci.client_user = TB_ACCESS_TOKEN; // ThingsBoard usa token como user
    ci.client_pass = NULL;
    ci.keep_alive = MQTT_KEEPALIVE_S;

    s->connecting = true;

    DEBUG_printf("[MQTT] tentando conectar %s:%d...\n", ipaddr_ntoa(&s->remote_addr), MQTT_SERVER_PORT);

    cyw43_arch_lwip_begin();
    err_t e = mqtt_client_connect(s->mqtt_client, &s->remote_addr, MQTT_SERVER_PORT,
                                 mqtt_connection_cb, s, &ci);
    cyw43_arch_lwip_end();

    if (e != ERR_OK) {
        s->connecting = false;
        s->connected = false;
        DEBUG_printf("[MQTT] mqtt_client_connect err=%d\n", (int)e);
        s->next_reconnect_ms = now_ms() + s->backoff_ms;
        s->backoff_ms = clamp_u32(s->backoff_ms * 2, MQTT_RECONNECT_MIN_MS, MQTT_RECONNECT_MAX_MS);
    }
}

static void mqtt_publish_telemetry(MQTT_CLIENT_T *s) {
    if (!s || !s->connected || !g_get_data_cb) return;

    char buf[BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));
    g_get_data_cb(buf, sizeof(buf));

    if (buf[0] == '\0') return;

    err_t e = mqtt_publish(s->mqtt_client, s->publish_topic, buf, (u16_t)strlen(buf), 0, 0, NULL, NULL);
    if (e != ERR_OK) {
        uint32_t t = now_ms();
        if (s->last_pub_err_ms == 0 || (t - s->last_pub_err_ms) > 2000) {
            s->last_pub_err_ms = t;
            DEBUG_printf("[MQTT] publish err=%d\n", (int)e);
        }
    }
}

// --------------------------
// API principal
// --------------------------
void mqtt_start_application(const char *publish_topic,
                            const char *client_id,
                            GetDataCallback get_data_cb) {
    memset(&g_state, 0, sizeof(g_state));
    g_state.publish_topic = publish_topic;
    g_get_data_cb = get_data_cb;

    g_state.mqtt_client = mqtt_client_new();
    if (!g_state.mqtt_client) {
        DEBUG_printf("[MQTT] ERRO: mqtt_client_new falhou\n");
        return;
    }

    g_state.backoff_ms = MQTT_RECONNECT_MIN_MS;
    g_state.next_reconnect_ms = 0;

    // loop principal (roda dentro da sua task)
    for (;;) {
        uint32_t t = now_ms();

        if (!g_state.connected && !g_state.connecting) {
            if (g_state.next_reconnect_ms == 0 || t >= g_state.next_reconnect_ms) {
                mqtt_try_connect(&g_state, client_id);
            }
        }

        if (g_state.connected) {
            // publica telemetria
            if (t - g_state.last_publish_ms >= PUB_DELAY_MS) {
                g_state.last_publish_ms = t;
                mqtt_publish_telemetry(&g_state);
            }

            // re-request active_user a cada 20s se ainda não chegou
            if (!g_have_user) {
                if (g_state.last_attr_req_ms == 0 || (t - g_state.last_attr_req_ms) > 20000) {
                    g_state.last_attr_req_ms = t;
                    mqtt_request_active_user();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
