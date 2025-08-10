#ifndef SX127X_H
#define SX127X_H

#include <stdbool.h>
#include <stdint.h>

// Inicializa SPI, GPIOs e configura o módulo LoRa
bool sx127x_init(void);

// Envia uma mensagem via LoRa
bool sx127x_send_message(const char *msg);

// Recebe uma mensagem via LoRa (modo contínuo)
// Retorna true se uma mensagem foi recebida
bool sx127x_receive_message(char *buf, uint8_t max_len);

#endif
