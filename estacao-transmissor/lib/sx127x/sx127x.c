#include "sx127x.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// === Mapeamento de pinos com base na placa LORA/SD conectada na BitDogLab ===
#define PIN_CS   17
#define PIN_RST  20
#define PIN_DIO0 8

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_MOSI 19
#define PIN_SCK  18

// === Registradores do SX1276 ===
#define REG_FIFO           0x00
#define REG_OP_MODE        0x01
#define REG_FRF_MSB        0x06
#define REG_FRF_MID        0x07
#define REG_FRF_LSB        0x08
#define REG_PA_CONFIG      0x09
#define REG_FIFO_ADDR_PTR  0x0D
#define REG_FIFO_TX_BASE   0x0E
#define REG_FIFO_RX_BASE   0x0F
#define REG_IRQ_FLAGS      0x12
#define REG_RX_NB_BYTES    0x13
#define REG_PKT_RSSI       0x1A
#define REG_MODEM_CONFIG1  0x1D
#define REG_MODEM_CONFIG2  0x1E
#define REG_VERSION        0x42
#define REG_PAYLOAD_LEN    0x22

// === Modos de operação ===
#define MODE_LONG_RANGE_MODE  0x80
#define MODE_TX               0x83
#define MODE_RX_CONTINUOUS    0x85
#define PA_BOOST              0x80

// === Reset do LoRa ===
static void sx127x_reset() {
    gpio_put(PIN_RST, 0);
    sleep_ms(100);
    gpio_put(PIN_RST, 1);
    sleep_ms(100);
}

// === Leitura de registrador via SPI ===
static uint8_t sx127x_read_reg(uint8_t addr) {
    uint8_t tx[] = { addr & 0x7F, 0x00 };
    uint8_t rx[2];
    gpio_put(PIN_CS, 0);
    spi_write_read_blocking(SPI_PORT, tx, rx, 2);
    gpio_put(PIN_CS, 1);
    return rx[1];
}

// === Escrita de registrador via SPI ===
static void sx127x_write_reg(uint8_t addr, uint8_t value) {
    uint8_t tx[] = { addr | 0x80, value };
    gpio_put(PIN_CS, 0);
    spi_write_blocking(SPI_PORT, tx, 2);
    gpio_put(PIN_CS, 1);
}

// === Inicialização do módulo SX1276 ===
bool sx127x_init() {
    // Inicializa SPI e pinos
    spi_init(SPI_PORT, 1 * 1000 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);

    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 1);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_init(PIN_DIO0); gpio_set_dir(PIN_DIO0, GPIO_IN);

    // Reset do módulo
    sx127x_reset();

    // Verifica versão
    uint8_t version = sx127x_read_reg(REG_VERSION);
    if (version != 0x12) return false;

    // Configura como LoRa, modo standby
    sx127x_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE);

    // Frequência = 915 MHz
    sx127x_write_reg(REG_FRF_MSB, 0xE4);
    sx127x_write_reg(REG_FRF_MID, 0xC0);
    sx127x_write_reg(REG_FRF_LSB, 0x00);

    // Potência de transmissão
    sx127x_write_reg(REG_PA_CONFIG, PA_BOOST | 0x0F);  // 20 dBm

    // Configurações de modem (spreading factor, error correction, etc.)
    sx127x_write_reg(REG_MODEM_CONFIG1, 0x72);
    sx127x_write_reg(REG_MODEM_CONFIG2, 0x74);

    // Configura bases FIFO
    sx127x_write_reg(REG_FIFO_TX_BASE, 0x00);
    sx127x_write_reg(REG_FIFO_RX_BASE, 0x00);

    return true;
}

// === Envia uma mensagem via LoRa ===
bool sx127x_send_message(const char *msg) {
    int len = strlen(msg);
    if (len > 255) return false;

    sx127x_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE);
    sx127x_write_reg(REG_FIFO_ADDR_PTR, 0x00);

    for (int i = 0; i < len; i++) {
        sx127x_write_reg(REG_FIFO, msg[i]);
    }

    sx127x_write_reg(REG_PAYLOAD_LEN, len);
    sx127x_write_reg(REG_OP_MODE, MODE_TX);

    // Aguarda transmissão
    while ((sx127x_read_reg(REG_IRQ_FLAGS) & 0x08) == 0) {
        tight_loop_contents();
    }

    // Limpa flag de TX
    sx127x_write_reg(REG_IRQ_FLAGS, 0x08);
    return true;
}

// === Recebe uma mensagem via LoRa (modo contínuo) ===
bool sx127x_receive_message(char *buf, uint8_t max_len) {
    sx127x_write_reg(REG_OP_MODE, MODE_RX_CONTINUOUS);

    if ((sx127x_read_reg(REG_IRQ_FLAGS) & 0x40) == 0) return false;  // RxDone

    // Limpa flag
    sx127x_write_reg(REG_IRQ_FLAGS, 0x40);

    uint8_t len = sx127x_read_reg(REG_RX_NB_BYTES);
    sx127x_write_reg(REG_FIFO_ADDR_PTR, sx127x_read_reg(0x10)); // FifoRxCurrentAddr

    for (int i = 0; i < len && i < max_len - 1; i++) {
        buf[i] = sx127x_read_reg(REG_FIFO);
    }
    buf[len] = '\0';
    return true;
}
