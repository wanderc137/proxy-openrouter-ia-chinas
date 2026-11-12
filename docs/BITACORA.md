# Bitácora técnica

Registro de decisiones, problemas reales y enfoques descartados durante la
construcción del proxy y de su cliente ESP8266. El objetivo es que quien retome
el proyecto no repita caminos ya recorridos y entienda **por qué** el código es
como es, incluidas sus deudas.

Fechas de los commits originales: **28 de agosto de 2026**. Esta bitácora se
redactó en noviembre de 2026 a partir del código y del historial de git.

---

## 1. Contexto de origen

El proxy nace como una escisión de `mega-free-chat`, una interfaz web que ya
usaba modelos gratuitos de OpenRouter. Para conectar un ESP8266 no hacía falta
la UI: hacía falta un endpoint estable, pequeño y sin streaming. De ahí la
decisión de crear un repositorio independiente con **solo** el proxy
(`/api/query`) y el firmware cliente.

Consecuencia práctica: este repositorio no comparte código con la UI. Si se
cambia el formato de respuesta, hay que actualizar ambos proyectos por
separado. Se aceptó ese coste a cambio de tener una superficie mínima.

---

## 2. Decisiones de diseño

### 2.1 Puerto 8001 en lugar de 8000

**Decisión:** el proxy escucha en `8001`.

**Motivo:** la UI web de `mega-free-chat` ocupa el `8000`. Con puertos distintos
ambos servicios pueden convivir en la misma máquina sin coordinación, y el
firmware apunta sin ambigüedad al proxy.

**Coste:** hay comentarios antiguos en el sketch que mencionan el puerto `8000`
mientras la constante real es `8001`. Es deuda documental, no funcional: el
código usa `SERVER_PORT = 8001`.

### 2.2 Un único `GET` con JSON de cuatro campos

**Decisión:** `GET /api/query?q=...` devuelve exactamente
`{"ok", "model", "text", "error"}`.

**Motivo:** el cliente es un microcontrolador. Un `GET` evita construir cuerpos
y cabeceras; cuatro claves evitan un parser genérico. El firmware usa
ArduinoJson con un buffer fijo y puede extraer los campos con `doc["..."] | ""`.

**Alternativas descartadas:**

- **`POST` con JSON:** más correcto semánticamente para texto largo, pero
  obliga al ESP8266 a enviar `Content-Type`, `Content-Length` y un cuerpo. No
  aporta ventajas frente a una query string de hasta 2000 caracteres.
- **SSE / streaming al cliente:** el ESP8266 tendría que parsear eventos
  incrementales y manejar cortes. El valor añadido es bajo para preguntas
  cortas y complica mucho el firmware. Se mantiene streaming **solo** entre el
  proxy y OpenRouter, para no bloquear la conexión completa.
- **Devolver el JSON crudo de OpenRouter:** incluye `usage`, `id`, `choices`,
  `finish_reason`, etc. Ocupa más RAM y obliga al cliente a navegar una
  estructura anidada. Se normaliza en el servidor.

### 2.3 Catálogo dinámico en lugar de lista hardcodeada

**Decisión:** descubrir los modelos gratuitos consultando
`GET /api/v1/models` y filtrar por precio.

**Motivo:** los modelos `$0` de OpenRouter son promociones temporales. Una lista
fija queda obsoleta en días y produce errores 404/402 difíciles de diagnosticar
en el cliente. El catálogo dinámico hace que el proxy se adapte sin recompilar
ni reiniciar.

**Coste:** la primera consulta depende de la red y del endpoint de catálogo; si
OpenRouter cambia el esquema de `pricing`, el filtro deja de encontrar modelos.

### 2.4 Filtro estricto de precio

**Decisión:** exigir `pricing.prompt == "0"` **y** `pricing.completion == "0"`
comparando cadenas.

**Motivo:** la API devuelve los precios como strings. Comparar contra el número
`0` o usar `<= 0` es frágil; una tarifa de `0.0000001` seguiría siendo de pago.
La comparación exacta con `"0"` es la única que garantiza coste cero según el
contrato observado.

**Riesgo asumido:** si OpenRouter empieza a devolver `"0.0"` o `"0e0"`, el
filtro se vacía. En ese caso hay que normalizar el valor, no relajar la
comparación sin revisar el formato.

