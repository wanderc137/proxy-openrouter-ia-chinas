# Referencias y enlaces oficiales

Colección curada de documentación primaria utilizada para diseñar, implementar
y operar este proyecto. Todos los enlaces apuntan a fuentes oficiales o a la
documentación mantenida por los autores de cada tecnología. No se incluyen
blogs de terceros ni tutoriales informales.

> **Nota:** las URLs se verificaron en noviembre de 2026. Los sitios de
> OpenRouter cambian con frecuencia porque su catálogo y su API evolucionan;
> si un enlace profundo deja de funcionar, navega desde la raíz del sitio.

---

## 1. OpenRouter (proveedor de modelos)

| Recurso | Enlace | Utilidad para este proyecto |
|---|---|---|
| Portal principal | <https://openrouter.ai> | Punto de entrada: catálogo, precios, estado de modelos y creación de cuenta. |
| Documentación | <https://openrouter.ai/docs> | Documentación general de la API compatible con OpenAI. |
| Referencia de la API | <https://openrouter.ai/docs/api-reference/overview> | Autenticación con `Authorization: Bearer`, formato de peticiones y respuestas. |
| Endpoint de modelos | <https://openrouter.ai/docs/api-reference/list-available-models> | Contrato de `GET /api/v1/models`: estructura del campo `pricing` y `supported_parameters`. Es la base del filtro de modelos `$0` en `src/models_pool.py`. |
| Streaming de respuestas | <https://openrouter.ai/docs/api-reference/streaming> | Formato SSE que consume `src/openrouter_client.py` mediante el SDK de OpenAI. |
| Manejo de errores | <https://openrouter.ai/docs/api-reference/errors> | Significado de `429`, `401`, `402`, `503` y cuándo reintentar. |
| Catálogo con filtro de precio | <https://openrouter.ai/models?max_price=0> | Verificación manual de qué modelos están en `$0` en un momento dado. |
| Gestión de claves | <https://openrouter.ai/keys> | Crear, rotar y revocar la API key. |
| Preguntas frecuentes | <https://openrouter.ai/docs/faq> | Límites de rate, políticas de uso y funcionamiento del free tier. |
| Estado del servicio | <https://status.openrouter.ai> | Comprobar incidentes antes de culpar al proxy. |
| Cabeceras de atribución | <https://openrouter.ai/docs/app-attribution> | Uso de `HTTP-Referer` y `X-Title`, enviadas por `src/openrouter_client.py`. |

### Por qué importa el campo `pricing`

`GET /api/v1/models` devuelve `pricing.prompt` y `pricing.completion` como
**cadenas**, no como números. Un modelo es gratuito cuando ambos valen
exactamente `"0"`. Comparar contra `0` numérico o usar una tolerancia puede
incluir modelos de pago con precios muy bajos. Este detalle está documentado en
la referencia del endpoint y es el que implementa el pool.

---

## 2. FastAPI y servidor ASGI

| Recurso | Enlace | Utilidad para este proyecto |
|---|---|---|
| Documentación de FastAPI | <https://fastapi.tiangolo.com/> | Framework del endpoint `GET /api/query`. |
| Parámetros de query | <https://fastapi.tiangolo.com/tutorial/query-params/> | Validación de `q` con `min_length` y `max_length`. |
| Respuestas JSON | <https://fastapi.tiangolo.com/advanced/response-directly/> | Uso de `JSONResponse` para controlar el cuerpo exacto. |
| Concurrencia y `async` | <https://fastapi.tiangolo.com/async/> | Motivo por el que el trabajo bloqueante se ejecuta con `asyncio.to_thread`. |
| Documentación de Uvicorn | <https://www.uvicorn.org/> | Servidor ASGI, `--host`, `--port`, workers y logs. |
| Ajustes de despliegue | <https://www.uvicorn.org/deployment/> | Consideraciones para ejecutar detrás de un proxy o en primer plano. |

---

## 3. SDK y utilidades de Python

| Recurso | Enlace | Utilidad para este proyecto |
|---|---|---|
| SDK oficial de OpenAI para Python | <https://github.com/openai/openai-python> | Cliente usado contra el endpoint compatible de OpenRouter. |
| Referencia del SDK | <https://platform.openai.com/docs/api-reference> | Forma de `chat.completions.create` y del streaming por chunks. |
| `httpx` | <https://www.python-httpx.org/> | Cliente HTTP usado para descargar el catálogo de modelos. |
| `python-dotenv` | <https://github.com/theskumar/python-dotenv> | Carga de `.env` sin exportar variables manualmente. |
| `asyncio.to_thread` | <https://docs.python.org/3/library/asyncio-task.html#asyncio.to_thread> | Ejecutar el router síncrono sin bloquear el event loop. |
| `threading.Lock` | <https://docs.python.org/3/library/threading.html#lock-objects> | Protección del estado del pool entre hilos. |
| `dataclasses` | <https://docs.python.org/3/library/dataclasses.html> | Modelo `FreeModel` con estado mutable. |
| Protocolo HTTP/1.1 | <https://www.rfc-editor.org/rfc/rfc9110> | Semántica de métodos, códigos de estado y cabeceras. |

