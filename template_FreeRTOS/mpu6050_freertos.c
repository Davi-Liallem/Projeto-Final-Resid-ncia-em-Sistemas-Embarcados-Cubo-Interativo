/**
 * @file main.c
 * @author Davi Liallem Passos dos Santos
 * @date 2026
 * @brief Firmware do Cubo Interativo de Foco e Emoções
 *
 * Este firmware implementa a lógica principal do Cubo Interativo,
 * utilizando FreeRTOS para execução concorrente de tarefas,
 * integração de sensores, atuadores e comunicação IoT.
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include "local_report.h"

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/watchdog.h"

#include "ssd1306.h"
#include "mpu6050_i2c.h"

#include "FreeRTOS.h"
#include "task.h"

#include "pico/cyw43_arch.h"
#include "mqtt.h"
#include "secrets.h"

#include "mic.h"

// ==========================
// CONFIG: manter MQTT sem mexer no resto
// ==========================
#ifndef USE_MQTT
#define USE_MQTT 1
#endif

// ==========================
// CONFIGURAÇÕES DE HARDWARE
// ==========================
#define I2C_PORT1 i2c1
#define I2C_SDA1  14
#define I2C_SCL1  15

#define BTN_START 5
#define BTN_STOP  6

#define BUZZER_PIN 21

// LEDs – MAPEAMENTO FINAL (VALIDADO)
#define PIN_LED_TOPO   17  // amarelo
#define PIN_LED_BASE   20  // vermelho
#define PIN_LED_FRENTE 18  // azul I
#define PIN_LED_TRAS   16  // verde (TRAS)
#define PIN_LED_ESQ    19  // branco (ESQ)
#define PIN_LED_DIR    4   // azul II

// ==========================
// LOG COM LIMITE
// ==========================
static inline bool log_every_ms(uint32_t ms) {
    static TickType_t last = 0;
    TickType_t now = xTaskGetTickCount();
    if ((now - last) >= pdMS_TO_TICKS(ms)) { last = now; return true; }
    return false;
}
#define LOG_5S(...) do { if (log_every_ms(5000)) printf(__VA_ARGS__); } while(0)

// ==========================
// TIPOS DO CUBO
// ==========================
typedef enum {
    FACE_MOVENDO = -1,
    FACE_FRENTE = 0,
    FACE_TRAS,
    FACE_ESQ,
    FACE_DIR,
    FACE_BASE,
    FACE_TOPO
} face_t;

typedef enum { ESTADO_PARADO = 0, ESTADO_RODANDO } estado_t;

// ==========================
// JOGO (menu/modos)
// ==========================
typedef enum { MODE_LVL1 = 0, MODE_MEM_NORMAL, MODE_MEM_RAPIDO } menu_mode_t;

typedef enum {
    ST_MENU = 0,
    ST_WAIT_YELLOW,
    ST_L1_ACTIVE,
    ST_MEM_SHOW,
    ST_MEM_INPUT,
} game_state_t;

// ==========================
// CONFIG DO JOGO
// ==========================
static const float LIMIAR_G = 0.60f;
static const int   ESTABILIDADE_MIN = 6;

static const uint32_t LOOP_MS         = 40;
static const uint32_t HOLD_MS_A       = 900;   // A longo: troca modo
static const uint32_t HOLD_MS_B       = 1200;  // B longo: encerra sessão
static const uint32_t YELLOW_READY_MS = 450;

static uint32_t SHOW_ON_MS  = 450;
static uint32_t SHOW_OFF_MS = 250;

static uint32_t SHOW_ON_MS_FAST  = 260;
static uint32_t SHOW_OFF_MS_FAST = 140;

static const uint32_t BLINK_MS = 450;

static uint32_t OLED_OK_MS  = 520;
static uint32_t OLED_ERR_MS = 650;

static const uint32_t L2_YOUR_TURN_MS = 300;
static const bool YELLOW_FEEDBACK_ON = true;

static int fast_rounds_done = 0;
static const int FAST_ROUNDS_TOTAL = 5;

// OLED: limite de atualização (melhora MUITO a fluidez)
static const uint32_t OLED_REFRESH_MS = 350;  // mais devagar e confortável


// ==========================
// VARIÁVEIS DO SISTEMA
// ==========================
static estado_t estado = ESTADO_PARADO;

static menu_mode_t mode_sel = MODE_LVL1;
static game_state_t st = ST_MENU;

static int mem_len = 2;
static const int MEM_LEN_MIN = 2;
static const int MEM_LEN_MAX = 4;

#define MAX_SEQ 4
static face_t seq[MAX_SEQ];
static int input_idx = 0;
static bool repeat_same_seq = false;
static face_t last_input_face = FACE_MOVENDO;

static face_t alvo_l1 = FACE_FRENTE;
static face_t last_l1_target = FACE_MOVENDO;

static face_t last_face_lida = FACE_MOVENDO;
static int estabilidade_cont = 0;
static face_t face_base_estavel = FACE_MOVENDO;

static bool yellow_timer_active = false;
static absolute_time_t yellow_t0;

// textos (mantidos p/ MQTT e debug)
static char texto_modo[20];
static char texto_face[12];
static char texto_alvo[12];
static char texto_info[24];

// Wi-Fi status (simples)
static volatile bool g_wifi_ok = false;

// Handles
static TaskHandle_t g_game_task = NULL;
static TaskHandle_t g_mic_task  = NULL;
static TaskHandle_t g_mqtt_task = NULL;

// ==========================
// MÉTRICAS
// ==========================
static uint32_t g_ok_total = 0;
static uint32_t g_err_total = 0;

static uint32_t g_round_start_ms = 0;
static uint32_t g_last_round_ms  = 0;
static uint32_t g_sum_ok_ms      = 0;

static inline void metrics_reset_all(void) {
    g_ok_total = 0;
    g_err_total = 0;
    g_round_start_ms = 0;
    g_last_round_ms = 0;
    g_sum_ok_ms = 0;
}
static inline void metrics_round_start(void) {
    g_round_start_ms = to_ms_since_boot(get_absolute_time());
}
static inline void metrics_round_finish_ok(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    g_last_round_ms = (g_round_start_ms != 0) ? (now - g_round_start_ms) : 0;
    g_ok_total++;
    g_sum_ok_ms += g_last_round_ms;
    g_round_start_ms = 0;
}
static inline void metrics_round_finish_err(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    g_last_round_ms = (g_round_start_ms != 0) ? (now - g_round_start_ms) : 0;
    g_err_total++;
    g_round_start_ms = 0;
}
static inline uint32_t metrics_avg_ms(void) {
    if (g_ok_total == 0) return 0;
    return (uint32_t)(g_sum_ok_ms / g_ok_total);
}

// ==========================
// NOME DO MODO (p/ local_report)
// ==========================
static const char* mode_to_str(menu_mode_t m) {
    switch (m) {
        case MODE_LVL1:       return "NIVEL 1";
        case MODE_MEM_NORMAL: return "MEMORIA";
        case MODE_MEM_RAPIDO: return "MEMORIA RAPIDA";
        default:              return "UNK";
    }
}

#if USE_MQTT
// ==========================
// CALLBACK PARA MQTT
// ==========================
static void cubo_data_to_json_callback(char *buffer, size_t buffer_size) {
    char user[16];
    bool has_user = mqtt_get_active_user(user, sizeof(user));
    if (!has_user || user[0] == '\0') has_user = false;

    float mf=0.0f, mi=0.0f;
    uint8_t mt=0;
    mic_get_last(&mf, &mi, &mt);

    snprintf(buffer, buffer_size,
        "{"
        "\"estado\":%d,"
        "\"user\":\"%s\","
        "\"modo\":\"%s\","
        "\"alvo\":\"%s\","
        "\"face\":\"%s\","
        "\"info\":\"%s\","
        "\"mic_freq\":%.1f,"
        "\"mic_int\":%.3f,"
        "\"mic_type\":%u,"
        "\"ok_total\":%u,"
        "\"err_total\":%u,"
        "\"last_ms\":%u,"
        "\"avg_ms\":%u"
        "}",
        (int)estado,
        has_user ? user : "",
        texto_modo, texto_alvo, texto_face, texto_info,
        mf, mi, (unsigned)mt,
        (unsigned)g_ok_total,
        (unsigned)g_err_total,
        (unsigned)g_last_round_ms,
        (unsigned)metrics_avg_ms()
    );
}
#endif

// ==========================
// UTILS
// ==========================
static void face_to_str(face_t f, char *dest) {
    switch (f) {
        case FACE_FRENTE: strcpy(dest, "FRENTE"); break;
        case FACE_TRAS:   strcpy(dest, "TRAS"); break;
        case FACE_ESQ:    strcpy(dest, "ESQ"); break;
        case FACE_DIR:    strcpy(dest, "DIR"); break;
        case FACE_BASE:   strcpy(dest, "BASE"); break;
        case FACE_TOPO:   strcpy(dest, "TOPO"); break;
        default:          strcpy(dest, "MOV"); break;
    }
}
static int face_to_led_pin(face_t f) {
    switch (f) {
        case FACE_FRENTE: return PIN_LED_FRENTE;
        case FACE_TRAS:   return PIN_LED_TRAS;
        case FACE_ESQ:    return PIN_LED_ESQ;
        case FACE_DIR:    return PIN_LED_DIR;
        case FACE_BASE:   return PIN_LED_BASE;
        case FACE_TOPO:   return PIN_LED_TOPO;
        default:          return -1;
    }
}
static void all_leds_off(void) {
    gpio_put(PIN_LED_FRENTE, 0);
    gpio_put(PIN_LED_TRAS,   0);
    gpio_put(PIN_LED_ESQ,    0);
    gpio_put(PIN_LED_DIR,    0);
    gpio_put(PIN_LED_BASE,   0);
    gpio_put(PIN_LED_TOPO,   0);
}
static void led_on(face_t f) {
    all_leds_off();
    int p = face_to_led_pin(f);
    if (p >= 0) gpio_put(p, 1);
}

// ==========================
// BUZZER
// ==========================
static void beep_ok(void) {
    for (int i = 0; i < 3; i++) {
        gpio_put(BUZZER_PIN, 1); vTaskDelay(pdMS_TO_TICKS(55));
        gpio_put(BUZZER_PIN, 0); vTaskDelay(pdMS_TO_TICKS(55));
    }
}
static void beep_err(void) {
    gpio_put(BUZZER_PIN, 1); vTaskDelay(pdMS_TO_TICKS(240));
    gpio_put(BUZZER_PIN, 0); vTaskDelay(pdMS_TO_TICKS(120));
    gpio_put(BUZZER_PIN, 1); vTaskDelay(pdMS_TO_TICKS(240));
    gpio_put(BUZZER_PIN, 0);
}
static void beep_start(void) {
    gpio_put(BUZZER_PIN, 1); vTaskDelay(pdMS_TO_TICKS(60));
    gpio_put(BUZZER_PIN, 0); vTaskDelay(pdMS_TO_TICKS(60));
    gpio_put(BUZZER_PIN, 1); vTaskDelay(pdMS_TO_TICKS(60));
    gpio_put(BUZZER_PIN, 0);
}

// ==========================
// OLED helpers (limpos + rate limit)
// ==========================
static void oled_clear_header(void) {
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "Curva Terapeutica");
}
static bool oled_can_refresh(bool force) {
    static TickType_t last = 0;
    TickType_t now = xTaskGetTickCount();
    if (force || (now - last) >= pdMS_TO_TICKS(OLED_REFRESH_MS)) {
        last = now;
        return true;
    }
    return false;
}
static void oled_msg(const char *l1, const char *l2, uint32_t ms) {
    oled_clear_header();
    if (l1) ssd1306_draw_string(0, 20, (char*)l1);
    if (l2) ssd1306_draw_string(0, 36, (char*)l2);
    ssd1306_show();
    vTaskDelay(pdMS_TO_TICKS(ms));
}
static void oled_your_turn(const char *titulo) {
    oled_clear_header();
    if (titulo) ssd1306_draw_string(0, 12, (char*)titulo);
    ssd1306_draw_string(0, 28, "SUA VEZ!");
    ssd1306_draw_string(0, 44, "Repita a sequencia");
    ssd1306_show();
    vTaskDelay(pdMS_TO_TICKS(L2_YOUR_TURN_MS));
}

// ==========================
// MPU: detectar face base
// ==========================
static face_t detectar_face_base_raw(void) {
    int16_t accel[3], gyro[3], temp;
    mpu6050_read_raw(accel, gyro, &temp);

    float ax = accel[0] / ACCEL_SENS_2G;
    float ay = accel[1] / ACCEL_SENS_2G;
    float az = accel[2] / ACCEL_SENS_2G;

    float abs_ax = fabsf(ax);
    float abs_ay = fabsf(ay);
    float abs_az = fabsf(az);

    if (abs_ax > abs_ay && abs_ax > abs_az && abs_ax > LIMIAR_G) {
        return (ax > 0) ? FACE_ESQ : FACE_DIR;
    } else if (abs_ay > abs_ax && abs_ay > abs_az && abs_ay > LIMIAR_G) {
        return (ay > 0) ? FACE_FRENTE : FACE_TRAS;
    } else if (abs_az > abs_ax && abs_az > abs_ay && abs_az > LIMIAR_G) {
        return (az > 0) ? FACE_TOPO : FACE_BASE;
    }
    return FACE_MOVENDO;
}
static void atualizar_face_estavel(void) {
    face_t f = detectar_face_base_raw();

    if (f == FACE_MOVENDO) {
        estabilidade_cont = 0;
        last_face_lida = FACE_MOVENDO;
        face_base_estavel = FACE_MOVENDO;
        return;
    }

    if (f == last_face_lida) {
        if (estabilidade_cont < 100) estabilidade_cont++;
    } else {
        estabilidade_cont = 0;
        last_face_lida = f;
    }

    if (estabilidade_cont >= ESTABILIDADE_MIN) {
        face_base_estavel = f;
    }
}
static void yellow_timer_update(void) {
    if (face_base_estavel == FACE_TOPO) {
        if (!yellow_timer_active) { yellow_timer_active = true; yellow_t0 = get_absolute_time(); }
    } else {
        yellow_timer_active = false;
    }
}
static bool yellow_ready(void) {
    if (face_base_estavel != FACE_TOPO) return false;
    if (!yellow_timer_active) return false;
    return absolute_time_diff_us(yellow_t0, get_absolute_time()) >= (int64_t)YELLOW_READY_MS * 1000;
}

// ==========================
// BOTÃO: clique curto/longo
// ==========================
typedef struct { bool last_down; absolute_time_t t_down; bool long_fired; } btn_hold_t;

static void btn_hold_init(btn_hold_t *b) {
    b->last_down = false;
    b->t_down = get_absolute_time();
    b->long_fired = false;
}
static int btn_event_ms(btn_hold_t *bh, bool down_now, uint32_t hold_ms) {
    int ev = -1;

    if (!bh->last_down && down_now) { bh->t_down = get_absolute_time(); bh->long_fired = false; }

    if (bh->last_down && down_now && !bh->long_fired) {
        int64_t held_us = absolute_time_diff_us(bh->t_down, get_absolute_time());
        if (held_us >= (int64_t)hold_ms * 1000) { bh->long_fired = true; ev = 1; }
    }

    if (bh->last_down && !down_now) { if (!bh->long_fired) ev = 0; }

    bh->last_down = down_now;
    return ev;
}

// ==========================
// ALEATORIEDADE
// ==========================
static face_t alvo_aleatorio_sem_amarelo(face_t evita) {
    for (int tent = 0; tent < 20; tent++) {
        int r = rand() % 5;
        face_t f = FACE_FRENTE;
        switch (r) {
            case 0: f = FACE_FRENTE; break;
            case 1: f = FACE_TRAS;   break;
            case 2: f = FACE_ESQ;    break;
            case 3: f = FACE_DIR;    break;
            default:f = FACE_BASE;   break;
        }
        if (f != evita) return f;
    }
    return FACE_BASE;
}
static int get_neighbors(face_t f, face_t out[4]) {
    switch (f) {
        case FACE_TOPO:
        case FACE_BASE:
            out[0]=FACE_FRENTE; out[1]=FACE_TRAS; out[2]=FACE_ESQ; out[3]=FACE_DIR; return 4;
        case FACE_FRENTE:
        case FACE_TRAS:
            out[0]=FACE_TOPO; out[1]=FACE_BASE; out[2]=FACE_ESQ; out[3]=FACE_DIR; return 4;
        case FACE_ESQ:
        case FACE_DIR:
            out[0]=FACE_TOPO; out[1]=FACE_BASE; out[2]=FACE_FRENTE; out[3]=FACE_TRAS; return 4;
        default:
            out[0]=FACE_FRENTE; out[1]=FACE_TRAS; out[2]=FACE_ESQ; out[3]=FACE_DIR; return 4;
    }
}
static face_t proxima_face_vizinha_sem_topo(face_t atual, face_t evita) {
    face_t nb[4];
    int n = get_neighbors(atual, nb);

    for (int tent = 0; tent < 40; tent++) {
        face_t f = nb[rand() % n];
        if (f == FACE_TOPO) continue;
        if (f == evita) continue;
        return f;
    }
    for (int i = 0; i < n; i++) {
        if (nb[i] != FACE_TOPO && nb[i] != evita) return nb[i];
    }
    return FACE_BASE;
}

// ==========================
// JOGO helpers
// ==========================
static void go_wait_yellow(void) {
    input_idx = 0;
    last_input_face = FACE_MOVENDO;
    yellow_timer_active = false;
    st = ST_WAIT_YELLOW;
}
static void feedback_ok_go_yellow(const char *msg2) {
    if (YELLOW_FEEDBACK_ON) led_on(FACE_TOPO);
    beep_ok();
    oled_msg("ACERTO!", msg2 ? msg2 : "Volte ao AMARELO", OLED_OK_MS);
    repeat_same_seq = false;
    go_wait_yellow();
}
static void feedback_err_repeat_go_yellow(const char *msg2) {
    if (YELLOW_FEEDBACK_ON) led_on(FACE_TOPO);
    beep_err();
    oled_msg("ERRO!", msg2 ? msg2 : "Repete a MESMA", OLED_ERR_MS);
    go_wait_yellow();
}
static void lvl1_new_target(void) {
    alvo_l1 = alvo_aleatorio_sem_amarelo(last_l1_target);
    last_l1_target = alvo_l1;
}
static void mem_generate_sequence(void) {
    if (repeat_same_seq) return;

    face_t cur = FACE_TOPO;
    face_t prev = FACE_MOVENDO;

    for (int i = 0; i < mem_len; i++) {
        face_t next = proxima_face_vizinha_sem_topo(cur, prev);
        seq[i] = next;
        prev = cur;
        cur = next;
    }
}
static void mem_show_sequence(bool rapido) {
    uint32_t on_ms  = rapido ? SHOW_ON_MS_FAST  : SHOW_ON_MS;
    uint32_t off_ms = rapido ? SHOW_OFF_MS_FAST : SHOW_OFF_MS;

    oled_clear_header();
    ssd1306_draw_string(0, 12, rapido ? "MEMORIA RAPIDA" : "MEMORIA");
    ssd1306_draw_string(0, 28, "OBSERVE...");
    char buf[20];
    snprintf(buf, sizeof(buf), "%d passos", mem_len);
    ssd1306_draw_string(0, 44, buf);
    ssd1306_show();

    for (int i = 0; i < mem_len; i++) {
        led_on(seq[i]);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        all_leds_off();
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

// ==========================
// INIT HW
// ==========================
static void hw_init(void)
{
    stdio_init_all();

    i2c_init(I2C_PORT1, 400000);
    gpio_set_function(I2C_SDA1, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL1, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA1);
    gpio_pull_up(I2C_SCL1);

    ssd1306_init(I2C_PORT1);
    oled_clear_header();
    ssd1306_draw_string(0, 20, "Inicializando...");
    ssd1306_show();

    mpu6050_setup_i2c();
    mpu6050_set_accel_range(0);
    if (!mpu6050_test()) {
        ssd1306_clear();
        ssd1306_draw_string(0, 20, "ERRO MPU6050!");
        ssd1306_show();
        while (true) tight_loop_contents();
    }

    gpio_init(BTN_START); gpio_set_dir(BTN_START, GPIO_IN); gpio_pull_up(BTN_START);
    gpio_init(BTN_STOP);  gpio_set_dir(BTN_STOP,  GPIO_IN); gpio_pull_up(BTN_STOP);

    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);

    gpio_init(PIN_LED_FRENTE); gpio_set_dir(PIN_LED_FRENTE, GPIO_OUT);
    gpio_init(PIN_LED_TRAS);   gpio_set_dir(PIN_LED_TRAS,   GPIO_OUT);
    gpio_init(PIN_LED_ESQ);    gpio_set_dir(PIN_LED_ESQ,    GPIO_OUT);
    gpio_init(PIN_LED_DIR);    gpio_set_dir(PIN_LED_DIR,    GPIO_OUT);
    gpio_init(PIN_LED_BASE);   gpio_set_dir(PIN_LED_BASE,   GPIO_OUT);
    gpio_init(PIN_LED_TOPO);   gpio_set_dir(PIN_LED_TOPO,   GPIO_OUT);
    all_leds_off();

    srand((unsigned)to_us_since_boot(get_absolute_time()));

    estado = ESTADO_PARADO;
    mode_sel = MODE_LVL1;
    st = ST_MENU;
    mem_len = MEM_LEN_MIN;
    repeat_same_seq = false;
    fast_rounds_done = 0;

    strcpy(texto_modo, "MENU");
    strcpy(texto_face, "MOV");
    strcpy(texto_alvo, "-");
    strcpy(texto_info, "-");

    metrics_reset_all();
}

// ==========================
// Wi-Fi robusto (retry)
// ==========================
static void wifi_connect_with_retry(void)
{
    g_wifi_ok = false;

    printf("Iniciando Wi-Fi...\n");
    if (cyw43_arch_init()) {
        printf("Erro ao inicializar chip Wi-Fi\n");
        while (true) tight_loop_contents();
    }

    cyw43_arch_enable_sta_mode();
    sleep_ms(300);

    const int MAX_TENTATIVAS = 10;
    for (int t = 1; t <= MAX_TENTATIVAS; t++) {
        printf("Wi-Fi: tentando conectar (%d/%d) SSID=%s\n", t, MAX_TENTATIVAS, WIFI_SSID);

        int r = cyw43_arch_wifi_connect_timeout_ms(
            WIFI_SSID,
            WIFI_PASSWORD,
            CYW43_AUTH_WPA2_AES_PSK,
            30000
        );

        if (r == 0) {
            printf("Wi-Fi conectado!\n");
            g_wifi_ok = true;
            return;
        }

        printf("Falha ao conectar no Wi-Fi. Codigo: %d\n", r);
        sleep_ms(2000);
    }

    printf("Nao conectou apos varias tentativas.\n");
    while (true) tight_loop_contents();
}

// ==========================
// OLED: MENU limpo
// ==========================
static void oled_draw_menu(bool force)
{
    if (!oled_can_refresh(force)) return;

    oled_clear_header();

    // mostra modo/nível de forma simples
    char line_mode[22];
    if (mode_sel == MODE_LVL1) {
        snprintf(line_mode, sizeof(line_mode), "Modo: NIVEL 1");
    } else if (mode_sel == MODE_MEM_NORMAL) {
        snprintf(line_mode, sizeof(line_mode), "Modo: MEM %d", mem_len);
    } else {
        snprintf(line_mode, sizeof(line_mode), "Modo: RAP %d (5x)", mem_len);
    }
    ssd1306_draw_string(0, 12, line_mode);

    ssd1306_draw_string(0, 28, "A: iniciar");
    ssd1306_draw_string(0, 40, "A seg: mudar nivel");
    ssd1306_draw_string(0, 52, "B: parar | B seg: fim");

    ssd1306_show();
}

// ==========================
// TASK DO JOGO
// ==========================
static void vGameTask(void *pvParameters)
{
    (void) pvParameters;

    btn_hold_t bhA;
    btn_hold_t bhB;
    btn_hold_init(&bhA);
    btn_hold_init(&bhB);

    game_state_t st_prev = (game_state_t)999;

    for (;;) {
        watchdog_update();

#if LOCAL_REPORT_ENABLE
        local_report_process_serial();
#endif

        bool downA = (gpio_get(BTN_START) == 0);
        bool downB = (gpio_get(BTN_STOP)  == 0);

        int evA = btn_event_ms(&bhA, downA, HOLD_MS_A);  // 0 curto / 1 longo
        int evB = btn_event_ms(&bhB, downB, HOLD_MS_B);  // 0 curto / 1 longo

        atualizar_face_estavel();
        yellow_timer_update();

        // B curto: parar (volta menu)
        if (evB == 0) {
            beep_start();
            estado = ESTADO_PARADO;
            st = ST_MENU;
            repeat_same_seq = false;
            input_idx = 0;
            last_input_face = FACE_MOVENDO;
            fast_rounds_done = 0;
            g_round_start_ms = 0;
        }

        // B longo: encerra sessão (stop geral)
        if (evB == 1) {
#if LOCAL_REPORT_ENABLE
            local_report_event_stop(g_ok_total, g_err_total, mode_to_str(mode_sel));
            printf("[LOCAL] STOP GERAL enviado\n");
#endif
            metrics_reset_all();

            beep_err();
            oled_msg("SESSAO ENCERRADA", "Voltou ao MENU", 900);

            estado = ESTADO_PARADO;
            st = ST_MENU;
            repeat_same_seq = false;
            input_idx = 0;
            last_input_face = FACE_MOVENDO;
            fast_rounds_done = 0;
        }

        // força refresh ao trocar de estado
        bool force_oled = (st != st_prev);
        st_prev = st;

        // MENU
        if (st == ST_MENU) {
            estado = ESTADO_PARADO;

            strcpy(texto_modo, "MENU");
            strcpy(texto_info, "-");
            strcpy(texto_alvo, "-");
            face_to_str(face_base_estavel, texto_face);

            if (face_base_estavel == FACE_TOPO) led_on(FACE_TOPO);
            else all_leds_off();

            oled_draw_menu(force_oled);

            // A longo: muda modo/nível
            if (evA == 1) {
                beep_ok();
                if (mode_sel == MODE_LVL1) {
                    mode_sel = MODE_MEM_NORMAL;
                    mem_len = MEM_LEN_MIN;
                } else if (mode_sel == MODE_MEM_NORMAL) {
                    mem_len++;
                    if (mem_len > MEM_LEN_MAX) { mode_sel = MODE_MEM_RAPIDO; mem_len = MEM_LEN_MIN; }
                } else {
                    mem_len++;
                    if (mem_len > MEM_LEN_MAX) { mode_sel = MODE_LVL1; mem_len = MEM_LEN_MIN; }
                }
            }

            // A curto: start
            if (evA == 0) {
#if LOCAL_REPORT_ENABLE
                local_report_new_session();
                local_report_event_start(mode_to_str(mode_sel));
                printf("[LOCAL] start solicitado (%s)\n", mode_to_str(mode_sel));
#endif
                beep_start();
                estado = ESTADO_RODANDO;
                repeat_same_seq = false;
                input_idx = 0;
                last_input_face = FACE_MOVENDO;
                fast_rounds_done = 0;
                g_round_start_ms = 0;
                go_wait_yellow();
            }

            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }

        // WAIT YELLOW
        if (st == ST_WAIT_YELLOW) {
            strcpy(texto_modo, "PRONTO");
            strcpy(texto_alvo, "-");
            face_to_str(face_base_estavel, texto_face);

            if (face_base_estavel == FACE_TOPO) led_on(FACE_TOPO);
            else all_leds_off();

            if (oled_can_refresh(force_oled)) {
                oled_clear_header();
                ssd1306_draw_string(0, 12, "PRONTO");
                ssd1306_draw_string(0, 28, "Coloque TOPO amarelo");
                ssd1306_draw_string(0, 44, "e mantenha estavel");
                ssd1306_show();
            }

            if (yellow_ready()) {
                if (mode_sel == MODE_LVL1) {
                    lvl1_new_target();
                    metrics_round_start();
                    st = ST_L1_ACTIVE;
                } else {
                    mem_generate_sequence();
                    st = ST_MEM_SHOW;
                }
            }

            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }

        // NIVEL 1
        if (st == ST_L1_ACTIVE) {
            strcpy(texto_modo, "NIVEL 1");
            face_to_str(face_base_estavel, texto_face);
            face_to_str(alvo_l1, texto_alvo);
            snprintf(texto_info, sizeof(texto_info), "OK:%u ER:%u", (unsigned)g_ok_total, (unsigned)g_err_total);

            static TickType_t t0 = 0;
            if (t0 == 0) t0 = xTaskGetTickCount();
            TickType_t dt = xTaskGetTickCount() - t0;

            if (((dt / pdMS_TO_TICKS(BLINK_MS)) % 2) == 0) led_on(alvo_l1);
            else all_leds_off();

            if (oled_can_refresh(force_oled)) {
                oled_clear_header();
                ssd1306_draw_string(0, 12, "NIVEL 1");
                {
                    char buf[22];
                    snprintf(buf, sizeof(buf), "Vire p/ %s", texto_alvo);
                    ssd1306_draw_string(0, 28, buf);
                }
                {
                    char buf[22];
                    snprintf(buf, sizeof(buf), "OK:%u ER:%u", (unsigned)g_ok_total, (unsigned)g_err_total);
                    ssd1306_draw_string(0, 44, buf);
                }
                ssd1306_show();
            }

            if (face_base_estavel != FACE_MOVENDO && face_base_estavel != FACE_TOPO) {
                t0 = 0;
                if (face_base_estavel == alvo_l1) {
                    metrics_round_finish_ok();
#if LOCAL_REPORT_ENABLE
                    local_report_event_ok(g_last_round_ms, metrics_avg_ms(), g_ok_total, g_err_total, "NIVEL 1");
#endif
                    feedback_ok_go_yellow("Volte ao AMARELO");
                } else {
                    metrics_round_finish_err();
#if LOCAL_REPORT_ENABLE
                    local_report_event_err(g_last_round_ms, g_ok_total, g_err_total, "NIVEL 1");
#endif
                    repeat_same_seq = false;
                    feedback_err_repeat_go_yellow("Volte ao AMARELO");
                }
            }

            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }

        // MEMORIA: SHOW
        if (st == ST_MEM_SHOW) {
            bool rapido = (mode_sel == MODE_MEM_RAPIDO);

            strcpy(texto_modo, rapido ? "RAPIDO" : "MEMORIA");
            strcpy(texto_alvo, "-");
            face_to_str(face_base_estavel, texto_face);

            mem_show_sequence(rapido);
            oled_your_turn(rapido ? "MEMORIA RAPIDA" : "MEMORIA");

            input_idx = 0;
            last_input_face = FACE_MOVENDO;

            metrics_round_start();
            st = ST_MEM_INPUT;

            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }

        // MEMORIA: INPUT
        if (st == ST_MEM_INPUT) {
            bool rapido = (mode_sel == MODE_MEM_RAPIDO);

            strcpy(texto_modo, rapido ? "RAPIDO" : "MEMORIA");
            face_to_str(face_base_estavel, texto_face);
            face_to_str(seq[input_idx], texto_alvo);

            if (face_base_estavel == FACE_TOPO) led_on(FACE_TOPO);
            else all_leds_off();

            if (oled_can_refresh(force_oled)) {
                oled_clear_header();
                ssd1306_draw_string(0, 12, rapido ? "MEMORIA RAPIDA" : "MEMORIA");
                {
                    char buf[22];
                    snprintf(buf, sizeof(buf), "Passo %d/%d", input_idx + 1, mem_len);
                    ssd1306_draw_string(0, 28, buf);
                }
                {
                    char buf[22];
                    snprintf(buf, sizeof(buf), "OK:%u ER:%u", (unsigned)g_ok_total, (unsigned)g_err_total);
                    ssd1306_draw_string(0, 44, buf);
                }
                ssd1306_show();
            }

            if (face_base_estavel != FACE_MOVENDO && face_base_estavel != FACE_TOPO) {
                if (face_base_estavel != last_input_face) {
                    last_input_face = face_base_estavel;

                    if (face_base_estavel == seq[input_idx]) {
                        input_idx++;

                        if (input_idx >= mem_len) {
                            metrics_round_finish_ok();
#if LOCAL_REPORT_ENABLE
                            local_report_event_ok(g_last_round_ms, metrics_avg_ms(), g_ok_total, g_err_total, mode_to_str(mode_sel));
#endif

                            if (rapido) {
                                fast_rounds_done++;
                                if (fast_rounds_done >= FAST_ROUNDS_TOTAL) {
                                    if (YELLOW_FEEDBACK_ON) led_on(FACE_TOPO);
                                    beep_ok();
                                    oled_msg("TOP!", "Fim 5 rodadas", 700);
                                    st = ST_MENU;
                                    repeat_same_seq = false;
                                    input_idx = 0;
                                    last_input_face = FACE_MOVENDO;
                                    fast_rounds_done = 0;
                                } else {
                                    feedback_ok_go_yellow("Proxima rodada!");
                                }
                            } else {
                                feedback_ok_go_yellow("Volte ao AMARELO");
                            }
                        }
                    } else {
                        metrics_round_finish_err();
#if LOCAL_REPORT_ENABLE
                        local_report_event_err(g_last_round_ms, g_ok_total, g_err_total, mode_to_str(mode_sel));
#endif
                        repeat_same_seq = true;
                        feedback_err_repeat_go_yellow("Repete a MESMA");
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }

        st = ST_MENU;
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

// ==========================
// TASK DO MICROFONE
// ==========================
static void vMicTask(void *pvParameters)
{
    (void) pvParameters;
    mic_init();
    for (;;) {
        watchdog_update();
        mic_process();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

#if USE_MQTT
// ==========================
// TASK DO MQTT
// ==========================
static void vMQTTTask(void *pvParameters)
{
    (void) pvParameters;

    while (!g_wifi_ok) {
        watchdog_update();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    mqtt_start_application("v1/devices/me/telemetry", "pico_cubo", cubo_data_to_json_callback);

    for (;;) {
        watchdog_update();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

// ==========================
// TASK DE SAÚDE (debug leve)
// ==========================
static void vHealthTask(void *pvParameters)
{
    (void) pvParameters;

    for (;;) {
        watchdog_update();

        LOG_5S("[HEALTH] wifi_ok=%d | free_heap=%u bytes\n",
               (int)g_wifi_ok,
               (unsigned)xPortGetFreeHeapSize());

        if (g_game_task)  LOG_5S("[STACK] Game=%u\n", (unsigned)uxTaskGetStackHighWaterMark(g_game_task));
        if (g_mic_task)   LOG_5S("[STACK] Mic =%u\n", (unsigned)uxTaskGetStackHighWaterMark(g_mic_task));
#if USE_MQTT
        if (g_mqtt_task)  LOG_5S("[STACK] MQTT=%u\n", (unsigned)uxTaskGetStackHighWaterMark(g_mqtt_task));
#endif
#if LOCAL_REPORT_ENABLE
        TaskHandle_t lr = local_report_get_task_handle();
        if (lr) LOG_5S("[STACK] LocalUDP=%u\n", (unsigned)uxTaskGetStackHighWaterMark(lr));
#endif

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ==========================
// MAIN
// ==========================
int main(void)
{
    hw_init();

    watchdog_enable(8000, 1);

    wifi_connect_with_retry();

#if LOCAL_REPORT_ENABLE
    local_report_init();
    printf("[LOCAL] init feito\n");
#endif

    xTaskCreate(vGameTask,   "GameTask", 4096, NULL, 2, &g_game_task);
    xTaskCreate(vMicTask,    "MicTask",  4096, NULL, 1, &g_mic_task);
#if USE_MQTT
    xTaskCreate(vMQTTTask,   "MQTTTask", 4096, NULL, 3, &g_mqtt_task);
#endif
    xTaskCreate(vHealthTask, "Health",   2048, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true) tight_loop_contents();
}

void vApplicationMallocFailedHook(void) {
    printf("FATAL: malloc failed!\n");
    taskDISABLE_INTERRUPTS();
    while (1) { tight_loop_contents(); }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    printf("FATAL: stack overflow in %s\n", pcTaskName);
    taskDISABLE_INTERRUPTS();
    while (1) { tight_loop_contents(); }
}
