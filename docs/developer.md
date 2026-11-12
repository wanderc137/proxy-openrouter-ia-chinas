# Guía de desarrollo

Documento técnico para quien vaya a leer, modificar o extender el proxy. Explica
la arquitectura real del código (no la ideal), las firmas de cada módulo, el
estado compartido y las decisiones que condicionan cualquier cambio.

---

## 1. Vista general

El proyecto es un traductor pequeño entre dos mundos:

- **Mundo A:** HTTP simple, sin streaming, con URLs cortas y JSON diminuto.
  Es el que entiende un ESP8266.
- **Mundo B:** API compatible con OpenAI, con streaming SSE, catálogo dinámico,
  rate limits y errores transitorios. Es el que ofrece OpenRouter.

El proxy no implementa inferencia: elige un modelo gratuito, reenvía los
mensajes y acumula los fragmentos hasta tener una respuesta completa. Todo el
trabajo pesado ocurre del lado de OpenRouter.

```
cliente HTTP (ESP8266, curl, script)
        │  GET /api/query?q=...
        ▼
server.py  ──►  FreeModelRouter  ──►  OpenRouterClient  ──►  openrouter.ai
                    │
                    └──►  ModelsPool (catálogo $0 + cooldown)
```

No hay persistencia: ni base de datos, ni archivos de estado, ni caché en
disco. El estado del pool vive en memoria del proceso y se pierde al
reiniciar. Esto es deliberado: el catálogo de modelos gratuitos cambia y no
tiene sentido congelarlo en disco.

---

## 2. Estructura de archivos

| Ruta | Responsabilidad | Líneas aprox. |
|---|---|---|
| `server.py` | Aplicación FastAPI, validación de entrada, orquestación por request y serialización JSON. | ~94 |
| `src/__init__.py` | Paquete Python; sin lógica. | 0 |
| `src/models_pool.py` | Descarga del catálogo, filtro de modelos `$0`, clasificación china, estado y cooldown. | ~219 |
| `src/openrouter_client.py` | Cliente OpenAI-compatible; una llamada por modelo; generador de fragmentos. | ~41 |
| `src/router.py` | Orden de candidatos, intentos, clasificación de errores y `RouterError`. | ~73 |
| `firmware_esp8266/mega_free_chat_8266/mega_free_chat_8266.ino` | Cliente de referencia: WiFi + HTTP + parseo JSON + consola serie. | ~224 |
| `run.sh` | Arranque manual en primer plano: crea venv, instala deps, valida key. | ~32 |
| `requirements.txt` | Dependencias sin pin de versión. | 5 |

---

## 3. Módulos y firmas

### 3.1 `src/models_pool.py`

Constantes de configuración leídas **al importar** el módulo:

```python
BASE_URL = os.getenv("OR_BASE_URL", "https://openrouter.ai/api/v1")
REFRESH_SECONDS = int(os.getenv("POOL_REFRESH_SECONDS", "21600"))
BACKOFF_SECONDS = int(os.getenv("BACKOFF_SECONDS", "300"))
```

Consecuencia: cambiar estas variables después de arrancar el proceso no tiene
efecto. Deben estar definidas antes de `import src.models_pool` (o del arranque
de `server.py`).

#### Funciones de clasificación

```python
def is_chinese_model(model_id: str, name: str = "") -> bool
```

Devuelve `True` si el identificador o el nombre contienen algún proveedor de
`CHINESE_PROVIDERS` (subcadena, en minúsculas) o si el **nombre** coincide con
uno de los `CHINESE_KEYWORDS` (regex con límites de palabra). Los prefijos
incluyen `z-ai`, `zhipu`, `minimax`, `deepseek`, `qwen`, `alibaba`,
`inclusionai`, `moonshot`, `kimi`, `baidu`, `tencent`, `hunyuan`, `stepfun`,
`01-ai`, `iflytek`, `sensetime`, `doubao`, `baichuan`, `internlm`.

Detalle importante: la comprobación por subcadena se aplica al conjunto
`"{model_id} {name}"`, mientras que las keywords con `\b` solo se aplican al
nombre. Esto evita falsos positivos como `ling` dentro de `inkling`, pero un
identificador que contenga `kimi` en cualquier posición se clasifica como
chino aunque el modelo no lo sea. Es una heurística, no una taxonomía.

