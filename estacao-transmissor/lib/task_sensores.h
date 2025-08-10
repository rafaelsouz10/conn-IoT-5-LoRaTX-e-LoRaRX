#ifndef TASK_SENSORES_H
#define TASK_SENSORES_H

#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "hardware/i2c.h"
#include "aht20/aht20.h"
#include "bmp280/bmp280.h"
#include <math.h>

// --- Variáveis globais com os dados dos sensores ---
volatile float temp_aht = 0.0f;
volatile float umid_aht = 0.0f;
volatile float pressao_bmp = 0.0f;

// --- I2C e parâmetros ---
#define SDA_I2C0 0
#define SCL_I2C0 1
#define I2C_PORT i2c0
#define SEA_LEVEL_PRESSURE 101325.0

// --- Task de leitura dos sensores ---
void vTaskSensores(void *pvParameters) {
    // Inicialização I2C0 
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(SDA_I2C0, GPIO_FUNC_I2C);
    gpio_set_function(SCL_I2C0, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_I2C0);
    gpio_pull_up(SCL_I2C0);


    // Inicialização dos sensores
    bmp280_init(I2C_PORT);
    struct bmp280_calib_param calib;
    bmp280_get_calib_params(I2C_PORT, &calib);

    aht20_reset(I2C_PORT);
    sleep_ms(50);
    aht20_init(I2C_PORT);

    AHT20_Data dados_aht;
    int32_t raw_temp, raw_press;

    while (1) {
        // BMP280
        bmp280_read_raw(I2C_PORT, &raw_temp, &raw_press);
        pressao_bmp = bmp280_convert_pressure(raw_press, raw_temp, &calib) / 1000.0;

        // AHT20
        if (aht20_read(I2C_PORT, &dados_aht)) {
            temp_aht = dados_aht.temperature;
            umid_aht = dados_aht.humidity;
        } else {
            printf("Falha na leitura do AHT20\n");
        }

        printf("AHT20: %.1f °C, %.1f %% | BMP280: %.2f kPa\n",
            temp_aht, umid_aht, pressao_bmp);

        vTaskDelay(pdMS_TO_TICKS(1000)); // espera 1s
    }
}

#endif
