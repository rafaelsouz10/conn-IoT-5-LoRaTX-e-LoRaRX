#include "sx127x.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// === Mapeamento de pinos com base na placa LORA/SD conectada na BitDogLab ===
#define PIN_CS   17      // Chip Select - seleção do dispositivo SPI
#define PIN_RST  20      // Reset - reinicia o módulo LoRa
#define PIN_DIO0 8       // Digital I/O 0 - usado para interrupções (TX/RX done)

#define SPI_PORT spi0    // Porta SPI utilizada (spi0)
#define PIN_MISO 16      // Master In Slave Out - dados do módulo para o microcontrolador
#define PIN_MOSI 19      // Master Out Slave In - dados do microcontrolador para o módulo
#define PIN_SCK  18      // Serial Clock - sinal de clock para sincronização SPI

// === Registradores do SX1276 - Mapeamento dos endereços de memória ===
#define REG_FIFO           0x00  // Buffer FIFO para dados TX/RX
#define REG_OP_MODE        0x01  // Modo de operação (LoRa, FSK, Sleep, TX, RX, etc.)
#define REG_FRF_MSB        0x06  // Frequência da portadora - byte mais significativo
#define REG_FRF_MID        0x07  // Frequência da portadora - byte do meio
#define REG_FRF_LSB        0x08  // Frequência da portadora - byte menos significativo
#define REG_PA_CONFIG      0x09  // Configuração do amplificador de potência
#define REG_FIFO_ADDR_PTR  0x0D  // Ponteiro de endereço FIFO
#define REG_FIFO_TX_BASE   0x0E  // Endereço base FIFO para transmissão
#define REG_FIFO_RX_BASE   0x0F  // Endereço base FIFO para recepção
#define REG_IRQ_FLAGS      0x12  // Flags de interrupção (TX done, RX done, etc.)
#define REG_RX_NB_BYTES    0x13  // Número de bytes recebidos
#define REG_PKT_RSSI       0x1A  // Intensidade do sinal recebido (RSSI)
#define REG_MODEM_CONFIG1  0x1D  // Configuração do modem: Bandwidth, Coding Rate, Header
#define REG_MODEM_CONFIG2  0x1E  // Configuração do modem: Spreading Factor, CRC
#define REG_VERSION        0x42  // Versão do chip (0x12 para SX1276)
#define REG_PAYLOAD_LEN    0x22  // Comprimento do payload
#define REG_PREAMBLE_MSB   0x20  // Comprimento do preâmbulo - byte mais significativo
#define REG_PREAMBLE_LSB   0x21  // Comprimento do preâmbulo - byte menos significativo
#define REG_MODEM_CONFIG3  0x26  // Configuração adicional: Low Data Rate Optimizer, AGC

// === Modos de operação - Valores para o registrador REG_OP_MODE ===
#define MODE_LONG_RANGE_MODE  0x80  // Habilita o modo LoRa (bit 7 = 1)
#define MODE_TX               0x83  // Modo de transmissão: LoRa + TX
#define MODE_RX_CONTINUOUS    0x85  // Modo de recepção contínua: LoRa + RX
#define PA_BOOST              0x80  // Habilita o amplificador PA_BOOST para alta potência

// === Reset do LoRa - Reinicialização por hardware ===
static void sx127x_reset() {
    gpio_put(PIN_RST, 0);    // Coloca o pino de reset em nível baixo
    sleep_ms(100);           // Aguarda 100ms para garantir o reset
    gpio_put(PIN_RST, 1);    // Libera o reset (nível alto)
    sleep_ms(100);           // Aguarda estabilização após o reset
}

// === Leitura de registrador via SPI ===
static uint8_t sx127x_read_reg(uint8_t addr) {
    uint8_t tx[] = { addr & 0x7F, 0x00 };  // Bit 7 = 0 para leitura, endereço + dummy byte
    uint8_t rx[2];                         // Buffer para receber dados
    gpio_put(PIN_CS, 0);                   // Seleciona o dispositivo (CS baixo)
    spi_write_read_blocking(SPI_PORT, tx, rx, 2);  // Transferência SPI bidirecional
    gpio_put(PIN_CS, 1);                   // Libera a seleção (CS alto)
    return rx[1];                          // Retorna o dado lido (segundo byte)
}