```python
def is_chat_model(model_id: str, name: str) -> bool
```

Excluye:

- identificadores que empiezan por `~` (alias flexibles de OpenRouter);
- el id exacto `openrouter/free` (router comodín, no un modelo concreto);
- cualquier modelo cuyo id o nombre contenga `lyria` (generación de audio).

No valida modalidad ni `supported_parameters`; solo descarta lo que rompería
un chat de texto.

#### Modelo de datos

```python
@dataclass
class FreeModel:
    id: str
    name: str
    context_length: int = 0
    modalities: dict = field(default_factory=dict)
    supported_parameters: list = field(default_factory=list)
    is_chinese: bool = False
    status: str = "active"          # active | backoff | failed
    last_failure: str | None = None
    fail_count: int = 0
    cooldown_until: float = 0.0

    def is_available(self, now: float) -> bool
    def mark_success(self) -> None
    def mark_failure(self, error: str, status_code: int | None = None) -> None
    def to_dict(self) -> dict
```

`mark_failure` calcula el cooldown así:

- error con `status_code == 429` → `BACKOFF_SECONDS * 2` (600 s por defecto);
- cualquier otro error → `BACKOFF_SECONDS` (300 s por defecto).

Siempre incrementa `fail_count` y deja `status = "backoff"`. `mark_success`
resetea contador, error y cooldown. El estado `failed` existe en el tipo pero
**ningún camino del código lo asigna**; se conserva por compatibilidad y para
futuras políticas de expulsión definitiva.

#### Pool

```python
class ModelsPool:
    def __init__(self, api_key: str) -> None
    def _fetch_models(self) -> list[FreeModel]
    def refresh(self, force: bool = False) -> None
    def available_models(self) -> list[FreeModel]
    def all_models(self) -> list[FreeModel]
    def mark_success(self, model_id: str) -> None
    def mark_failure(self, model_id: str, error: str, status_code: int | None) -> None
    def to_payload(self) -> dict
```

Comportamiento relevante:

1. `_fetch_models()` hace `GET {BASE_URL}/models` con `httpx.Client(timeout=30)`
   y cabecera `Authorization: Bearer <api_key>`. Filtra `pricing.prompt == "0"`
   **y** `pricing.completion == "0"` (comparación de cadenas).
2. `refresh()` es *throttled*: si `time.time() - _last_fetch < REFRESH_SECONDS`
   y `force=False`, retorna sin hacer red. El chequeo se hace con el lock
   tomado, pero la descarga ocurre **fuera** del lock; dos hilos podrían
   descargar el catálogo a la vez en el primer acceso concurrente. Es
   aceptable porque el resultado es idempotente.
3. Al fusionar, conserva el objeto `FreeModel` previo si el `id` ya existía.
   Así no se pierden cooldowns ni contadores cuando cambia el catálogo.
4. Si la descarga falla, guarda `_last_error` y mantiene el pool anterior. Un
   fallo de red **no vacía** el pool.
5. `available_models()` llama a `refresh()` y devuelve solo los modelos cuyo
   `cooldown_until` ya pasó.

`to_payload()` existe para una futura UI de estado; hoy no está expuesto por
ningún endpoint.

### 3.2 `src/openrouter_client.py`

```python
BASE_URL = os.getenv("OR_BASE_URL", "https://openrouter.ai/api/v1")
APP_NAME = "mega-free-chat"

class OpenRouterClient:
    def __init__(self, api_key: str) -> None
    def chat_stream(self, model_id: str, messages: list[dict])
```

El constructor crea `openai.OpenAI(api_key=..., base_url=BASE_URL,
default_headers={"HTTP-Referer": "http://localhost:8000", "X-Title":
"mega-free-chat"})`.

`chat_stream` es un **generador**: llama a
`client.chat.completions.create(model=model_id, messages=messages,
stream=True)` y produce únicamente `chunk.choices[0].delta.content` cuando no
es nulo. No captura excepciones; esa responsabilidad es del router (en teoría,
ver limitación 8.1).

### 3.3 `src/router.py`

```python
class RouterError(Exception): ...

class FreeModelRouter:
    def __init__(self, pool: ModelsPool, client: OpenRouterClient) -> None
    def stream(self, messages: list[dict])
```

`stream` devuelve una tupla `(model_id: str, stream: generator)`. Pasos:

