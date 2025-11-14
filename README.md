# morse-transceiver

Um transceptor CW (Morse) baseado em ESP8266 que permite enviar/receber sinais Morse localmente (botão + buzzer) e remotamente via Wi‑Fi/TCP entre duas unidades. Fornece feedback visual por LED (pisca mensagens em Morse) e interface por display OLED.

---

## Conteúdo do repositório (descrição breve)
- `morse-project.ino` — setup e loop principal (orquestra módulos)  
- `cw-transceiver.cpp` / `.h` — núcleo CW (entrada, buzzer, tradução, histórico)  
- `network.cpp` / `.h` — gerenciamento Wi‑Fi assíncrono, TCP e protocolo de mensagens  
- `blinker.cpp` / `.h` — converte texto → Morse e pisca LED  
- `display.*` — módulo do display OLED (não enviado aqui; define `initDisplay()` e `updateDisplay()`)  
- `bitmap.h` (opcional) — imagem usada no splash

---

## Visão geral funcional
- Entrada local: botão em D5 (INPUT_PULLUP).  
- Entrada remota: mensagens `duration:<ms>` recebidas via TCP definem pressões remotas.  
- Buzzer em D8: liga enquanto há press local/remoto.  
- Estado da conexão: `FREE`, `TX`, `RX`. Quando um envio local começa e a rede está disponível, a unidade ocupa a rede e envia o duration ao peer.  
- Protocolo TCP textual simples (porta 5000): `alive`, `duration:<ms>`, `request_tx`, `ok`/`busy`, `mac:<mac>`. Heartbeat a cada 1s.  
- Blinker (D4) pisca mensagens em Morse continuamente (mensagem padrão `"SEMPRE ALERTA"`).  
- Operação não bloqueante com `millis()` + `yield()` (ESP8266-friendly).

---

## Esquemático mínimo / mapeamento de pinos
- D5 — botão LOCAL (INPUT_PULLUP; botão fecha para GND)  
- D6 — entrada REMOTE (INPUT_PULLUP)  
- D8 — buzzer (digital ON/OFF) — buzzer passivo pode precisar de driver/transistor  
- D4 — LED blinker (GPIO2; ativo HIGH)  
- I2C display — normalmente D1 = SCL, D2 = SDA (ver `display.*` para confirmar endereço/biblioteca)  
- Alimentação: 5V/USB ou 3.3V conforme placa (usar regulador apropriado)

Notas:
- Botões: usar fiação para curto ao GND (INPUT_PULLUP).  
- Buzzer passivo pode exigir transistor se corrente elevada.  
- Se usar LED externo, colocar resistor adequado (ex.: 220 Ω).

---

## Compilação e gravação

Ambiente sugerido:
- Arduino IDE com suporte ESP8266 instalado, ou PlatformIO (VS Code).  
- Placa: selecionar modelo ESP8266 correto (NodeMCU, Wemos D1 mini, etc).  
- Monitor serial: 115200 baud.

Dependências:
- Core ESP8266 (inclui `ESP8266WiFi`, `WiFiClient`, `ESP8266WiFiMulti`)  
- Biblioteca do display usada em `display.*` (ex.: Adafruit SSD1306 ou U8g2 — verificar no arquivo `display`)

Passos:
1. Copie todos os arquivos para uma pasta de projeto (o `.ino` e os `.cpp/.h`).  
2. Abra no Arduino IDE ou configure `platformio.ini` com `platform: espressif8266`.  
3. Selecione porta e placa corretas.  
4. Compile e faça upload.  
5. Abra o Monitor Serial (115200) para acompanhar logs.

---

## Uso e testes rápidos

1. Inicialização: ao ligar, o dispositivo inicia scan Wi‑Fi assíncrono, mostra splash no display e inicia o blinker. Confira logs serial.  
2. Teste do buzzer e entrada local:
   - Pressione o botão local (D5) e mantenha: buzzer deve ficar ON durante o pressionamento; ao soltar, o código registra Duration no Serial.
   - Curtos/longos: duration <= 150 ms → ponto (`.`); >150 ms → traço (`-`).  
3. Tradução de letra:
   - Após release, aguarde `LETTER_GAP` (800 ms) sem novas pressões; `currentSymbol` é traduzido para letra e adicionada ao histórico (TX se local, RX se remoto).  
