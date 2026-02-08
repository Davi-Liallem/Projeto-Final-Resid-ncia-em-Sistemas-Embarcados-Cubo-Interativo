#include "local_report.h"
#include "secrets.h"

#if LOCAL_REPORT_ENABLE

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"

// Se seu mic_get_last estiver em outro header, ajuste aqui:
#include "mic.h"

// ============================
// Config
// ============================
#ifndef LOCAL_SERVER_IP
#define LOCAL_SERVER_IP   "192.168.0.10"
#endif

#ifndef LOCAL_SERVER_PORT
#define LOCAL_SERVER_PORT 5000
#endif

#define LR_QUEUE_LEN     24
#define LR_PAYLOAD_MAX   256
#define LR_USER_MAX      32
#define LR_SERIAL_BUF    64

#define LR_TASK_STACK    2048
#define LR_TASK_PRIO     (tskIDLE_PRIORITY + 2)

// ============================
// Tipos
// ============================
typedef struct {
    char json[LR_PAYLOAD_MAX];
} lr_msg_t;

// ============================
// Estado interno
// ============================
static QueueHandle_t g_lr_q = NULL;
static TaskHandle_t  g_lr_task = NULL;

static struct udp_pcb *g_pcb = NULL;
static ip_addr_t g_dst_ip;

static char g_user[LR_USER_MAX] = {0};

static bool     g_session_open = false;
static uint32_t g_session_id = 0;
static uint32_t g_session_start_ts = 0;

// ============================
// Utils
// ============================
static uint32_t lr_now_ms(void) {
    return (uint32_t)(to_ms_since_boot(get_absolute_time()));
}

static const char* safe_user(void) {
    // Se não tiver user definido, manda vazio (servidor /live mapeia)
    return (g_user[0] == '\0') ? "" : g_user;
}

static void lr_send_json(const char *json) {
    if (!g_lr_q || !json) return;

    lr_msg_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.json, json, sizeof(m.json) - 1);
    m.json[sizeof(m.json) - 1] = '\0';

    if (xQueueSend(g_lr_q, &m, 0) != pdTRUE) {
        // fila cheia: descarta 1 item e tenta de novo (não perde STOP)
        lr_msg_t drop;
        (void)xQueueReceive(g_lr_q, &drop, 0);
        (void)xQueueSend(g_lr_q, &m, 0);
    }
}

static void lr_udp_init_once(void) {
    if (g_pcb) return;

    g_pcb = udp_new();
    if (!g_pcb) {
        printf("[LOCAL] ERRO: udp_new falhou\n");
        return;
    }

    ipaddr_aton(LOCAL_SERVER_IP, &g_dst_ip);
    printf("[LOCAL] UDP pronto -> %s:%d\n", LOCAL_SERVER_IP, LOCAL_SERVER_PORT);
}

static void lr_udp_send_now(const char *json) {
    if (!g_pcb) return;
    if (!json) return;

    size_t n = strlen(json);
    if (n == 0) return;
    if (n > (LR_PAYLOAD_MAX - 1)) n = (LR_PAYLOAD_MAX - 1);

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)n, PBUF_RAM);
    if (!p) return;

    memcpy(p->payload, json, n);
    udp_sendto(g_pcb, p, &g_dst_ip, LOCAL_SERVER_PORT);
    pbuf_free(p);
}

// ============================
// Task
// ============================
static void lr_task_fn(void *p) {
    (void)p;
    lr_udp_init_once();

    for (;;) {
        lr_msg_t m;
        if (xQueueReceive(g_lr_q, &m, portMAX_DELAY) == pdTRUE) {
            lr_udp_send_now(m.json);
        }
    }
}

// ============================
// API pública
// ============================
void local_report_init(void) {
    if (g_lr_q) return;

    g_lr_q = xQueueCreate(LR_QUEUE_LEN, sizeof(lr_msg_t));
    if (!g_lr_q) {
        printf("[LOCAL] ERRO: xQueueCreate falhou\n");
        return;
    }

    BaseType_t ok = xTaskCreate(lr_task_fn, "lr_udp", LR_TASK_STACK, NULL, LR_TASK_PRIO, &g_lr_task);
    if (ok != pdPASS) {
        printf("[LOCAL] ERRO: xTaskCreate falhou\n");
        g_lr_task = NULL;
        return;
    }

    printf("[LOCAL] init OK\n");
}

void local_report_new_session(void) {
    // no-op seguro (compat)
}

TaskHandle_t local_report_task_handle(void) {
    return g_lr_task;
}

TaskHandle_t local_report_get_task_handle(void) {
    // compat com teu HealthTask
    return g_lr_task;
}

bool local_report_has_user(void) {
    return (g_user[0] != '\0');
}

void local_report_set_user(const char *user) {
    if (!user) return;

    // aceita: "USER nome", "USER:nome" ou só "nome"
    const char *p = user;

    // pular espaços
    while (*p && isspace((unsigned char)*p)) p++;

    // se começar com USER...
    if ((p[0]=='U'||p[0]=='u') && (p[1]=='S'||p[1]=='s') && (p[2]=='E'||p[2]=='e') && (p[3]=='R'||p[3]=='r')) {
        p += 4;
        while (*p && (isspace((unsigned char)*p) || *p==':' )) p++;
    }

    char tmp[LR_USER_MAX];
    memset(tmp, 0, sizeof(tmp));

    size_t n = 0;
    for (; n < (LR_USER_MAX - 1) && p[n] != '\0'; n++) {
        char c = p[n];
        if (c == '\r' || c == '\n') break;
        tmp[n] = c;
    }
    tmp[n] = '\0';

    // remover espaços no fim
    while (n > 0 && isspace((unsigned char)tmp[n-1])) {
        tmp[n-1] = '\0';
        n--;
    }

    if (tmp[0] == '\0') return;

    strncpy(g_user, tmp, sizeof(g_user) - 1);
    g_user[sizeof(g_user) - 1] = '\0';

    printf("[LOCAL] user set: %s\n", g_user);
}

