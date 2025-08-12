#include "sx127x.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// === Mapeamento de pinos com base na placa LORA/SD conectada na BitDogLab ===
#define PIN_CS   17      // Chip Select - sele��o do dispositivo SPI
#define PIN_RST  20      // Reset - reinicia o m�dulo LoRa
#define PIN_DIO0 8       // Digital I/O 0 - usado para interrup��es (TX/RX done)

#define SPI_PORT spi0    // Porta SPI utilizada (spi0)
#define PIN_MISO 16      // Master In Slave Out - dados do m�dulo para o microcontrolador
#define PIN_MOSI 19      // Master Out Slave In - dados do microcontrolador para o m�dulo
#define PIN_SCK  18      // Serial Clock - sinal de clock para sincroniza��o SPI

// === Registradores do SX1276 - Mapeamento dos endere�os de mem�ria ===
#define REG_FIFO           0x00  // Buffer FIFO para dados TX/RX
#define REG_OP_MODE        0x01  // Modo de opera��o (LoRa, FSK, Sleep, TX, RX, etc.)
#define REG_FRF_MSB        0x06  // Frequ�ncia da portadora - byte mais significativo
#define REG_FRF_MID        0x07  // Frequ�ncia da portadora - byte do meio
#define REG_FRF_LSB        0x08  // Frequ�ncia da portadora - byte menos significativo
#define REG_PA_CONFIG      0x09  // Configura��o do amplificador de pot�ncia
#define REG_FIFO_ADDR_PTR  0x0D  // Ponteiro de endere�o FIFO
#define REG_FIFO_TX_BASE   0x0E  // Endere�o base FIFO para transmiss�o
#define REG_FIFO_RX_BASE   0x0F  // Endere�o base FIFO para recep��o
#define REG_IRQ_FLAGS      0x12  // Flags de interrup��o (TX done, RX done, etc.)
#define REG_RX_NB_BYTES    0x13  // N�mero de bytes recebidos
#define REG_PKT_RSSI       0x1A  // Intensidade do sinal recebido (RSSI)
#define REG_MODEM_CONFIG1  0x1D  // Configura��o do modem: Bandwidth, Coding Rate, Header
#define REG_MODEM_CONFIG2  0x1E  // Configura��o do modem: Spreading Factor, CRC
#define REG_VERSION        0x42  // Vers�o do chip (0x12 para SX1276)
#define REG_PAYLOAD_LEN    0x22  // Comprimento do payload
#define REG_PREAMBLE_MSB   0x20  // Comprimento do pre�mbulo - byte mais significativo
#define REG_PREAMBLE_LSB   0x21  // Comprimento do pre�mbulo - byte menos significativo
#define REG_MODEM_CONFIG3  0x26  // Configura��o adicional: Low Data Rate Optimizer, AGC

// === Modos de opera��o - Valores para o registrador REG_OP_MODE ===
#define MODE_LONG_RANGE_MODE  0x80  // Habilita o modo LoRa (bit 7 = 1)
#define MODE_TX               0x83  // Modo de transmiss�o: LoRa + TX
#define MODE_RX_CONTINUOUS    0x85  // Modo de recep��o cont�nua: LoRa + RX
#define PA_BOOST              0x80  // Habilita o amplificador PA_BOOST para alta pot�ncia

// === Reset do LoRa - Reinicializa��o por hardware ===
static void sx127x_reset() {
    gpio_put(PIN_RST, 0);    // Coloca o pino de reset em n�vel baixo
    sleep_ms(100);           // Aguarda 100ms para garantir o reset
    gpio_put(PIN_RST, 1);    // Libera o reset (n�vel alto)
    sleep_ms(100);           // Aguarda estabiliza��o ap�s o reset
}

// === Leitura de registrador via SPI ===
static uint8_t sx127x_read_reg(uint8_t addr) {
    uint8_t tx[] = { addr & 0x7F, 0x00 };  // Bit 7 = 0 para leitura, endere�o + dummy byte
    uint8_t rx[2];                         // Buffer para receber dados
    gpio_put(PIN_CS, 0);                   // Seleciona o dispositivo (CS baixo)
    spi_write_read_blocking(SPI_PORT, tx, rx, 2);  // Transfer�ncia SPI bidirecional
    gpio_put(PIN_CS, 1);                   // Libera a sele��o (CS alto)
    return rx[1];                          // Retorna o dado lido (segundo byte)
}