1. `available = pool.available_models()`. Si está vacío → `RouterError`.
2. Ordena con `key=lambda m: (not m.is_chinese, m.name.lower())`. Los chinos
   van primero; dentro de cada grupo, orden alfabético por nombre para que el
   comportamiento sea reproducible entre ejecuciones.
3. Para cada candidato: crea el generador, marca éxito y retorna.
4. Captura `RateLimitError`, `APIStatusError`, `APIConnectionError` y
   `Exception` genérica; en todos los casos marca fallo con su código y
   acumula un texto de intento.
5. Si termina el bucle → `RouterError("Todos los modelos gratuitos fallaron.
   Detalle: ...")`.

La intención del bloque `try` es hacer failover cuando un modelo está muerto.
La implementación tiene un matiz importante que se documenta en la sección 8.

### 3.4 `server.py`

```python
API_KEY = os.getenv("OPENROUTER_API_KEY", "")
pool = ModelsPool(API_KEY)
client = OpenRouterClient(API_KEY)
router = FreeModelRouter(pool, client)
app = FastAPI(title="Proxy OpenRouter IA Chinas", version="1.0.0")

@app.get("/")
async def root() -> dict

def _query_sync(rtr: FreeModelRouter, question: str) -> dict

@app.get("/api/query")
async def query(
    q: str = Query(..., min_length=1, max_length=2000),
    key: str = Query(None),
) -> JSONResponse
```

Flujo por petición:

1. FastAPI valida `q` (1–2000 caracteres) y `key` (opcional).
2. Si llega `key`, se construyen `ModelsPool`, `OpenRouterClient` y
   `FreeModelRouter` **nuevos para esa petición**. Esto significa que ese
   camino descarga el catálogo en cada llamada (no comparte `_last_fetch`).
3. `asyncio.to_thread(_query_sync, active_router, q)` mueve el trabajo
   bloqueante (SDK síncrono + iteración del stream) a un hilo del pool.
4. `_query_sync` arma `[{"role": "user", "content": question}]`, itera el
   generador y concatena los fragmentos.
5. Devuelve `JSONResponse` con una de estas dos formas:

```json
{"ok": true,  "model": "<id>", "text": "<respuesta>", "error": null}
{"ok": false, "model": null,   "text": null,        "error": "<detalle>"}
```

`RouterError` se traduce a `ok: false` con el detalle de los intentos. Cualquier
otra excepción se registra con `logger.exception` y se devuelve como
`"Error interno: <mensaje>"`.

El proceso se arranca con `uvicorn.run(app, host="0.0.0.0", port=8001)`, pero
también funciona `uvicorn server:app --host 0.0.0.0 --port 8001`. `run.sh`
invoca directamente `python server.py`.

---

## 4. Diagramas de secuencia

### 4.1 Consulta exitosa

```
Cliente            server.py           Router            Pool             OpenRouter
  │ GET /api/query    │                   │                │                  │
  ├──────────────────►│                   │                │                  │
  │                   ├─ to_thread ──────►│                │                  │
  │                   │                   ├─ available ───►│                  │
  │                   │                   │                ├─ GET /models ───►│
  │                   │                   │                │◄─ catálogo ──────┤
  │                   │                   │◄─ [FreeModel] ─┤                  │
  │                   │                   ├─ chat_stream ──┼─────────────────►│
  │                   │                   │◄─ (model_id, ──┼── generador) ────┤
  │                   │◄─ (model_id, gen) ┤                │                  │
  │                   ├─ for chunk ◄──────┼────────────────┼──────────────────┤
  │                   ├─ join(chunks)     │                │                  │
  │◄─ JSON compacto ──┤                   │                │                  │
```

### 4.2 Failover con 429

```
Router                        Pool                    OpenRouter
  ├─ intenta modelo A ─────────────────────────────────► 429
  ├─ mark_failure(A, 429) ────► cooldown 600 s
  ├─ intenta modelo B ─────────────────────────────────► 200 + stream
  └─ mark_success(B) ─────────► reset estado
```

Si todos fallan, el cliente recibe `ok: false` con la lista de intentos
separada por ` | `.

---

## 5. Estado, concurrencia y memoria

- **Un lock por pool.** Protege `_models`, `_last_fetch` y `_last_error`.
  `mark_success`/`mark_failure` recorren la lista linealmente; con decenas de
  modelos el coste es despreciable.
