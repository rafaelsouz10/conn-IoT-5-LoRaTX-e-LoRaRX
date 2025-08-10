// task_LoRa_RX.h — Receptor LoRa (FreeRTOS) para BitDogLab + SX1276
#ifndef TASK_LORA_RX_H
#define TASK_LORA_RX_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "sx127x.h"

// Variáveis globais publicadas para outras tasks (display, etc.)
volatile float temp_aht = 0.0f;
volatile float umid_aht = 0.0f;
volatile float pressao_bmp = 0.0f;

// Buffer de recepção
#define RX_BUFFER_SIZE 96

void vTaskLoRaRX(void *pvParameters) {
    (void)pvParameters;

    printf("[LoRaRX] Iniciando receptor...\n");
    if (!sx127x_init()) {
        printf("[LoRaRX] ERRO: SX1276 não detectado.\n");
        vTaskDelete(NULL);
    }

    printf("[LoRaRX] Pronto. Aguardando mensagens...\n");
    char buffer[RX_BUFFER_SIZE];

    for (;;) {
        if (sx127x_receive_message(buffer, sizeof(buffer))) {
            printf("[LoRaRX] Recebido: %s\n", buffer);

            // Espera formato CSV: TS,temp,umid,press,seq
            char *token = strtok(buffer, ",");
            if (token && strcmp(token, "TS") == 0) {
                token = strtok(NULL, ",");
                if (token) temp_aht = atof(token);

                token = strtok(NULL, ",");
                if (token) umid_aht = atof(token);

                token = strtok(NULL, ",");
                if (token) pressao_bmp = atof(token);

                // token = strtok(NULL, ","); // seq (opcional)
            } else {
                printf("[LoRaRX] Formato inválido.\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // pequena folga
    }
}

#endif // TASK_LORA_RX_H