// === Escrita de registrador via SPI ===
static void sx127x_write_reg(uint8_t addr, uint8_t value) {
    uint8_t tx[] = { addr | 0x80, value }; // Bit 7 = 1 para escrita, endereço + valor
    gpio_put(PIN_CS, 0);                   // Seleciona o dispositivo (CS baixo)
    spi_write_blocking(SPI_PORT, tx, 2);   // Transferência SPI (apenas escrita)
    gpio_put(PIN_CS, 1);                   // Libera a seleção (CS alto)
}

// === Inicialização do módulo SX1276 ===
bool sx127x_init() {
    // ========== INICIALIZAÇÃO DO HARDWARE ==========
    // Configura SPI com velocidade de 1 MHz
    spi_init(SPI_PORT, 1 * 1000 * 1000);
    
    // Define as funções dos pinos GPIO para SPI
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);  // Configura MISO como função SPI
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);  // Configura MOSI como função SPI
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);   // Configura SCK como função SPI

    // Configura pinos de controle como GPIO de saída/entrada
    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 1);    // CS como saída, inicialmente alto
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);                       // RST como saída
    gpio_init(PIN_DIO0); gpio_set_dir(PIN_DIO0, GPIO_IN);                      // DIO0 como entrada (interrupções)

    // Reset por hardware do módulo
    sx127x_reset();

    // ========== VERIFICAÇÃO DO CHIP ==========
    // Verifica se o chip está respondendo corretamente
    uint8_t version = sx127x_read_reg(REG_VERSION);
    if (version != 0x12) return false;  // SX1276 deve retornar 0x12

    // ========== CONFIGURAÇÃO BÁSICA ==========
    // Habilita o modo LoRa e coloca em standby
    sx127x_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE);

    // ========== CONFIGURAÇÃO DE FREQUÊNCIA ==========
    // Frequência = 915 MHz (915.000.000 Hz)
    // Cálculo: Frf = (Freq × 2^19) / 32MHz = 915000000 × 524288 / 32000000 = 14991360 = 0xE4C000
    sx127x_write_reg(REG_FRF_MSB, 0xE4);  // Byte mais significativo da frequência
    sx127x_write_reg(REG_FRF_MID, 0xC0);  // Byte do meio da frequência
    sx127x_write_reg(REG_FRF_LSB, 0x00);  // Byte menos significativo da frequência

    // ========== CONFIGURAÇÃO DE POTÊNCIA DE TRANSMISSÃO ==========
    // Potência de transmissão = 17 dBm
    // PA_BOOST (0x80) + OutputPower (0x0F) = 0x8F
    // Fórmula: Pout = 17 - (15 - OutputPower) = 17 - (15 - 15) = 17 dBm
    sx127x_write_reg(REG_PA_CONFIG, PA_BOOST | 0x0F);

    // ========== CONFIGURAÇÃO DO MODEM - REGISTRADOR 1 ==========
    // REG_MODEM_CONFIG1 = 0x73 (01110011)
    // Bits 7-5: Bandwidth = 0111 (125 kHz)
    // Bits 4-2: Coding Rate = 001 (4/5 - taxa de correção de erro)
    // Bit 1: ImplicitHeaderModeOn = 1 (Header implícito = DISABLED)
    // Bit 0: Reserved = 1
    sx127x_write_reg(REG_MODEM_CONFIG1, 0x73);

    // ========== CONFIGURAÇÃO DO MODEM - REGISTRADOR 2 ==========
    // REG_MODEM_CONFIG2 = 0x70 (01110000)
    // Bits 7-4: Spreading Factor = 0111 (SF7)
    // Bit 3: TxContinuousMode = 0 (modo normal)
    // Bit 2: RxPayloadCrcOn = 0 (CRC desabilitado)
    // Bits 1-0: SymbTimeout = 00 (timeout padrão)
    sx127x_write_reg(REG_MODEM_CONFIG2, 0x70);

    // ========== CONFIGURAÇÃO DO PREÂMBULO ==========
    // Preâmbulo = 8 símbolos
    // O preâmbulo é usado para sincronização entre transmissor e receptor
    sx127x_write_reg(REG_PREAMBLE_MSB, 0x00);  // MSB = 0 (total = 8)
    sx127x_write_reg(REG_PREAMBLE_LSB, 0x08);  // LSB = 8 símbolos

    // ========== CONFIGURAÇÃO DO MODEM - REGISTRADOR 3 ==========
    // REG_MODEM_CONFIG3 = 0x04 (00000100)
    // Bit 3: LowDataRateOptimize = 0 (otimização para baixa taxa desabilitada)
    // Bit 2: AgcAutoOn = 1 (controle automático de ganho habilitado)
    // Outros bits: reservados = 0
    sx127x_write_reg(REG_MODEM_CONFIG3, 0x04);

    // ========== CONFIGURAÇÃO DO PAYLOAD ==========
    // Define o comprimento fixo do payload = 10 bytes
    // No modo de header implícito, o comprimento deve ser definido previamente
    sx127x_write_reg(REG_PAYLOAD_LEN, 10);

    // ========== CONFIGURAÇÃO DO BUFFER FIFO ==========
    // Define os endereços base para transmissão e recepção no buffer FIFO
    sx127x_write_reg(REG_FIFO_TX_BASE, 0x00);  // Base TX no início do FIFO
    sx127x_write_reg(REG_FIFO_RX_BASE, 0x00);  // Base RX no início do FIFO

    return true;  // Inicialização bem-sucedida
}