### 2.5 Prioridad a modelos chinos

**Decisión:** ordenar candidatos con
`(not is_chinese, name.lower())`.

**Motivo:** el objetivo declarado del proyecto es consumir modelos chinos
(GLZ/Zhipu, Qwen, DeepSeek, MiniMax, Kimi, etc.). Además, en el momento del
diseño eran los que ofrecían `$0` con más frecuencia. El desempate alfabético
hace el comportamiento reproducible y facilita comparar ejecuciones.

**Implementación:** dos señales combinadas.

1. Subcadena del proveedor en `"{id} {name}"` (lista `CHINESE_PROVIDERS`).
2. Regex con límite de palabra sobre el nombre (`CHINESE_KEYWORDS`), para
   evitar falsos positivos como `ling` dentro de `inkling`.

**Limitación:** es una heurística. Un proveedor nuevo no listado no se
prioriza; un nombre con `kimi` en un modelo no chino sí. Mantener la lista es
parte del trabajo.

### 2.6 Cooldown con castigo doble para 429

**Decisión:** `mark_failure` aplica `BACKOFF_SECONDS` (300 s) y, si el código es
`429`, `BACKOFF_SECONDS * 2` (600 s).

**Motivo:** un `429` significa que el proveedor está limitando tasa; insistir
cada pocos segundos solo empeora la situación y retrasa la respuesta. Los
errores de conexión o `5xx` suelen ser transitorios y merecen un castigo menor.

**Detalle:** el estado `failed` existe en el dataclass pero ningún camino lo
asigna. Se dejó preparado para una política futura de expulsión definitiva
(por ejemplo, tras N fallos consecutivos). Hoy todos los fallos son
recuperables por tiempo.

### 2.7 Conservar estado al refrescar el catálogo

**Decisión:** al fusionar el catálogo nuevo, reutilizar el objeto `FreeModel`
previo si el `id` coincide.

**Motivo:** si se reemplazaran los objetos, cada refresco borraría cooldowns y
contadores. Un modelo castigado volvería a intentarse de inmediato tras la
recarga, justo lo contrario del propósito del backoff. Los modelos nuevos se
crean con estado limpio; los desaparecidos se descartan.

### 2.8 `asyncio.to_thread` para el trabajo bloqueante

**Decisión:** el endpoint `async` delega en `_query_sync` mediante
`asyncio.to_thread`.

**Motivo:** el SDK de OpenAI y la iteración del stream son síncronos. Ejecutarlos
directamente en el event loop bloquearía el servidor durante toda la respuesta
(potencialmente decenas de segundos). El hilo del executor mantiene el loop
libre para healthchecks y otras peticiones.

**Coste:** cada consulta ocupa un hilo. Con muchas consultas simultáneas se
alcanza el límite del executor por defecto; no hay cola explícita ni límite de
concurrencia propio.

### 2.9 Estado en memoria, sin base de datos

**Decisión:** no persistir catálogo, cooldowns ni contadores.

**Motivo:** el catálogo es volátil y reconstruible. Una base de datos añadiría
dependencias, migraciones y estados corruptos posibles, sin beneficio para un
servicio doméstico. Reiniciar limpia cooldowns, lo cual es aceptable.

**Contrapartida:** con varios workers, cada proceso tiene su propio estado. Se
recomienda un solo worker.

---

## 3. Problemas encontrados y cómo se resolvieron

### 3.1 La ESP8266 no veía la red WiFi

**Síntoma:** la placa no conectaba y el monitor serie mostraba el mensaje de
fallo tras 20 segundos de intentos.

**Causa:** la red disponible era de **5 GHz**. El ESP8266 solo soporta 2.4 GHz
y no lista ni asocia redes de 5 GHz.

**Solución:** usar el SSID gemelo de 2.4 GHz, en la misma subred que la
notebook. El firmware documenta la restricción en un comentario y la guía del
cliente la repite.

**Lección:** antes de depurar código, verificar que la radio puede ver la red.
Un `WiFi.scanNetworks()` habría ahorrado tiempo.

### 3.2 El JSON no entraba en el buffer del microcontrolador

**Síntoma:** `No se pudo parsear JSON` con respuestas largas.

**Causa:** `DynamicJsonDocument(8192)` reserva 8 KB. Una respuesta extensa más
el JSON de envelope puede superarlo, sobre todo con texto en UTF-8.