// === Escrita de registrador via SPI ===
static void sx127x_write_reg(uint8_t addr, uint8_t value) {
    uint8_t tx[] = { addr | 0x80, value }; // Bit 7 = 1 para escrita, endere�o + valor
    gpio_put(PIN_CS, 0);                   // Seleciona o dispositivo (CS baixo)
    spi_write_blocking(SPI_PORT, tx, 2);   // Transfer�ncia SPI (apenas escrita)
    gpio_put(PIN_CS, 1);                   // Libera a sele��o (CS alto)
}

// === Inicializa��o do m�dulo SX1276 ===
bool sx127x_init() {
    // ========== INICIALIZA��O DO HARDWARE ==========
    // Configura SPI com velocidade de 1 MHz
    spi_init(SPI_PORT, 1 * 1000 * 1000);
    
    // Define as fun��es dos pinos GPIO para SPI
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);  // Configura MISO como fun��o SPI
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);  // Configura MOSI como fun��o SPI
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);   // Configura SCK como fun��o SPI

    // Configura pinos de controle como GPIO de sa�da/entrada
    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 1);    // CS como sa�da, inicialmente alto
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);                       // RST como sa�da
    gpio_init(PIN_DIO0); gpio_set_dir(PIN_DIO0, GPIO_IN);                      // DIO0 como entrada (interrup��es)

    // Reset por hardware do m�dulo
    sx127x_reset();

    // ========== VERIFICA��O DO CHIP ==========
    // Verifica se o chip est� respondendo corretamente
    uint8_t version = sx127x_read_reg(REG_VERSION);
    if (version != 0x12) return false;  // SX1276 deve retornar 0x12

    // ========== CONFIGURA��O B�SICA ==========
    // Habilita o modo LoRa e coloca em standby
    sx127x_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE);

    // ========== CONFIGURA��O DE FREQU�NCIA ==========
    // Frequ�ncia = 915 MHz (915.000.000 Hz)
    // C�lculo: Frf = (Freq � 2^19) / 32MHz = 915000000 � 524288 / 32000000 = 14991360 = 0xE4C000
    sx127x_write_reg(REG_FRF_MSB, 0xE4);  // Byte mais significativo da frequ�ncia
    sx127x_write_reg(REG_FRF_MID, 0xC0);  // Byte do meio da frequ�ncia
    sx127x_write_reg(REG_FRF_LSB, 0x00);  // Byte menos significativo da frequ�ncia

    // ========== CONFIGURA��O DE POT�NCIA DE TRANSMISS�O ==========
    // Pot�ncia de transmiss�o = 17 dBm
    // PA_BOOST (0x80) + OutputPower (0x0F) = 0x8F
    // F�rmula: Pout = 17 - (15 - OutputPower) = 17 - (15 - 15) = 17 dBm
    sx127x_write_reg(REG_PA_CONFIG, PA_BOOST | 0x0F);

    // ========== CONFIGURA��O DO MODEM - REGISTRADOR 1 ==========
    // REG_MODEM_CONFIG1 = 0x73 (01110011)
    // Bits 7-5: Bandwidth = 0111 (125 kHz)
    // Bits 4-2: Coding Rate = 001 (4/5 - taxa de corre��o de erro)
    // Bit 1: ImplicitHeaderModeOn = 1 (Header impl�cito = DISABLED)
    // Bit 0: Reserved = 1
    sx127x_write_reg(REG_MODEM_CONFIG1, 0x73);

    // ========== CONFIGURA��O DO MODEM - REGISTRADOR 2 ==========
    // REG_MODEM_CONFIG2 = 0x70 (01110000)
    // Bits 7-4: Spreading Factor = 0111 (SF7)
    // Bit 3: TxContinuousMode = 0 (modo normal)
    // Bit 2: RxPayloadCrcOn = 0 (CRC desabilitado)
    // Bits 1-0: SymbTimeout = 00 (timeout padr�o)
    sx127x_write_reg(REG_MODEM_CONFIG2, 0x70);

    // ========== CONFIGURA��O DO PRE�MBULO ==========
    // Pre�mbulo = 8 s�mbolos
    // O pre�mbulo � usado para sincroniza��o entre transmissor e receptor
    sx127x_write_reg(REG_PREAMBLE_MSB, 0x00);  // MSB = 0 (total = 8)
    sx127x_write_reg(REG_PREAMBLE_LSB, 0x08);  // LSB = 8 s�mbolos

    // ========== CONFIGURA��O DO MODEM - REGISTRADOR 3 ==========
    // REG_MODEM_CONFIG3 = 0x04 (00000100)
    // Bit 3: LowDataRateOptimize = 0 (otimiza��o para baixa taxa desabilitada)
    // Bit 2: AgcAutoOn = 1 (controle autom�tico de ganho habilitado)
    // Outros bits: reservados = 0
    sx127x_write_reg(REG_MODEM_CONFIG3, 0x04);

    // ========== CONFIGURA��O DO PAYLOAD ==========
    // Define o comprimento fixo do payload = 10 bytes
    // No modo de header impl�cito, o comprimento deve ser definido previamente
    sx127x_write_reg(REG_PAYLOAD_LEN, 32);

    // ========== CONFIGURA��O DO BUFFER FIFO ==========
    // Define os endere�os base para transmiss�o e recep��o no buffer FIFO
    sx127x_write_reg(REG_FIFO_TX_BASE, 0x00);  // Base TX no in�cio do FIFO
    sx127x_write_reg(REG_FIFO_RX_BASE, 0x00);  // Base RX no in�cio do FIFO

    return true;  // Inicializa��o bem-sucedida
}

