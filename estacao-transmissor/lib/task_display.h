#ifndef TASK_DISPLAY_H
#define TASK_DISPLAY_H

#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "hardware/i2c.h"
#include "ssd1306/ssd1306.h"

// Variáveis globais dos sensores
extern volatile float temp_aht;
extern volatile float umid_aht;
extern volatile float pressao_bmp;

// I2C do display
#define I2C_PORT_DISP i2c1
#define SDA_DISP 14
#define SCL_DISP 15
#define DISPLAY_ADDR 0x3C

void vTaskDisplay(void *pvParameters) {
    // Inicializa o barramento I2C1 para o display
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_DISP);
    gpio_pull_up(SCL_DISP);

    // Inicializa o display SSD1306
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, DISPLAY_ADDR, I2C_PORT_DISP);
    ssd1306_config(&ssd);

    char str_tempAHT[8], str_umi[8], str_pressao[8];
    bool cor = true;

    while (1) {
        // Converte dados para string
        sprintf(str_tempAHT, "%.1fC", temp_aht);
        sprintf(str_umi, "%.1f%%", umid_aht);
        sprintf(str_pressao, "%.1fKPa", pressao_bmp);

        // Atualiza display com bordas e layout
        ssd1306_fill(&ssd, !cor);                        // Limpa com inversão
        ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);    // Moldura externa
        ssd1306_line(&ssd, 3, 25, 123, 25, cor);         // Linha horizontal 1
        ssd1306_line(&ssd, 3, 37, 123, 37, cor);         // Linha horizontal 2
        ssd1306_line(&ssd, 63, 37, 63, 60, cor);         // Linha vertical central

        // Título e legendas
        ssd1306_draw_string(&ssd, "EMBARCATECH", 18, 6);
        ssd1306_draw_string(&ssd, "AHT10  BMP280", 15, 16);

        // --- IP CENTRAL ENTRE AS LINHAS ---
        ssd1306_draw_string(&ssd, "TRANSMISSOR", 15, 28);

        // Dados BMP280 (esquerda)
        ssd1306_draw_string(&ssd, str_umi, 12, 43);
        ssd1306_draw_string(&ssd, str_tempAHT, 12, 53);

        // Dados AHT20 (direita)
        ssd1306_draw_string(&ssd, str_pressao, 66, 43);
        ssd1306_draw_string(&ssd, "ND", 66, 53);

        ssd1306_send_data(&ssd);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Atualiza a cada 2s
    }
}

#endif
