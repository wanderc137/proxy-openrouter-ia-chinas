"""Servidor proxy local de OpenRouter para modelos IA gratuitos (prioridad china).

Expone un único endpoint pensado para microcontroladores (ESP8266) y scripts:

  GET /api/query?q=<pregunta>[&key=sk-or-v1-...]

Devuelve JSON compacto (sin streaming):
  - Éxito: {"ok": true, "model": "<id>", "text": "<respuesta>", "error": null}
  - Falla: {"ok": false, "model": null, "error": "<detalle>"}

Reutiliza el pool de modelos gratuitos con failover y prioridad a modelos chinos.
La API key se toma de .env (OPENROUTER_API_KEY) o se puede pasar por parametro `key`.
"""

from __future__ import annotations

import asyncio
import logging
import os
from pathlib import Path

from dotenv import load_dotenv
from fastapi import FastAPI, Query
from fastapi.responses import JSONResponse

from src.models_pool import ModelsPool
from src.openrouter_client import OpenRouterClient
from src.router import FreeModelRouter, RouterError

load_dotenv()

logging.basicConfig(
    level=logging.DEBUG if os.getenv("DEBUG", "false").lower() == "true" else logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
logger = logging.getLogger("proxy_openrouter_ia")

API_KEY = os.getenv("OPENROUTER_API_KEY", "")
if not API_KEY or API_KEY.startswith("sk-or-v1-..."):
    logger.warning("OPENROUTER_API_KEY no configurada o vacía en .env")

pool = ModelsPool(API_KEY)
client = OpenRouterClient(API_KEY)
router = FreeModelRouter(pool, client)

app = FastAPI(title="Proxy OpenRouter IA Chinas", version="1.0.0")


@app.get("/")
async def root() -> dict:
    """Info mínima del servicio (útil como healthcheck)."""
    return {
        "name": "proxy-openrouter-ia-chinas",
        "version": "1.0.0",
        "uso": "/api/query?q=<pregunta>[&key=sk-or-v1-...]",
    }


def _query_sync(rtr: FreeModelRouter, question: str) -> dict:
    """Ejecuta la consulta completa (síncrona) y devuelve un dict serializable."""
    messages = [{"role": "user", "content": question}]
    try:
        model_id, stream = rtr.stream(messages)
        parts = []
        for fragment in stream:
            parts.append(fragment)
        return {"ok": True, "model": model_id, "text": "".join(parts), "error": None}
    except RouterError as exc:
        return {"ok": False, "model": None, "text": None, "error": str(exc)}
    except Exception as exc:  # noqa: BLE001
        logger.exception("Error en /api/query")
        return {"ok": False, "model": None, "text": None, "error": f"Error interno: {exc}"}


@app.get("/api/query")
async def query(
    q: str = Query(..., min_length=1, max_length=2000, description="Pregunta"),
    key: str = Query(None, description="API key de OpenRouter (usa .env si no viene)"),
) -> JSONResponse:
    """Respuesta JSON compacta para microcontroladores (ESP8266) y scripts."""
    active_router = router
    if key:
        local_pool = ModelsPool(key)
        local_client = OpenRouterClient(key)
        active_router = FreeModelRouter(local_pool, local_client)

    result = await asyncio.to_thread(_query_sync, active_router, q)
    return JSONResponse(result)


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=8001)
