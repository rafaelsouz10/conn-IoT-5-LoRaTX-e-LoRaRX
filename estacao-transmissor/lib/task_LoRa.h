// task_LoRa.h — usa o driver existente em sx127x/
#ifndef TASK_LORA_H
#define TASK_LORA_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "sx127x.h"   // já cuida de pinos e setup no seu projeto

// Variáveis globais publicadas pela task_sensores.h
extern volatile float temp_aht;
extern volatile float umid_aht;
extern volatile float pressao_bmp;

// Período de envio (ms)
#ifndef LORA_TX_PERIOD_MS
#define LORA_TX_PERIOD_MS 3000
#endif

// Tentativas de reenvio em caso de falha
#define LORA_TX_RETRY 1

static inline int is_valid_float(float v) {
    return (v == v) && (v != (float)1e300) && (v != -(float)1e300); // checagem simples p/ NaN/Inf
}

void vTaskLoRaTX(void *pvParameters) {
    (void)pvParameters;

    printf("[LoRaTX] Iniciando transmissor...\n");
    if (!sx127x_init()) {                 // tua lib já faz toda a config de rádio/pinos
        printf("[LoRaTX] ERRO: SX1276 não detectado.\n");
        vTaskDelete(NULL);
    }
    printf("[LoRaTX] Pronto para transmitir.\n");

    uint32_t seq = 0;
    char payload[96];

    for (;;) {
        // snapshot das leituras (evita inconsistência durante o printf/snprintf)
        float ta = temp_aht;
        float ua = umid_aht;
        float pb = pressao_bmp; // kPa no teu código

        // (opcional) sanity check
        if (!is_valid_float(ta) || !is_valid_float(ua) || !is_valid_float(pb)) {
            printf("[LoRaTX] Leituras inválidas, pulando envio.\n");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // CSV compacto: TAG,TempAHT,UmidAHT,Press_kPa,SEQ
        // Ex.: TS,25.31,61.20,100.84,123
        int n = snprintf(payload, sizeof(payload),
                         "TS,%.2f,%.2f,%.2f,%lu",
                         ta, ua, pb, (unsigned long)seq++);

        if (n <= 0 || n >= (int)sizeof(payload)) {
            printf("[LoRaTX] ERRO: payload maior que o buffer (%d).\n", n);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        bool ok = sx127x_send_message(payload);
        if (!ok) {
            printf("[LoRaTX] Falha no envio, tentando novamente...\n");
            for (int i = 0; i < LORA_TX_RETRY && !ok; i++) {
                vTaskDelay(pdMS_TO_TICKS(300));   // pequeno backoff
                ok = sx127x_send_message(payload);
            }
        }

        if (ok) {
            printf("[LoRaTX] Enviado: %s\n", payload);
        } else {
            printf("[LoRaTX] ERRO: envio não confirmado após retries.\n");
        }

        vTaskDelay(pdMS_TO_TICKS(LORA_TX_PERIOD_MS));
    }
}

#endif // TASK_LORA_H