**Solución inmediata:** mantener el buffer en 8 KB y aceptar respuestas
moderadas. **Mejora pendiente:** usar `JsonDocument` con capacidad calculada
(ArduinoJson 7) o filtrar la respuesta en el servidor (por ejemplo, un parámetro
`max_chars`).

**Lección:** el tamaño de la respuesta es parte del contrato cuando el cliente
tiene RAM limitada. No basta con que el JSON sea válido.

### 3.3 Codificación de la pregunta en la URL

**Síntoma:** preguntas con espacios, acentos o `&` rompían la petición.

**Causa:** la query string debe ir percent-encoded y el cliente no traía una
función lista.

**Solución:** `urlencode()` propio en el sketch, que deja intactos
`A-Z a-z 0-9 - _ . ~` y codifica el resto en hexadecimal. Es suficiente para
texto UTF-8, aunque codifica byte a byte (correcto para UTF-8) y no distingue
caracteres multibyte.

**Lección:** en un microcontrolador, la biblioteca estándar de URL no está
disponible; hay que implementar y probar el encoding con acentos y símbolos.

### 3.4 El servicio quedaba en segundo plano

**Síntoma:** tras probar un lanzador de escritorio tipo «dock toggle», no estaba
claro si el servicio seguía corriendo ni cómo detenerlo.

**Solución:** el commit `16ebae5` eliminó el lanzador y dejó `run.sh` en primer
plano. `Ctrl+C` detiene el proceso y la terminal muestra los logs en vivo.

**Decisión descartada:** lanzador de dock con estado on/off. Añadía un archivo
PID, lógica de idempotencia y una forma de matar el proceso; para un servicio
de desarrollo era más superficie de error que beneficio. Se prefiere un proceso
en primer plano, visible y trivial de matar.

### 3.5 Key ausente detectada tarde

**Síntoma:** el servicio arrancaba, pero todas las consultas fallaban con 401.

**Solución:** `run.sh` valida que `.env` contenga
`OPENROUTER_API_KEY=sk-or-v1-...` antes de arrancar y aborta con un mensaje
claro. En `server.py` se emite un `WARNING` al importar si la clave está vacía
o sigue siendo el marcador.

**Limitación:** la validación es sintáctica (prefijo), no verifica que la clave
sea válida contra OpenRouter. Un 401 en la primera consulta sigue siendo
posible.

### 3.6 Failover que no cubre todos los errores

**Hallazgo (revisión de documentación, noviembre de 2026):**
`OpenRouterClient.chat_stream` es un generador. Al invocarlo no se ejecuta su
cuerpo; la petición HTTP ocurre en el primer `next()`. `FreeModelRouter.stream`
captura excepciones alrededor de la **creación** del generador y marca éxito de
inmediato, pero la iteración real ocurre después, en `server._query_sync`.

**Efecto:** un `429` o un `APIStatusError` que se manifieste al abrir el stream
no dispara failover ni cooldown; se devuelve como `Error interno: ...`.

**Estado:** **no corregido** en esta fase (la documentación no modifica código).
Queda registrado como la deuda funcional más importante. La corrección pasa por
consumir el primer fragmento dentro del `try` antes de retornar, o por hacer
que el router sea un generador que gestione el cambio de modelo durante la
iteración.

**Lección:** «envolver en try/except» no equivale a «capturar el error» cuando
hay generadores perezosos de por medio. Hay que probar con un modelo que falle
al abrir el stream, no solo con uno que falle antes.

---

## 4. Enfoques descartados

| Enfoque | Por qué se descartó |
|---|---|
| Lista fija de modelos `:free` | Caducidad casi diaria; errores 404/402 silenciosos. |
| Usar el router comodín `openrouter/free` | No permite priorizar modelos chinos ni saber qué modelo respondió. |
| Streaming SSE hasta el ESP8266 | Parser incremental, reconexiones y RAM; beneficio bajo para preguntas cortas. |
| `POST` con JSON | Más cabeceras y cuerpo en el cliente; sin ventaja frente a `GET` para ≤2000 caracteres. |
| Base de datos para cooldowns | Persistencia innecesaria para estado volátil; más piezas que mantener. |
| Múltiples workers de Uvicorn | Estado duplicado: cooldowns incoherentes entre procesos. |
| Autenticación con usuarios/roles | Sobredimensionado para una LAN doméstica; un token estático bastaría si hiciera falta. |
| Lanzador de dock on/off | Complejidad de PID y ciclo de vida; se prefirió primer plano. |
| Exponer el proxy a Internet | Superficie de ataque y consumo de cuota ajeno; fuera de alcance. |
| Devolver la respuesta cruda de OpenRouter | Más bytes y estructura anidada para un parser de 8 KB. |

