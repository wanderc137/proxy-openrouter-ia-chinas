# Datasheet de usuario y operador

**Proxy OpenRouter IA Chinas** · versión 1.0.0 · documentación para quien
instala, opera y diagnostica el servicio.

---

## 1. Resumen ejecutivo

Este servicio convierte una notebook o una Raspberry Pi en una **puerta de
entrada local a modelos de lenguaje gratuitos**. Los clientes hacen una petición
HTTP muy simple (`GET /api/query?q=...`) y reciben un JSON de cuatro campos con
la respuesta. Detrás, el proxy mantiene un catálogo de modelos cuyo precio es
`$0` en OpenRouter, prioriza los modelos de proveedores chinos y prueba uno tras
otro hasta obtener respuesta.

Está pensado para:

- microcontroladores con poca RAM y sin TLS moderno (ESP8266, ESP32);
- scripts de automatización que no quieren integrar el SDK de OpenRouter;
- pruebas y prototipos donde importa más la simplicidad que la latencia.

No está pensado para: producción multi-usuario, datos sensibles, cargas altas,
ni para revender acceso a modelos.

| Atributo | Valor |
|---|---|
| Tipo | Servicio HTTP local (proxy de API) |
| Framework | FastAPI + Uvicorn |
| Puerto por defecto | `8001` |
| Protocolo | HTTP/1.1, sin TLS |
| Endpoint principal | `GET /api/query` |
| Formato de respuesta | JSON compacto, sin streaming |
| Estado | En memoria (sin base de datos) |
| Autenticación de entrada | Ninguna |
| Autenticación hacia OpenRouter | API key propia (`Bearer`) |
| Cliente incluido | Firmware ESP8266 (NodeMCU) |

---

## 2. Requisitos

### 2.1 Servidor

| Componente | Mínimo | Recomendado |
|---|---|---|
| Python | 3.9 | 3.11 o 3.12 |
| RAM libre | ~100 MB | 256 MB |
| Red | Salida HTTPS a `openrouter.ai` | Salida estable, sin proxy interceptado |
| Sistema | Linux, macOS o Windows con shell | Linux (por `run.sh`) |
| Cuenta | API key de OpenRouter | Key con crédito `$0` y límites propios |

No se necesita GPU. Todo el cómputo ocurre en los servidores de OpenRouter.

### 2.2 Cliente microcontrolador (opcional)

| Componente | Detalle |
|---|---|
| Placa | ESP8266 (NodeMCU v3 / ESP-12E) o ESP32 con adaptación del sketch |
| WiFi | **2.4 GHz obligatorio** en ESP8266 |
| Alimentación | 5 V por micro-USB |
| Cable | USB de datos (no solo carga) |
| Herramientas | `arduino-cli`, core `esp8266`, librería `ArduinoJson` |
| Monitor serie | 115200 baudios, UTF-8 |

### 2.3 Dependencias de Python

El proyecto declara sus dependencias en `requirements.txt`, **sin fijar
versión** (`fastapi`, `uvicorn[standard]`, `openai`, `httpx`,
`python-dotenv`). Esto significa que una instalación nueva resuelve las últimas
versiones compatibles en el momento; para reproducibilidad a largo plazo
conviene generar un `requirements.lock` o fijar versiones tras validar.

Instalación dentro del entorno virtual del proyecto:

```bash
cd proxy-openrouter-ia-chinas
python3 -m venv .venv
source .venv/bin/activate          # Linux / macOS
# .venv\Scripts\activate           # Windows (cmd)
# source .venv/Scripts/activate    # Windows (Git Bash)

python -m pip install --upgrade pip
pip install -r requirements.txt
```

Verificación rápida:

```bash
python -c "import fastapi, uvicorn, openai, httpx, dotenv; print('ok')"
```

`run.sh` automatiza lo anterior: crea `.venv` si no existe e instala las
dependencias solo si `uvicorn` no está disponible.

---

## 3. Instalación paso a paso

### 3.1 Obtener el código

```bash
git clone https://github.com/wanderc137/proxy-openrouter-ia-chinas.git
cd proxy-openrouter-ia-chinas
```

### 3.2 Crear el entorno

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### 3.3 Configurar la clave

```bash
cp .env.example .env
```

Editar `.env` y sustituir el marcador:

```dotenv
OPENROUTER_API_KEY=sk-or-v1-...
BACKOFF_SECONDS=300
POOL_REFRESH_SECONDS=21600
DEBUG=false
```

Reglas prácticas:

- La clave se obtiene en <https://openrouter.ai/keys>.
- `.env` está en `.gitignore`; nunca debe subirse.
- Permisos sugeridos: `chmod 600 .env`.
- No se necesita crédito para modelos `$0`, pero sí una cuenta válida.

### 3.4 Arrancar

Opción A — script (primer plano, recomendado para uso diario):

```bash
./run.sh
```

El script:

1. crea `.venv` si falta;
2. instala `requirements.txt` si `uvicorn` no está en el venv;
3. copia `.env.example` a `.env` si no existe;
4. aborta si `OPENROUTER_API_KEY` no empieza por `sk-or-v1-`;
5. ejecuta `server.py` en la terminal actual.

Para detenerlo: **Ctrl+C**. No deja procesos en segundo plano.

Opción B — Uvicorn directo:

```bash
source .venv/bin/activate
uvicorn server:app --host 0.0.0.0 --port 8001
```

Opción C — Python directo:

```bash
source .venv/bin/activate
python server.py
```

### 3.5 Comprobar que está vivo

```bash
curl -s http://localhost:8001/
```

Respuesta esperada:

```json
{
  "name": "proxy-openrouter-ia-chinas",
  "version": "1.0.0",
  "uso": "/api/query?q=<pregunta>[&key=sk-or-v1-...]"
}
```

Este healthcheck **no** comprueba la conectividad con OpenRouter; solo indica
que el proceso responde.

---

## 4. Operación

### 4.1 Consulta básica

```bash
curl -s "http://localhost:8001/api/query?q=que+es+un+resistor"
```

```json
{
  "ok": true,
  "model": "inclusionai/ling-3.0-flash-fin:free",
  "text": "Un resistor es un componente pasivo que se opone al paso de la corriente...",
  "error": null
}
```

### 4.2 Consulta desde otro equipo de la LAN

Sustituir `localhost` por la IP del equipo servidor:

```bash
curl -s "http://192.0.2.10:8001/api/query?q=hola"
```

Si no responde, revisar firewall y que el proceso escuche en `0.0.0.0`, no en
`127.0.0.1`.

### 4.3 Consulta con clave explícita

```bash
curl -s "http://192.0.2.10:8001/api/query?q=hola&key=sk-or-v1-..."
```

Advertencia: la clave queda en el historial del shell y en logs intermedios.
Usar solo para pruebas. Cada llamada con `key` reconstruye el pool y descarga
el catálogo, por lo que es más lenta que la ruta con `.env`.

### 4.4 Uso desde el ESP8266

1. Compilar y cargar `firmware_esp8266/mega_free_chat_8266` (ver
   [`firmware_esp8266.md`](firmware_esp8266.md)).
2. Abrir el monitor serie a **115200** baudios.
3. Esperar el mensaje `[WiFi] Conectado. IP: ...`.
4. Escribir la pregunta y pulsar **ENTER**.
5. Leer el bloque con `Modelo:` y la respuesta.

El firmware tiene un timeout de 30 s por consulta y reconecta WiFi cada 15 s si
se cae. La pregunta máxima es de 255 caracteres (buffer de 256).

---

## 5. Referencia de la API

### 5.1 `GET /`

| Campo | Tipo | Descripción |
|---|---|---|
| `name` | string | Nombre del servicio |
| `version` | string | Versión declarada en FastAPI |
| `uso` | string | Recordatorio del endpoint principal |

Códigos: `200`.

### 5.2 `GET /api/query`

| Parámetro | Ubicación | Obligatorio | Límites | Descripción |
|---|---|---|---|---|
| `q` | query string | Sí | 1–2000 caracteres | Pregunta en texto plano |
| `key` | query string | No | — | API key de OpenRouter; si falta se usa `.env` |

Respuesta de éxito (`200`):

```json
{"ok": true, "model": "proveedor/modelo:free", "text": "respuesta", "error": null}
```

Respuesta de fallo lógico (también `200`):

```json
{"ok": false, "model": null, "text": null, "error": "Todos los modelos gratuitos fallaron. Detalle: ..."}
```

Errores de validación (`422`) los genera FastAPI cuando `q` falta, está vacío o
supera 2000 caracteres. El cuerpo sigue el formato estándar de FastAPI
(`detail` con la lista de errores), **no** el formato de cuatro campos.

### 5.3 Semántica de `ok`

- `ok: true` → el campo `text` contiene la respuesta completa; `model` indica
  qué modelo gratuito la produjo.