// === Envia uma mensagem via LoRa ===
bool sx127x_send_message(const char *msg) {
    int len = strlen(msg);
    if (len > 255) return false;  // Verifica se a mensagem n�o excede o limite

    // ========== PREPARA��O PARA TRANSMISS�O ==========
    // Coloca o m�dulo em modo standby (LoRa habilitado)
    sx127x_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE);
    
    // Aponta o ponteiro FIFO para o in�cio do buffer de transmiss�o
    sx127x_write_reg(REG_FIFO_ADDR_PTR, 0x00);

    // ========== CARREGAMENTO DOS DADOS NO FIFO ==========
    // Escreve cada byte da mensagem no buffer FIFO
    for (int i = 0; i < len; i++) {
        sx127x_write_reg(REG_FIFO, msg[i]);
    }

    // ========== CONFIGURA��O DO COMPRIMENTO ==========
    // Define o comprimento da mensagem a ser transmitida
    // NOTA: Isso sobrescreve o valor fixo definido na inicializa��o
    sx127x_write_reg(REG_PAYLOAD_LEN, len);

    // ========== IN�CIO DA TRANSMISS�O ==========
    // Coloca o m�dulo em modo de transmiss�o
    sx127x_write_reg(REG_OP_MODE, MODE_TX);

    // ========== AGUARDA CONCLUS�O DA TRANSMISS�O ==========
    // Monitora a flag TxDone (bit 3) no registrador de interrup��es
    while ((sx127x_read_reg(REG_IRQ_FLAGS) & 0x08) == 0) {
        tight_loop_contents();  // Loop otimizado para economizar energia
    }

    // ========== LIMPEZA DA FLAG DE INTERRUP��O ==========
    // Limpa a flag TxDone para futuras transmiss�es
    sx127x_write_reg(REG_IRQ_FLAGS, 0x08);
    
    return true;  // Transmiss�o bem-sucedida
}

// === Recebe uma mensagem via LoRa (modo cont�nuo) ===
bool sx127x_receive_message(char *buf, uint8_t max_len) {
    // ========== CONFIGURA��O DO MODO DE RECEP��O ==========
    // Coloca o m�dulo em modo de recep��o cont�nua
    sx127x_write_reg(REG_OP_MODE, MODE_RX_CONTINUOUS);

    // ========== VERIFICA��O DE MENSAGEM RECEBIDA ==========
    // Verifica se a flag RxDone (bit 6) est� ativa
    if ((sx127x_read_reg(REG_IRQ_FLAGS) & 0x40) == 0) return false;

    // ========== LIMPEZA DA FLAG DE INTERRUP��O ==========
    // Limpa a flag RxDone
    sx127x_write_reg(REG_IRQ_FLAGS, 0x40);

    // ========== LEITURA DOS DADOS RECEBIDOS ==========
    // Obt�m o n�mero de bytes recebidos
    uint8_t len = sx127x_read_reg(REG_RX_NB_BYTES);
    
    // Aponta o ponteiro FIFO para o endere�o atual de recep��o
    sx127x_write_reg(REG_FIFO_ADDR_PTR, sx127x_read_reg(0x10)); // FifoRxCurrentAddr

    // L� os dados do FIFO para o buffer de destino
    for (int i = 0; i < len && i < max_len - 1; i++) {
        buf[i] = sx127x_read_reg(REG_FIFO);
    }
    buf[len] = '\0';  // Adiciona terminador de string

    return true;  // Recep��o bem-sucedida
}