---

## 5. Deudas técnicas y riesgos abiertos

| Deuda | Impacto | Corrección sugerida |
|---|---|---|
| Failover no cubre errores al abrir el stream | Alto: un modelo caído puede tumbar la consulta | Consumir el primer chunk dentro del `try`; probar con 429 inicial |
| Credenciales WiFi/IP en el `.ino` | Alto si el repo es público | Mover a `secrets.h` ignorado; publicar `secrets.example.h` |
| `key` por query string | Medio: filtración en logs/historial | Cabecera dedicada o solo `.env` |
| Pool nuevo por request con `key` | Medio: latencia y rate del catálogo | Cachear pools por clave o eliminar el parámetro |
| Sin autenticación de entrada | Medio en redes compartidas | Token estático o bind a `127.0.0.1` |
| Sin tests automatizados | Medio: regresiones silenciosas | Tests con `httpx.MockTransport` y modelos falsos |
| Dependencias sin pin | Bajo/medio: builds no reproducibles | `requirements.lock` o versiones fijas |
| Comentarios con puerto 8000 | Bajo: confusión | Unificar a 8001 |
| `status="failed"` sin uso | Bajo: código muerto | Implementar expulsión o eliminar el estado |
| Sin endpoint de estado | Bajo: diagnóstico manual | Exponer `pool.to_payload()` en `/api/models` |

---

## 6. Lecciones aprendidas

1. **Los generadores perezosos cambian el alcance del `try/except`.** Cualquier
   failover sobre una API con streaming debe forzar el primer fragmento antes
   de considerar el intento exitoso.
2. **El «gratis» es un estado transitorio.** Un proxy de modelos `$0` debe
   diseñarse para que el catálogo cambie sin intervención y para que «no hay
   modelos» sea una respuesta normal, no una excepción fatal.
3. **El cliente manda.** Si el consumidor es un ESP8266, el contrato debe
   limitarse a bytes, RAM y timeouts reales: JSON plano, sin streaming, con
   campos fijos y longitudes acotadas.
4. **La red física es parte del sistema.** 5 GHz vs 2.4 GHz no es un detalle de
   configuración: es la diferencia entre funcionar y no funcionar.
5. **El estado en memoria es una decisión, no un olvido.** Elegir un worker y
   documentarlo evita «arreglos» con `--workers 4` que rompen los cooldowns.
6. **Un lanzador «cómodo» puede ocultar el ciclo de vida.** Para desarrollo, un
   proceso en primer plano con `Ctrl+C` es más honesto que un toggle.
7. **Las credenciales deben salir del código desde el primer commit.** Mover
   WiFi y claves a un archivo ignorado cuesta minutos; limpiar el historial de
   git cuesta mucho más.
8. **Documentar la deuda vale tanto como documentar lo que funciona.** La
   sección 3.6 existe para que nadie asuma un failover que el código aún no
   garantiza.

---

## 7. Cronología resumida

| Fecha | Hito |
|---|---|
| 2026-08-28 | Commit inicial: proxy FastAPI, pool de modelos `$0`, router con prioridad china, firmware ESP8266. |
| 2026-08-28 | Lanzador de dock tipo toggle para el servicio. |
| 2026-08-28 | Se elimina el lanzador y se adopta `run.sh` en primer plano. |
| 2026-11-12 | Documentación de portfolio: README, datasheet, guía de desarrollo, referencias, diagramas SVG y esta bitácora. |

---

## 8. Cómo usar esta bitácora

- Antes de cambiar el router, leer la sección 3.6.
- Antes de añadir un modelo o proveedor, revisar 2.5 y actualizar las listas.
- Antes de proponer workers o persistencia, leer 2.9 y la tabla de deudas.
- Al cerrar una deuda, añadir una entrada nueva con fecha y evidencia; no borrar
  la original, porque el historial explica por qué el código llegó hasta aquí.