- `ok: false` → `text` es `null`; `error` explica el motivo. Puede ser un fallo
  de todos los modelos, una key inválida o un error interno.

El código HTTP es `200` en ambos casos porque el proxy comunica el resultado en
el cuerpo, no en el status. Los clientes deben mirar `ok`, no solo el código.

### 5.4 Ejemplos de error

Key inválida:

```json
{"ok": false, "model": null, "text": null,
 "error": "Error interno: Error code: 401 - {'error': {'message': 'No auth credentials found'}}"}
```

Sin modelos gratuitos:

```json
{"ok": false, "model": null, "text": null,
 "error": "No hay ningún modelo gratuito disponible en este momento."}
```

Todos fallaron:

```json
{"ok": false, "model": null, "text": null,
 "error": "Todos los modelos gratuitos fallaron. Detalle: proveedor/a: rate limit (429) | proveedor/b: HTTP 503"}
```

---

## 6. Configuración

### 6.1 Variables de entorno

| Variable | Obligatoria | Default | Efecto real |
|---|---|---|---|
| `OPENROUTER_API_KEY` | Sí | — | Se usa en el pool y en el cliente. Sin ella, las llamadas a OpenRouter fallan con 401. |
| `BACKOFF_SECONDS` | No | `300` | Cooldown base tras un fallo. Con `429` se duplica (600 s). |
| `POOL_REFRESH_SECONDS` | No | `21600` | Cada cuánto se vuelve a descargar el catálogo (6 h). |
| `DEBUG` | No | `false` | `true` activa nivel DEBUG en el logger raíz. |
| `OR_BASE_URL` | No | `https://openrouter.ai/api/v1` | Base URL de OpenRouter; útil para un mock local. |

`OR_BASE_URL` no aparece en `.env.example`, pero el código la lee. Documentarla
es importante para pruebas: permite apuntar a un servidor falso sin tocar el
código.

### 6.2 Puerto

El puerto está fijado en `server.py` (`uvicorn.run(..., port=8001)`) y en
`run.sh` (`PORT`, por defecto `8001`). Si se arranca con Uvicorn directamente,
el puerto se pasa por CLI. El firmware usa `SERVER_PORT = 8001`; si se cambia el
puerto del servidor hay que recompilar el sketch.

### 6.3 Logs

El logger se llama `proxy_openrouter_ia` (servidor), `mega_free_chat.pool`,
`mega_free_chat.router` y `mega_free_chat.client`. Formato:

```
2026-01-01 12:00:00,000 INFO proxy_openrouter_ia: ...
```

Niveles esperados:

- `INFO` → inicio, modelo elegido, pool actualizado;
- `WARNING` → modelo con 429, pool no actualizado, key ausente;
- `DEBUG` → detalle del SDK (solo con `DEBUG=true`);
- `ERROR` → excepción no controlada en `/api/query`.

---

## 7. Comportamiento interno (lo que el operador debe saber)

1. **Catálogo perezoso.** La primera consulta descarga `/models`. Puede tardar
   unos segundos. Las siguientes usan la copia en memoria hasta que expire
   `POOL_REFRESH_SECONDS`.
2. **Filtro estricto `$0`.** Solo entran modelos con `pricing.prompt == "0"` y
   `pricing.completion == "0"`.
3. **Prioridad china.** Los modelos detectados como chinos se intentan primero,
   en orden alfabético por nombre. Luego el resto.
4. **Cooldown.** Un fallo saca al modelo de rotación durante 300 s; un `429`,
   durante 600 s. Al expirar, vuelve a estar disponible.
5. **Sin memoria de conversación.** Cada request es independiente; el proxy
   envía un único mensaje de rol `user`.
6. **Sin streaming al cliente.** El proxy acumula los fragmentos y responde
   una sola vez.
7. **Pools efímeros con `key`.** Si se pasa `key`, se crea un pool nuevo para
   esa petición; no comparte catálogo ni cooldowns con el global.
8. **Failover parcial.** Los errores que aparecen al abrir el stream pueden
   escapar del router y devolverse como `Error interno`. Ver
   [`developer.md`](developer.md), sección 8.1.

---

## 8. Mantenimiento

| Tarea | Frecuencia | Cómo |
|---|---|---|
| Revisar que hay modelos `$0` | Semanal | <https://openrouter.ai/models?max_price=0> |
| Rotar la API key | Ante sospecha o cada 90 días | Crear nueva en OpenRouter, actualizar `.env`, reiniciar |
| Actualizar dependencias | Mensual | `pip install -U -r requirements.txt` en el venv |
| Revisar logs | Semanal | Buscar `429`, `401` y `No se pudo actualizar el pool` |
| Verificar el firmware | Al cambiar IP/puerto/red | Recompilar y volver a cargar |
| Respaldar `.env` | Al cambiar | Guardar en gestor de secretos, nunca en git |