4. Teste de rede (duas unidades):
   - Ambas executando firmware podem se encontrar via SSID `morse-transceiver` (SSID aberto por padrão). Elas negociam roles por MAC e estabelecem TCP na porta 5000.
   - Uma unidade envia `duration:<ms>\n` ao peer; o receptor converte em símbolo remoto.
5. Blinker:
   - LED em D4 pisca mensagem padrão. Altere chamando `setBlinkerMessage("TEXTO")`.

---

## Protocolos e mensagens TCP

Porta: `5000`

Mensagens (linha finaliza com `\n`):
- `alive` — heartbeat enviado a cada 1s  
- `duration:<ms>` — comunica duração de um press local ao peer  
- `request_tx` — requisição para iniciar TX; resposta: `ok` ou `busy`  
- `mac:<mac>` — negociação de roles por comparação de MAC

Heartbeat:
- Envia `alive` a cada `HEARTBEAT_INTERVAL` (1s).  
- Se `HEARTBEAT_TIMEOUT` (3s) for ultrapassado sem receber `alive`, desconecta e vai para `DISCONNECTED`.

---

## APIs públicas (resumo para integradores)

cw-transceiver
- `initCWTransceiver()`  
- `updateCWTransceiver()` — chamar frequentemente (ex.: a cada 5 ms)  
- `getConnectionState()` → `FREE|TX|RX`  
- `getMode()` → `DIDACTIC|MORSE`  
- `getCurrentSymbol()`, `getHistoryTX()`, `getHistoryRX()`

network
- `initNetwork()`  
- `updateNetwork()` — chamar periodicamente (ex.: a cada 100 ms)  
- `occupyNetwork()` — retorna true se a rede está disponível para TX  
- `isConnected()`  
- `sendDuration(unsigned long duration)`  
- `getNetworkStrength()` → `"###%"` ou `" OFF"`

blinker
- `initBlinker()`  
- `setBlinkerMessage(const char* newMessage)`  
- `updateBlinker()` — chamar periodicamente (ex.: a cada 100 ms)

display (esperado)
- `initDisplay()`  
- `updateDisplay()` — deve exibir: `netState`, força do sinal, histórico, símbolo atual

---

## Valores e temporizações relevantes

- `DEBOUNCE_TIME` = 25 ms  
- `SHORT_PRESS` = 150 ms (<= → `.`)  
- `LONG_PRESS` = 400 ms (uso especial: press >= `LONG_PRESS * 5` alterna modo)  
- `LETTER_GAP` (cw-transceiver) = 800 ms  
- `INACTIVITY_TIMEOUT` = 5000 ms

Blinker:
- `DOT_TIME` = 300 ms  
- `DASH_TIME` = 600 ms  
- `SYMBOL_GAP` = 300 ms  
- `LETTER_GAP` = 600 ms  
- `WORD_GAP` = 1800 ms

Recomendação: alinhar DOT/DASH do blinker com `SHORT_PRESS` / `LONG_PRESS` para feedback coerente.

---

## Problemas comuns e soluções

- Display não aparece: verifique SDA/SCL e biblioteca usada; confirmar endereço I2C no `display.*`.  
- Wi‑Fi repetidamente em AP_MODE: ver logs de scan; SSID aberto `morse-transceiver` é padrão.  
- Buzzer não soa: confirme fiação no D8; buzzer passivo pode precisar de transistor.  
- Debounce: ajustar `DEBOUNCE_TIME` se houver press/release espúrios.  
- Tradução inválida: `translateMorse()` retorna `'\0'` se o símbolo não existe; adicionar fallback (`'?'`) se preferir.

---

## Melhorias sugeridas
- Tornar senha do SSID configurável (PASS vazio é inseguro).  
- Resetar `retryDelay` em reconexão bem-sucedida e impor limite máximo de backoff.  
- Implementar níveis de log (DEBUG/INFO) para reduzir `Serial.print` em produção.  
- Validar/limitar `duration` recebidos via TCP.  
- Sincronizar tempos entre blinker e cw-transceiver.  
- Em `captureInput`, notificar display quando `occupyNetwork()` falhar.

---

## Licença
Adicionar um arquivo `LICENSE` (ex.: MIT) ao repositório se quiser permissões explícitas.

---

## Contato e contribuição
Abra issues ou PRs no repositório para:
- correções de bugs  
- adicionar suporte a displays diferentes  
- melhorar o protocolo de rede (autenticação, reconexão robusta)  
- sugestões de UX (feedback visual/sonoro)