---

## 4. ESP8266 y Arduino

| Recurso | Enlace | Utilidad para este proyecto |
|---|---|---|
| Core ESP8266 para Arduino | <https://github.com/esp8266/Arduino> | Toolchain, ejemplos y notas de versión del core. |
| Documentación del core | <https://arduino-esp8266.readthedocs.io/> | Referencia de `ESP8266WiFi`, `ESP8266HTTPClient` y límites del chip. |
| `ESP8266WiFi` | <https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/readme.html> | Modo estación, `WiFi.begin`, reconexión y estado. |
| `ESP8266HTTPClient` | <https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/readme.html#client> | `http.begin`, `GET`, `getString`, timeouts y códigos de error. |
| Arduino CLI | <https://arduino.github.io/arduino-cli/> | Compilación y carga del sketch sin IDE. |
| Instalación de cores | <https://arduino.github.io/arduino-cli/latest/getting-started/> | `core install`, `lib install`, `board list`. |
| ArduinoJson | <https://arduinojson.org/> | Parseo del JSON de respuesta en el microcontrolador. |
| Asistente de ArduinoJson | <https://arduinojson.org/v7/assistant/> | Calcular el tamaño de `DynamicJsonDocument` para una respuesta dada. |
| Documentación de `String` | <https://www.arduino.cc/reference/en/language/variables/data-types/stringobject/> | Concatenación y reserva de memoria en el sketch. |
| Codificación percent-encoding | <https://www.rfc-editor.org/rfc/rfc3986#section-2.1> | Base de la función `urlencode()` del firmware. |
| NodeMCU (hardware) | <https://nodemcu.readthedocs.io/> | Referencia de pines y variantes de la placa. |

### Nota sobre el core y el FQBN

El identificador de placa (`FQBN`) depende del core instalado. Para una NodeMCU
v3 con ESP-12E y 4 MB de flash, el valor habitual es
`esp8266:esp8266:nodemcuv2`. El core también ofrece `esp8266:esp8266:nodemcu`
para variantes antiguas con 512 KB. Si la compilación falla por tamaño o la
carga no arranca, revisa el FQBN y el tamaño de flash declarado; la lista
completa se obtiene con `arduino-cli board listall esp8266`.

---

## 5. Redes, seguridad y operación

| Recurso | Enlace | Utilidad para este proyecto |
|---|---|---|
| Direcciones IPv4 privadas (RFC 1918) | <https://www.rfc-editor.org/rfc/rfc1918> | Rangos válidos para la red local donde vive el proxy (`192.168.x.x`, `10.x.x.x`, `172.16-31.x.x`). |
| mDNS / DNS de servicio | <https://www.rfc-editor.org/rfc/rfc6762> | Alternativa a hardcodear IPs; permite usar `host.local` si el entorno lo soporta. |
| OWASP API Security Top 10 | <https://owasp.org/API-Security/editions/2023/en/0x00-header/> | Riesgos de exponer una API sin autenticación (API1, API2, API4). |
| Tor Project — servicios onion | <https://community.torproject.org/onion-services/> | Solo relevante si en el futuro se quisiera exponer el proxy fuera de la LAN; **no** es parte de este diseño. |
| Guía de systemd | <https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html> | Opción para ejecutar el proxy como servicio en lugar de `run.sh`. |
| `ufw` (manual) | <https://manpages.ubuntu.com/manpages/noble/en/man8/ufw.8.html> | Restringir el puerto 8001 a la subred local. |
| Licencia MIT | <https://opensource.org/license/mit> | Texto de referencia de la licencia del repositorio. |
| Choose a License | <https://choosealicense.com/licenses/mit/> | Comparativa de licencias permisivas. |

---

## 6. Documentación interna del repositorio

| Documento | Ruta | Contenido |
|---|---|---|
| README principal | [`../README.md`](../README.md) | Visión general, instalación, API, firmware y seguridad. |
| Datasheet de usuario | [`datasheet.md`](datasheet.md) | Manual de operación paso a paso. |
| Guía de desarrollo | [`developer.md`](developer.md) | Arquitectura, módulos, firmas y extensión. |
| Guía del firmware | [`firmware_esp8266.md`](firmware_esp8266.md) | Conexión, compilación y uso del cliente ESP8266. |
| Bitácora técnica | [`BITACORA.md`](BITACORA.md) | Decisiones, problemas y lecciones aprendidas. |
| Diagramas | [`assets/`](assets/) | SVG de arquitectura, flujo de petición y despliegue. |

---

## 7. Cómo mantener esta lista

1. Añadir solo enlaces oficiales o documentación de los mantenedores.
2. Incluir una frase que explique **para qué** sirve el enlace en este proyecto.
3. Revisar los enlaces profundos cada vez que se actualice una dependencia
   mayor (FastAPI, SDK de OpenAI, core ESP8266).
4. No enlazar a documentos internos de planificación ni a informes de auditoría:
   esos artefactos no forman parte de la documentación publicada.