Reinicio limpio:

```bash
# Ctrl+C en la terminal donde corre
./run.sh
```

---

## 9. Diagnóstico de problemas

| Síntoma | Causa probable | Acción |
|---|---|---|
| `curl: connection refused` | Servicio apagado o puerto distinto | Arrancar `./run.sh`; verificar `8001` |
| `ok:false` con `No auth credentials found` | Key vacía o inválida | Revisar `.env`; probar la key contra OpenRouter |
| `ok:false` con `No hay ningún modelo gratuito` | El catálogo no tiene modelos `$0` o el filtro no encontró ninguno | Verificar el catálogo en la web; esperar; revisar `DEBUG` |
| `ok:false` con `rate limit (429)` en todos | Cuota gratuita agotada | Esperar el cooldown; usar otra key; bajar frecuencia |
| Primera consulta muy lenta | Descarga inicial del catálogo | Es esperado; siguientes consultas usan caché |
| Respuestas cortadas | Timeout del cliente | Subir timeout del cliente; acortar la pregunta |
| ESP8266 no ve la red | Red de 5 GHz | Usar SSID de 2.4 GHz |
| ESP8266 conecta pero no consulta | IP/puerto incorrectos o firewall | `ping` desde otro equipo; revisar `SERVER_IP`; abrir puerto en LAN |
| JSON no parsea en el ESP8266 | Respuesta mayor que el buffer o error HTTP | Subir `DynamicJsonDocument`; revisar payload en serie |
| Acentos raros en serie | Terminal en Latin-1 | Usar UTF-8 (`picocom` lo respeta) |
| `No se pudo actualizar el pool` | Sin Internet, DNS o OpenRouter caído | Probar `curl https://openrouter.ai/api/v1/models` |
| `422` al consultar | `q` vacío o mayor de 2000 | Ajustar la pregunta |

---

## 10. Seguridad operativa

- El endpoint **no autentica**. Cualquiera en la LAN puede consumir la cuota.
- El servicio escucha en `0.0.0.0`. Si no se necesita acceso remoto, arrancar
  con `--host 127.0.0.1` (o cambiar `server.py`, que hoy fija `0.0.0.0`).
- No exponer el puerto a Internet. Si se requiere acceso externo, usar una VPN
  y autenticación delante del proxy.
- La API key viaja al upstream en cada llamada; nunca se devuelve al cliente.
  Pero el parámetro `key` sí la expone en la URL de entrada.
- Los logs no imprimen la clave, pero sí el id del modelo y mensajes de error
  de OpenRouter.
- `.env` debe tener permisos restringidos y estar fuera de git.
- El firmware contiene credenciales WiFi en claro: trátese como deuda técnica
  y no reutilizar ese archivo con datos reales en repositorios públicos.

---

## 11. Glosario

| Término | Significado |
|---|---|
| **Pool** | Lista en memoria de modelos gratuitos con su estado. |
| **Cooldown / backoff** | Periodo durante el cual un modelo que falló no se intenta. |
| **Failover** | Pasar automáticamente al siguiente modelo cuando uno falla. |
| **Modelo `$0`** | Modelo cuyo `pricing.prompt` y `pricing.completion` valen `"0"`. |
| **Modelo chino** | Modelo detectado por proveedor o keyword (GLM, Qwen, DeepSeek, etc.). |
| **Streaming** | Entrega de la respuesta por fragmentos; el proxy lo usa internamente. |
| **FQBN** | Fully Qualified Board Name de Arduino (`esp8266:esp8266:nodemcuv2`). |
| **Rate limit** | Límite de peticiones; OpenRouter responde `429` al superarlo. |

---

## 12. Soporte y alcance

Este es un proyecto personal publicado como referencia. No hay SLA ni soporte
formal. Los problemas deben reportarse con:

1. versión de Python y sistema operativo;
2. salida de `curl -s http://localhost:8001/`;
3. una consulta de ejemplo y su respuesta JSON;
4. fragmento de log con `DEBUG=true`, **sin la API key**.

No se aceptan solicitudes para evadir rate limits ni para usar modelos de pago
sin pagarlos. El proyecto usa exclusivamente las rutas públicas `$0` de
OpenRouter con una key propia.