void local_report_clear_user(void) {
    g_user[0] = '\0';
}

// =====================
// Serial: digita nome e Enter
// =====================
void local_report_process_serial(void) {
    static char buf[LR_SERIAL_BUF];
    static int idx = 0;

    int c = getchar_timeout_us(0);
    while (c != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            if (idx > 0) {
                buf[idx] = '\0';
                local_report_set_user(buf);
                idx = 0;
            }
        } else {
            if (idx < (int)(sizeof(buf) - 1)) {
                buf[idx++] = (char)c;
            } else {
                idx = 0;
            }
        }
        c = getchar_timeout_us(0);
    }
}

// =====================
// Eventos do jogo
// =====================
void local_report_event_start(const char *modo) {
    uint32_t ts = lr_now_ms();

    if (g_session_open) {
        printf("[LOCAL] start ignorado: sessao ja aberta\n");
        return;
    }

    g_session_open = true;
    g_session_id++;
    g_session_start_ts = ts;

    char j[LR_PAYLOAD_MAX];
    snprintf(j, sizeof(j),
             "{\"event\":\"start\",\"user\":\"%s\",\"session\":%u,\"modo\":\"%s\",\"ts\":%u}",
             safe_user(), (unsigned)g_session_id, modo ? modo : "", (unsigned)ts);

    lr_send_json(j);
}

static void lr_get_mic(float *mf, float *mi, uint8_t *mt) {
    if (mf) *mf = 0.0f;
    if (mi) *mi = 0.0f;
    if (mt) *mt = 0;

    // Se sua função tiver outro nome, ajuste aqui
    mic_get_last(mf, mi, mt);
}

void local_report_event_ok(uint32_t last_ms, uint32_t avg_ms,
                           uint32_t ok_total, uint32_t err_total,
                           const char *modo) {
    if (!g_session_open) return;

    float mf, mi; uint8_t mt;
    lr_get_mic(&mf, &mi, &mt);

    char j[LR_PAYLOAD_MAX];
    snprintf(j, sizeof(j),
             "{\"event\":\"ok\",\"user\":\"%s\",\"session\":%u,\"modo\":\"%s\","
             "\"mic_freq\":%.1f,\"mic_int\":%.3f,\"mic_type\":%u,"
             "\"last_ms\":%u,\"avg_ms\":%u,\"ok_total\":%u,\"err_total\":%u,\"ts\":%u}",
             safe_user(), (unsigned)g_session_id, modo ? modo : "",
             mf, mi, (unsigned)mt,
             (unsigned)last_ms, (unsigned)avg_ms,
             (unsigned)ok_total, (unsigned)err_total,
             (unsigned)lr_now_ms());

    lr_send_json(j);
}

void local_report_event_err(uint32_t last_ms,
                            uint32_t ok_total, uint32_t err_total,
                            const char *modo) {
    if (!g_session_open) return;

    float mf, mi; uint8_t mt;
    lr_get_mic(&mf, &mi, &mt);

    char j[LR_PAYLOAD_MAX];
    snprintf(j, sizeof(j),
             "{\"event\":\"err\",\"user\":\"%s\",\"session\":%u,\"modo\":\"%s\","
             "\"mic_freq\":%.1f,\"mic_int\":%.3f,\"mic_type\":%u,"
             "\"last_ms\":%u,\"ok_total\":%u,\"err_total\":%u,\"ts\":%u}",
             safe_user(), (unsigned)g_session_id, modo ? modo : "",
             mf, mi, (unsigned)mt,
             (unsigned)last_ms,
             (unsigned)ok_total, (unsigned)err_total,
             (unsigned)lr_now_ms());

    lr_send_json(j);
}

void local_report_event_stop(uint32_t ok_total, uint32_t err_total,
                             const char *modo) {
    uint32_t ts = lr_now_ms();
    if (!g_session_open) return;

    uint32_t total_ms = (ts >= g_session_start_ts) ? (ts - g_session_start_ts) : 0;

    float mf, mi; uint8_t mt;
    lr_get_mic(&mf, &mi, &mt);

    char j[LR_PAYLOAD_MAX];
    snprintf(j, sizeof(j),
             "{\"event\":\"stop\",\"user\":\"%s\",\"session\":%u,\"modo\":\"%s\","
             "\"mic_freq\":%.1f,\"mic_int\":%.3f,\"mic_type\":%u,"
             "\"ok_total\":%u,\"err_total\":%u,\"total_ms\":%u,\"ts\":%u}",
             safe_user(), (unsigned)g_session_id, modo ? modo : "",
             mf, mi, (unsigned)mt,
             (unsigned)ok_total, (unsigned)err_total,
             (unsigned)total_ms, (unsigned)ts);

    lr_send_json(j);

    g_session_open = false;
    g_session_start_ts = 0;
}

#endif // LOCAL_REPORT_ENABLE