// === Envia uma mensagem via LoRa ===
bool sx127x_send_message(const char *msg) {
    int len = strlen(msg);
    if (len > 255) return false;  // Verifica se a mensagem não excede o limite

    // ========== PREPARAÇÃO PARA TRANSMISSÃO ==========
    // Coloca o módulo em modo standby (LoRa habilitado)
    sx127x_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE);
    
    // Aponta o ponteiro FIFO para o início do buffer de transmissão
    sx127x_write_reg(REG_FIFO_ADDR_PTR, 0x00);

    // ========== CARREGAMENTO DOS DADOS NO FIFO ==========
    // Escreve cada byte da mensagem no buffer FIFO
    for (int i = 0; i < len; i++) {
        sx127x_write_reg(REG_FIFO, msg[i]);
    }

    // ========== CONFIGURAÇÃO DO COMPRIMENTO ==========
    // Define o comprimento da mensagem a ser transmitida
    // NOTA: Isso sobrescreve o valor fixo definido na inicialização
    sx127x_write_reg(REG_PAYLOAD_LEN, len);

    // ========== INÍCIO DA TRANSMISSÃO ==========
    // Coloca o módulo em modo de transmissão
    sx127x_write_reg(REG_OP_MODE, MODE_TX);

    // ========== AGUARDA CONCLUSÃO DA TRANSMISSÃO ==========
    // Monitora a flag TxDone (bit 3) no registrador de interrupções
    while ((sx127x_read_reg(REG_IRQ_FLAGS) & 0x08) == 0) {
        tight_loop_contents();  // Loop otimizado para economizar energia
    }

    // ========== LIMPEZA DA FLAG DE INTERRUPÇÃO ==========
    // Limpa a flag TxDone para futuras transmissões
    sx127x_write_reg(REG_IRQ_FLAGS, 0x08);
    
    return true;  // Transmissão bem-sucedida
}

// === Recebe uma mensagem via LoRa (modo contínuo) ===
bool sx127x_receive_message(char *buf, uint8_t max_len) {
    // ========== CONFIGURAÇÃO DO MODO DE RECEPÇÃO ==========
    // Coloca o módulo em modo de recepção contínua
    sx127x_write_reg(REG_OP_MODE, MODE_RX_CONTINUOUS);

    // ========== VERIFICAÇÃO DE MENSAGEM RECEBIDA ==========
    // Verifica se a flag RxDone (bit 6) está ativa
    if ((sx127x_read_reg(REG_IRQ_FLAGS) & 0x40) == 0) return false;

    // ========== LIMPEZA DA FLAG DE INTERRUPÇÃO ==========
    // Limpa a flag RxDone
    sx127x_write_reg(REG_IRQ_FLAGS, 0x40);

    // ========== LEITURA DOS DADOS RECEBIDOS ==========
    // Obtém o número de bytes recebidos
    uint8_t len = sx127x_read_reg(REG_RX_NB_BYTES);
    
    // Aponta o ponteiro FIFO para o endereço atual de recepção
    sx127x_write_reg(REG_FIFO_ADDR_PTR, sx127x_read_reg(0x10)); // FifoRxCurrentAddr

    // Lê os dados do FIFO para o buffer de destino
    for (int i = 0; i < len && i < max_len - 1; i++) {
        buf[i] = sx127x_read_reg(REG_FIFO);
    }
    buf[len] = '\0';  // Adiciona terminador de string

    return true;  // Recepção bem-sucedida
}