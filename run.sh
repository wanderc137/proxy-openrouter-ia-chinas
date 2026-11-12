#!/usr/bin/env bash
# Ejecución MANUAL en PRIMER PLANO del proxy OpenRouter de IA (chinas).
#   - Arranca el servicio en el puerto 8001 (endpoint /api/query).
#   - Se ejecuta en la terminal actual (no en segundo plano).
#   - Para DETENERLO: pulsa Ctrl+C.
set -euo pipefail

APP_DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${PORT:-8001}"
URL="http://localhost:$PORT"

echo "[proxy] Preparando entorno..."
[ -d "$APP_DIR/.venv" ] || python3 -m venv "$APP_DIR/.venv"
# shellcheck disable=SC1091
"$APP_DIR/.venv/bin/python" -c "import uvicorn" >/dev/null 2>&1 || \
  "$APP_DIR/.venv/bin/pip" install -q -r "$APP_DIR/requirements.txt"

# Asegurar .env
[ -f "$APP_DIR/.env" ] || cp "$APP_DIR/.env.example" "$APP_DIR/.env"

if ! grep -qE '^OPENROUTER_API_KEY=sk-or-v1-.+' "$APP_DIR/.env" 2>/dev/null; then
  echo "ERROR: OPENROUTER_API_KEY no configurada en .env"
  exit 1
fi

# Cargar variables y ejecutar EN PRIMER PLANO
set -a; # shellcheck disable=SC1091
source "$APP_DIR/.env"; set +a

echo "[proxy] Proxy IA en $URL  (Ctrl+C para detener)"
echo "[proxy] Endpoint: ${URL}/api/query?q=tu+pregunta"
exec "$APP_DIR/.venv/bin/python" "$APP_DIR/server.py"
