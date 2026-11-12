# Cliente ESP8266 — Guía

Este repositorio (`proxy-openrouter-ia-chinas`) incluye un **firmware de
ejemplo** (`firmware_esp8266/`) que convierte una placa **ESP8266 (NodeMCU v3)**
en un terminal serie capaz de hacer preguntas a la IA y mostrar la respuesta,
consumiendo el endpoint local `GET /api/query` del proxy.

## Requisitos de hardware y software

- **Placa:** ESP8266 (NodeMCU v3 / ESP-12E con chip CH340).
- **Herramientas:** `arduino-cli` con el core `esp8266` (3.x).
- **Librería:** [ArduinoJson](https://github.com/bblanchon/ArduinoJson) (7.x).

```bash
arduino-cli core install esp8266:esp8266
arduino-cli lib install ArduinoJson
```

## Qué hace el firmware

1. Conecta a una red WiFi **2.4 GHz** (`WIFI_SSID` / `WIFI_PASS`).
2. En el `loop()` lee una línea por el puerto serie (115200 baud) hasta `\n`.
3. Hace `GET http://<SERVER_IP>:<SERVER_PORT>/api/query?q=<urlencoded>` con
   `ESP8266HTTPClient`.
4. Parsea el JSON con ArduinoJson e imprime el resultado:
   - Éxito → `Modelo: <id>` + respuesta en texto.
   - Fallo → `[API] Error: <detalle>`.
5. Si pierde la conexión WiFi, intenta reconectar cada 15 s.

## Configuración (en `mega_free_chat_8266.ino`)

| Constante | Valor | Descripción |
|---|---|---|
| `WIFI_SSID` | `"..."` | Red WiFi **2.4 GHz** |
| `WIFI_PASS` | `"..."` | Contraseña de la red |
| `SERVER_IP` | `"192.0.2.10"` | IP local del notebook (donde corre el proxy) |
| `SERVER_PORT` | `8001` | Puerto del proxy |
| `SERIAL_BAUD` | `115200` | Baudeaje del monitor serie |
| `BUFFER_SIZE` | `256` | Largo máximo de la pregunta |

### ⚠️ 5 GHz vs 2.4 GHz

La **ESP8266 solo admite 2.4 GHz**. Si tu red es de 5 GHz (SSID como "... 5G"),
la placa **no la verá**. Usa la red 2.4 GHz gemela (p. ej. "... 2.4 Ghz") en la
misma **subred** que el notebook (mismo `192.168.1.x`).

## Compilar y subir

```bash
cd firmware_esp8266
arduino-cli compile --fqbn esp8266:esp8266:nodemcu mega_free_chat_8266
arduino-cli upload --fqbn esp8266:esp8266:nodemcu --port /dev/ttyUSB0 mega_free_chat_8266
```

> Cierra el monitor serie (picocom/cutecom) antes de subir.

## Uso

```bash
picocom -b 115200 /dev/ttyUSB0     # UTF-8 correcto; salir: Ctrl+A Ctrl+X
```

1. Espera a que conecte y muestre la IP.
2. Escribe una pregunta y pulsa **ENTER**.
3. Verás el modelo usado y la respuesta.

Ejemplo:

```
Pregunta: me ayudas
[API] HTTP: 200
========================================
Modelo: inclusionai/ling-3.0-flash-fin:free
----------------------------------------
¡Claro que sí! Estoy aquí para ayudarte...
========================================
```

## Formato de respuesta de la API

```json
{"ok": true, "model": "<modelo>", "text": "<respuesta>", "error": null}
{"ok": false, "model": null, "text": null, "error": "<detalle>"}
```

## Solución de problemas

| Síntoma | Causa probable | Fix |
|---|---|---|
| `[WiFi] No se pudo conectar` | SSID 5 GHz o contraseña errónea | Usar red 2.4 GHz y `WIFI_PASS` correcta |
| `[API] Falla de conexión` | Proxy apagado o IP errónea | Arrancar proxy (`./run.sh`) y verificar `SERVER_IP`/`SERVER_PORT` |
| `[API] Error HTTP 4xx` | Key inválida / sin modelos free | Verificar `OPENROUTER_API_KEY` y cuota |
| Caracteres `<0xc3><0xa1>` | Terminal en ASCII/Latin-1 | Usar picocom / terminal UTF-8 (es UTF-8 normal) |