- **Pool global por proceso.** `server.py` crea uno solo al importar. Todos los
  requests sin `key` comparten catálogo y cooldowns.
- **Pools efímeros con `key`.** Cada request con clave crea su propio pool; no
  hay caché ni reutilización. Es cómodo para pruebas, costoso en producción.
- **Sin persistencia.** Reiniciar el proceso borra cooldowns y contadores.
- **Un solo worker recomendado.** Con `--workers N`, cada proceso tendría su
  propio pool y sus propios cooldowns; el estado dejaría de ser coherente.
- **Hilos.** `asyncio.to_thread` usa el executor por defecto de asyncio. Una
  consulta lenta ocupa un hilo durante toda la respuesta; con muchas consultas
  simultáneas el límite práctico es el tamaño del executor.

---

## 6. Cómo extender

### 6.1 Añadir un endpoint de estado del pool

`ModelsPool.to_payload()` ya devuelve todo lo necesario. Basta exponer:

```python
@app.get("/api/models")
async def models() -> JSONResponse:
    return JSONResponse(pool.to_payload())
```

No requiere cambios en el pool. Si se quiere evitar que `to_payload()` dispare
una descarga, usar `all_models()` con el throttle actual o añadir un parámetro
`refresh=False`.

### 6.2 Soportar conversaciones multi-turno

`FreeModelRouter.stream` acepta `list[dict]` con el formato de OpenAI, pero
`/api/query` siempre construye un único mensaje de usuario. Para multi-turno:

1. Añadir un parámetro `history` (JSON serializado) o un `POST` con cuerpo.
2. Validar tamaño y roles permitidos (`user`, `assistant`; nunca `system`
   desde un cliente no confiable).
3. Mantener el límite de contexto: `FreeModel.context_length` está disponible.

No conviene hacerlo por `GET`: la URL tiene límites prácticos y los historiales
crecen rápido.

### 6.3 Añadir autenticación local

El endpoint no tiene auth. Para una red compartida, un token estático por
cabecera es suficiente:

```python
from fastapi import Header, HTTPException

def require_token(x_api_token: str = Header(...)) -> None:
    if x_api_token != os.environ["PROXY_TOKEN"]:
        raise HTTPException(status_code=401)
```

Y declararlo en el endpoint (`dependencies=[Depends(require_token)]`). Para el
ESP8266 habría que añadir la cabecera con `http.addHeader(...)`.

### 6.4 Cambiar la política de prioridad

El orden se decide en una sola línea de `router.py`. Alternativas:

- **Más contexto primero:** `key=lambda m: (not m.is_chinese, -m.context_length)`.
- **Aleatorio dentro del grupo:** `random.shuffle` tras filtrar; reduce la
  presión sobre el primer modelo, a costa de reproducibilidad.
- **Por tasa de éxito histórica:** ya existe `fail_count`, pero se resetea con
  cada éxito; haría falta un contador acumulado aparte.

### 6.5 Añadir un proveedor distinto

`OpenRouterClient` es un envoltorio fino del SDK. Para otro proveedor
compatible con OpenAI, basta cambiar `base_url` y el modelo; para uno con API
propia, implementar un método `chat_stream(model_id, messages)` con la misma
firma y pasarlo al router. El resto del sistema no depende de OpenRouter salvo
por el catálogo (`ModelsPool`), que sí es específico de su formato.

### 6.6 Ajustar timeouts

- Catálogo: `httpx.Client(timeout=30)` en `_fetch_models`.
- Firmware: `http.setTimeout(30000)` en el sketch.
- OpenRouter: el SDK usa sus defaults; se puede pasar `timeout=` en
  `OpenAI(...)` si se necesita un límite explícito.
- Uvicorn: `--timeout-keep-alive` no afecta la duración de la respuesta.

---

## 7. Pruebas manuales sugeridas

No hay suite automatizada. Para validar un cambio:

```bash
# 1. Catálogo (requiere key válida para el header, aunque el endpoint es público)
curl -s -H "Authorization: Bearer $OPENROUTER_API_KEY" \
  https://openrouter.ai/api/v1/models | head -c 300

# 2. Arranque
python server.py

# 3. Healthcheck
curl -s http://localhost:8001/

# 4. Consulta nominal
curl -s "http://localhost:8001/api/query?q=hola"

# 5. Validación de longitud (debe dar 422)
python - <<'PY'
import urllib.request
try:
    urllib.request.urlopen("http://localhost:8001/api/query?q=" + "a"*2001)
except Exception as e:
    print(e)
PY

# 6. Key explícita (crea pool efímero)
curl -s "http://localhost:8001/api/query?q=hola&key=sk-or-v1-..."
```

