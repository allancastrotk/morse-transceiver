# Morse Transceiver — Documentation Core

## 1. Visão Geral

Firmware modular para ESP8266 que implementa um transceptor CW (Morse), com entrada local (botão físico) e remota (Wi‑Fi/TCP). O sistema fornece feedback sonoro via **buzzer**, feedback visual via **OLED SSD1306**, além de um **LED blinker**. A arquitetura é **não-bloqueante**, baseada em `millis()` + `yield()`, garantindo responsividade.

---

## 2. Estrutura de Arquivos e Módulos

| Módulo / Arquivo    | Responsabilidade                                    | Interações                      |
| ------------------- | --------------------------------------------------- | ------------------------------- |
| `main.cpp`          | Ponto de entrada; integra módulos; define callbacks | Chama `init`/`update` de todos  |
| `morse-key.*`       | Captura eventos do botão físico (ISR + debounce)    | Telegrapher, History            |
| `telegrapher.*`     | Lógica Morse; classificação; tradução; modos        | History, Network-State, Display |
| `morse-telecom.*`   | Protocolo TCP textual                               | Network-Connect, Telegrapher    |
| `network-state.*`   | Máquina de estados TX/RX/FREE                       | Buzzer, Display                 |
| `network-connect.*` | Gerenciamento Wi‑Fi/TCP                             | Network-State, Telecom          |
| `history.*`         | Buffers circulares TX/RX (30 chars)                 | Display lê, Telegrapher escreve |
| `display-adapter.*` | UI OLED: splash, redraw, caching                    | Telegrapher, Network-State      |
| `buzzer-driver.*`   | Driver não-bloqueante de buzzer                     | Telegrapher, Network-State      |
| `blinker.*`         | LED blinker                                         | Translator                      |
| `translator.*`      | Tabela ASCII ↔ Morse                                | Telegrapher, Blinker            |
| `bitmap.h`          | Splash screen                                       | Display                         |

---

## 3. Padrão de Cabeçalho e Flags

Todos os arquivos devem seguir o padrão:

```cpp
// File: nome.ext vX.X
// Description: descrição
// Last modification: descrição
// Modified: YYYY-MM-DD HH:MM
// Created: YYYY-MM-DD
```

E imediatamente após os includes:

```cpp
// ====== LOG FLAGS ======
#define LOG_[MODULE]_INIT   1
#define LOG_[MODULE]_EVENT  1
#define LOG_[MODULE]_ERROR  0
```

Flags sempre declaradas no topo.

---

## 4. Descritivo dos Módulos

### 4.1 main.cpp

* Integra todos os módulos.
* `setup()`: inicializa History, Translator, Telecom, Network, Telegrapher, Display, Buzzer, Blinker.
* `loop()`: chama `update()` de cada módulo.
* Orquestra callbacks e fluxo de eventos.

### 4.2 morse-key

* Captura eventos do botão físico (D5).
* Usa ISR + debounce.
* Envia eventos ao Telegrapher.
* Flags: `LOG_MORSE_KEY_ISR`, `LOG_MORSE_KEY_EVENT`.

### 4.3 telegrapher

* Classifica duração em dot/dash.
* Traduz símbolos em letras.
* Alterna modos: DIDACTIC / MORSE.
* Atualiza History; informa Display; envia símbolos via Network-State.
* Flags: `LOG_TELEGRAPHER_SYMBOL`, `LOG_TELEGRAPHER_MODE`.

### 4.4 morse-telecom

* Protocolo TCP textual (porta 5000).
* Mensagens: `alive`, `duration:<ms>`, `request_tx`, `ok`, `busy`, `mac:<mac>`.
* Flags: `LOG_TELECOM_RX`, `LOG_TELECOM_TX`.

### 4.5 network-state

* FSM: FREE, TX, RX.
* Controla Display e Buzzer.
* Flags: `LOG_NETSTATE_CHANGE`, `LOG_NETSTATE_TIMEOUT`.

### 4.6 network-connect

* Scan, conexão, reconexão, heartbeat.
* Fornece socket para Telecom.
* Flags: `LOG_NETCONNECT_SCAN`, `LOG_NETCONNECT_CONN`.

### 4.7 history

* Buffers circulares TX/RX (30 chars).
* Display lê apenas os últimos 29 chars.
* Layout:

  * Linha 1: 10 chars
  * Linha 2: 10 chars
  * Linha 3: 9 chars
* Flags: `LOG_HISTORY_PUSH`, `LOG_HISTORY_OVERFLOW`.

### 4.8 display-adapter

* Desenha o layout OLED.
* Metade esquerda: TX e RX.
* Metade direita: símbolo/letra por 1.5s.
* Wi‑Fi: canto superior direito.
* Cursor piscante no modo didático.
* Flags: `LOG_DISPLAY_UPDATE`, `LOG_DISPLAY_CACHE`.

### 4.9 buzzer-driver

* Gera padrões sonoros não-bloqueantes.
* Usado por Telegrapher e Network-State.
* Flags: `LOG_BUZZER_INIT`, `LOG_BUZZER_PATTERN`.

### 4.10 blinker

* Traduz frase → Morse → LED pisca em loop.
* Independente dos demais.
* Flags: `LOG_BLINKER_INIT`, `LOG_BLINKER_BUILD`, `LOG_BLINKER_RUN`, `LOG_BLINKER_PHASE`.

### 4.11 translator

* Tabela ASCII ↔ Morse.
* Flags: `LOG_TRANSLATOR_LOOKUP`.

### 4.12 bitmap.h

* Arte em PROGMEM.
* Exibida no splash inicial (3s).

---

## 5. Boas Práticas

* Sempre usar `millis()` + `yield()`.
* Logs com timestamps.
* `strncpy` e checagem de limites em buffers.
* Callbacks para desacoplar módulos.
* Constantes em CAPS.
* Splash: sempre 3 segundos.
* Histórico consistente com 29 chars visíveis.

---

## 6. Fluxo de Interações

### 1. Entrada local (morse-key)

* Botão → Telegrapher → History → Display → Buzzer → Network-State → Telecom.

### 2. Entrada remota

* TCP → Network-Connect → Telecom → Telegrapher → History → Display → Buzzer.

### 3. Blinker

* Independente; LED pisca frase configurada.

### 4. Display

* Lê History; exibe símbolo central; estados TX/RX/Free; Wi‑Fi.

### 5. Buzzer

* Feedback local e remoto.

---

## 7. Testes e Troubleshooting

* Verificar splash de 3s.
* Botão: press → buzzer ON; release → duração logada.
* Classificação: ≤150ms = dot; >150ms = dash.
* Tradução: 800ms sem entrada → converte letra.
* Rede: troca de mensagens entre dois dispositivos.
* Display: histórico, símbolo, Wi‑Fi.

Problemas comuns:

* OLED em branco → SDA/SCL.
* Wi‑Fi preso em AP → revisar SSID.
* Buzzer parado → transistor.
* Debounce ruim → ajustar `DEBOUNCE_TIME`.

---

## 8. Recomendações Avançadas

* SSID e senha configuráveis.
* Níveis de log (DEBUG/INFO/WARN/ERROR).
* Validar pacotes TCP.
* Alinhar tempos do blinker e thresholds.
* Exportar histórico via rede.
* Garantir isolamento modular.

---

## 9. Licença e Créditos

* Licença sugerida: MIT.
* Créditos: Adafruit GFX / SSD1306, ESP8266 core.
* Autor: Allan.

---

## 10. Conclusão

Documento base técnico do projeto, define estrutura modular, padrões, interações, layout do display, fluxos e recomendações para manutenção e expansão.
