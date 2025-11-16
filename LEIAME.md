# Morse Transceiver — LEIAME

## 1. Visão Geral

Firmware modular para ESP8266 que implementa um transceptor CW (Morse) com entrada local (botão físico) e remota (Wi‑Fi/TCP). O sistema oferece feedback via **buzzer**, **OLED SSD1306**, além de um **LED blinker**, utilizando arquitetura totalmente **não‑bloqueante** (`millis()` + `yield()`).

---

## 2. Estrutura de Arquivos e Módulos

| Arquivo / Módulo    | Função Principal                    | Interações                      |
| ------------------- | ----------------------------------- | ------------------------------- |
| `main.cpp`          | Setup, loop, integração geral       | Todos os módulos                |
| `morse-key.*`       | Leitura do botão (ISR + debounce)   | Telegrapher, History            |
| `telegrapher.*`     | Lógica Morse, tradução, modos       | Display, History, Network-State |
| `morse-telecom.*`   | Protocolo TCP textual               | Network-Connect, Telegrapher    |
| `network-state.*`   | FSM (FREE/TX/RX)                    | Buzzer, Display                 |
| `network-connect.*` | Gerenciamento Wi‑Fi/TCP             | Telecom, Network-State          |
| `history.*`         | Buffers circulares TX/RX (30 chars) | Display lê, Telegrapher escreve |
| `display-adapter.*` | Desenho do layout OLED              | Telegrapher, Network-State      |
| `buzzer-driver.*`   | Driver não‑bloqueante do buzzer     | Telegrapher, Network-State      |
| `blinker.*`         | Frase → Morse → LED                 | Translator                      |
| `translator.*`      | Tabela ASCII ↔ Morse                | Telegrapher, Blinker            |
| `bitmap.h`          | Bitmap do splash                    | Display                         |

---

## 3. Padrão de Cabeçalho e Flags

Modelo padrão de cabeçalho:

```cpp
// File: nome.ext vX.X
// Description: descrição
// Last modification: descrição
// Modified: YYYY-MM-DD HH:MM
// Created: YYYY-MM-DD
```

Flags de log logo após os includes:

```cpp
// ====== LOG FLAGS ======
#define LOG_[MODULE]_INIT   1
#define LOG_[MODULE]_EVENT  1
#define LOG_[MODULE]_ERROR  0
```

---

## 4. Descrição dos Módulos

### 4.1 main.cpp

* Inicializa todos os módulos.
* `loop()` chama todos os `update()`.
* Coordena callbacks e fluxo geral.

### 4.2 morse-key

* Captura eventos do botão (D5).
* Usa ISR + debounce.
* Flags: `LOG_MORSE_KEY_ISR`, `LOG_MORSE_KEY_EVENT`.

### 4.3 telegrapher

* Classifica dot/dash.
* Traduz caracteres.
* Modos: DIDACTIC / MORSE.
* Atualiza Display, History e rede.
* Flags: `LOG_TELEGRAPHER_SYMBOL`, `LOG_TELEGRAPHER_MODE`.

### 4.4 morse-telecom

* Protocolo TCP em porta 5000.
* Mensagens: `alive`, `duration:<ms>`, `request_tx`, `busy`, `ok`, `mac:<mac>`.
* Flags: `LOG_TELECOM_RX`, `LOG_TELECOM_TX`.

### 4.5 network-state

* FSM de estados: FREE/TX/RX.
* Afeta buzzer e display.
* Flags: `LOG_NETSTATE_CHANGE`, `LOG_NETSTATE_TIMEOUT`.

### 4.6 network-connect

* Scan, conexão, heartbeat.
* Entrega socket ao Telecom.
* Flags: `LOG_NETCONNECT_SCAN`, `LOG_NETCONNECT_CONN`.

### 4.7 history

* Buffer TX/RX (30 chars).
* Display usa 29 visíveis divididos em 3 linhas.
* Flags: `LOG_HISTORY_PUSH`, `LOG_HISTORY_OVERFLOW`.

### 4.8 display-adapter

* Renderização do OLED.
* Histórico + símbolo + força do Wi‑Fi.
* Flags: `LOG_DISPLAY_UPDATE`, `LOG_DISPLAY_CACHE`.

### 4.9 buzzer-driver

* Padrões sonoros não‑bloqueantes.
* Flags: `LOG_BUZZER_INIT`, `LOG_BUZZER_PATTERN`.

### 4.10 blinker

* Pisca frase em Morse, de modo independente.
* Flags: `LOG_BLINKER_INIT`, `LOG_BLINKER_BUILD`, `LOG_BLINKER_RUN`, `LOG_BLINKER_PHASE`.

### 4.11 translator

* Mapeamento ASCII ↔ Morse.
* Flags: `LOG_TRANSLATOR_LOOKUP`.

### 4.12 bitmap.h

* Arte PROGMEM para tela de abertura (3s).

---

## 5. Boas Práticas

* Uso consistente de `millis()` + `yield()`.
* Logs com timestamps.
* `strncpy` + validação de limites.
* Desacoplamento via callbacks.
* Constantes sempre em CAPS.
* Splash fixo de 3 segundos.
* Histórico sempre com 29 chars visíveis.

---

## 6. Fluxo de Interações

### Entrada Local

Botão → Telegrapher → History → Display → Buzzer → Network-State → Telecom.

### Entrada Remota

TCP → Network-Connect → Telecom → Telegrapher → History → Display → Buzzer.

### Blinker

Rodando independente, piscando frase configurada.

### Display

Mostra histórico, símbolo temporário, estado e Wi‑Fi.

### Buzzer

Feedback local e remoto.

---

## 7. Testes & Troubleshooting

* Verificar splash de 3 segundos.
* Press → buzzer ON; Release → log de duração.
* Dot ≤150 ms; Dash >150 ms.
* Letra gerada após ~800 ms sem entrada.
* TCP deve manter troca entre dispositivos.
* Display deve exibir histórico e símbolo.

**Problemas comuns:**

* Tela branca → verificar SDA/SCL.
* Wi‑Fi travado em AP → checar SSID.
* Buzzer mudo → transistor.
* Debounce inconsistente → ajustar `DEBOUNCE_TIME`.

---

## 8. Recursos Avançados

* Configurações flexíveis de Wi‑Fi.
* Níveis de log configuráveis.
* Validação rígida de pacotes TCP.
* Ajuste fino de tempos Morse.
* Exportação do histórico pela rede.
* Modularidade rígida garantida.

---

## 9. Licença e Créditos

* Licença: MIT.
* Bibliotecas: Adafruit GFX, SSD1306, ESP8266 core.
* Autor: Allan.

---

## 10. Conclusão

LEIAME unificado no mesmo padrão de documentação oficial, padronizado e pronto para uso no repositório.