Para observar el failover: poner una key inválida y comprobar que la respuesta
es `ok: false` con el detalle; o revisar los logs `WARNING` de
`mega_free_chat.router` cuando un modelo devuelve 429.

---

## 8. Limitaciones conocidas del diseño

### 8.1 El failover no cubre errores durante la iteración

`OpenRouterClient.chat_stream` es una función generadora. Al invocarla, su
cuerpo no se ejecuta: la petición HTTP real ocurre en el primer `next()`.
`FreeModelRouter.stream` envuelve en `try/except` la **creación** del generador
y llama a `mark_success` inmediatamente, pero la iteración ocurre después, en
`server._query_sync`. Por tanto:

- un `429` o un `APIStatusError` que se manifieste al abrir el stream **no**
  dispara el failover ni el cooldown; se propaga como `"Error interno: ..."`;
- el comentario «Verificar que realmente arranca» describe la intención, no el
  comportamiento efectivo.

Arreglarlo requiere consumir el primer fragmento dentro del `try` antes de
retornar, o convertir el router en un generador que gestione el failover
mientras itera. Es el cambio de mayor impacto pendiente y debe ir acompañado
de pruebas con modelos que devuelvan 429 al inicio del stream.

### 8.2 `key` por query string

La clave viaja en la URL: queda en logs de acceso, historial y proxies
intermedios. Está pensada para pruebas puntuales. En producción, usar solo
`.env` o una cabecera dedicada.

### 8.3 Catálogo recargado por request con `key`

Cada llamada con `key` descarga `/models` (hasta 30 s de timeout) antes de
consultar. En un bucle de pruebas esto multiplica la latencia y el consumo de
rate del endpoint de catálogo.

### 8.4 Sin autenticación ni TLS

El servicio escucha en `0.0.0.0:8001` en HTTP plano. Cualquiera en la misma
red puede usarlo y consumir la cuota de la key configurada. El diseño asume
red doméstica de confianza.

### 8.5 Estado no compartido entre workers

Cooldowns y catálogo viven en memoria. `uvicorn --workers 2` crea dos estados
independientes; un modelo en cooldown en un worker puede seguir intentándose en
el otro.

### 8.6 Respuesta no streaming hacia el cliente

El proxy acumula todos los fragmentos y responde de una vez. Es intencional
(JSON simple para el ESP8266), pero implica que el cliente espera la respuesta
completa y que el proxy retiene la respuesta en memoria. Una respuesta de
decenas de miles de caracteres ocupa RAM proporcional.

### 8.7 Clasificación china heurística

Los proveedores se detectan por subcadena. Un modelo nuevo de un proveedor no
listado no se prioriza; uno con un nombre que contenga, por ejemplo, `kimi`,
se prioriza aunque no sea chino. Actualizar `CHINESE_PROVIDERS` es parte del
mantenimiento.

### 8.8 Credenciales del firmware en el fuente

El sketch de referencia declara `WIFI_SSID`, `WIFI_PASS` y `SERVER_IP` como
constantes en el propio `.ino`. Para un repositorio público eso es deuda
técnica: conviene moverlas a un `secrets.h` local ignorado por git y dejar un
`secrets.example.h`. Esta documentación no reproduce esos valores.

---

## 9. Convenciones para contribuir

- Python 3.9+; el código usa `from __future__ import annotations` y tipos
  modernos (`list[dict]`, `str | None`).
- No añadir dependencias sin justificar: el valor del proyecto es su tamaño.
- Mantener las respuestas de `/api/query` con las cuatro claves exactas
  (`ok`, `model`, `text`, `error`) y los mismos tipos. El firmware depende de
  ellas.
- Cualquier cambio en el router debe probarse contra un modelo que falle al
  abrir el stream, no solo contra el camino feliz.
- No commitear `.env`, logs, `__pycache__` ni binarios. `.gitignore` ya cubre
  esos casos.
- Documentar en `BITACORA.md` las decisiones que cambien el comportamiento
  observable (nuevo estado, nuevo endpoint, nueva política de cooldown).
