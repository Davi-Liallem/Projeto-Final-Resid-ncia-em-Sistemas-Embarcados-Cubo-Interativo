#ifndef LOCAL_REPORT_H
#define LOCAL_REPORT_H

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

// =====================================================
// LOCAL REPORT - Sessões por usuário (UDP -> PC)
// =====================================================
//
// Regras (anti-mistura):
// 1) start abre sessão e envia JSON.
//    - Se user não estiver definido, envia user="" (vazio) para o /live mapear.
// 2) ok/err só são enviados se sessão estiver aberta.
// 3) stop fecha sessão e envia total_ms (tempo total da sessão).
//
// Opção A: definir USER pelo Serial:
//   Envie no serial:
//     USER Davi
//   ou
//     USER:Davi
//   ou só:
//     Davi
//
// Observação:
// - local_report_process_serial() deve ser chamado no loop/tarefa do jogo,
//   para capturar o nome digitado no Serial Monitor.
// =====================================================

void local_report_init(void);

// Compatibilidade com código antigo (no-op seguro)
void local_report_new_session(void);

// Define/atualiza o usuário (Serial ou ThingsBoard)
void local_report_set_user(const char *user);

// Retorna se já existe user definido (para não sobrescrever com TB)
bool local_report_has_user(void);

// (opcional) limpar user
void local_report_clear_user(void);

// Processa comandos do Serial (chamar sempre dentro do loop do jogo)
void local_report_process_serial(void);

// Eventos do jogo
void local_report_event_start(const char *modo);

void local_report_event_ok(uint32_t last_ms, uint32_t avg_ms,
                           uint32_t ok_total, uint32_t err_total,
                           const char *modo);

void local_report_event_err(uint32_t last_ms,
                            uint32_t ok_total, uint32_t err_total,
                            const char *modo);

void local_report_event_stop(uint32_t ok_total, uint32_t err_total,
                             const char *modo);

// Debug/stack no HealthTask (opcional)
TaskHandle_t local_report_get_task_handle(void);

// Alias opcional (caso seu código use esse nome)
TaskHandle_t local_report_task_handle(void);

#endif // LOCAL_REPORT_H
