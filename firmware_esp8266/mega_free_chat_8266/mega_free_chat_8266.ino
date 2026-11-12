/*
  Mega Free Chat - Cliente ESP8266 (NodeMCU v3)

  Hace preguntas a una IA (modelos gratuitos chinos de OpenRouter, priorizados)
  a través de la API local que corre en el notebook:

    GET http://192.0.2.10:8001/api/query?q=<pregunta>

  Respuesta JSON:
    {"ok":true, "model":"...", "text":"respuesta"}
    {"ok":false, "model":null, "error":"mensaje"}

  Uso:
    1. El ESP8266 se conecta a la red WiFi "TU_RED_2.4GHZ".
    2. Abre el monitor serie a 115200 baudios.
    3. Escribe una pregunta y pulsa ENTER.
    4. La respuesta de la IA se imprime por el puerto serie.

  Configura:
    - SSID / PASSWORD de tu red.
    - SERVER_IP: la IP local del notebook donde corre la API (puerto 8001).
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// ---- Configuración de red ----
// La ESP8266 solo soporta WiFi 2.4 GHz; por eso usamos la red "... 2.4 Ghz"
// (NO la "... 5G", que es de 5 GHz y la placa no puede ver).
const char* WIFI_SSID = "TU_RED_2.4GHZ";
const char* WIFI_PASS = "TU_PASSWORD_WIFI";

// ---- Servidor (notebook con la API en el puerto 8001) ----
const char* SERVER_IP = "192.0.2.10";
const uint16_t SERVER_PORT = 8001;
const char* API_PATH = "/api/query";

// El monitor serie debe abrirse a 115200 baudios
#define SERIAL_BAUD 115200

// Buffer para la pregunta leída del terminal
#define BUFFER_SIZE 256
char questionBuf[BUFFER_SIZE];
int bufIndex = 0;

// Última vez que se reinició la conexión (watchdog básico)
unsigned long lastReconnect = 0;

// ---------------------------------------------------------------------
// URL-encode simple (solo lo necesario para texto normal)
// ---------------------------------------------------------------------
String urlencode(const String &s) {
  const char hex[] = "0123456789ABCDEF";
  String out;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if ((c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(uint8_t)(c >> 4) & 0x0F];
      out += hex[(uint8_t)c & 0x0F];
    }
  }
  return out;
}

// ---------------------------------------------------------------------
// Conectarse / reconectar WiFi
// ---------------------------------------------------------------------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println();
  Serial.print("[WiFi] Conectando a ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Esperar hasta 20 s en bloques de 500 ms
  for (int i = 0; i < 40; i++) {
    if (WiFi.status() == WL_CONNECTED) break;
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Conectado. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] No se pudo conectar.");
  }
}

// ---------------------------------------------------------------------
// Hacer la pregunta a la API y mostrar la respuesta
// ---------------------------------------------------------------------
void ask(const String &question) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[API] Sin conexión WiFi. Reintentando...");
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[API] No se pudo conectar; cancele el intento.");
      return;
    }
  }

  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + SERVER_IP + ":" + SERVER_PORT +
               API_PATH + "?q=" + urlencode(question);

  Serial.print("[API] Consultando: http://");
  Serial.print(SERVER_IP);
  Serial.print(":");
  Serial.println(SERVER_PORT);

  http.setTimeout(30000); // 30 s max
  http.begin(client, url);

  int httpCode = http.GET();

  if (httpCode > 0) {
    Serial.print("[API] HTTP: ");
    Serial.println(httpCode);

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();

      // Parsear JSON  {"ok":..,"model":..,"text":..,"error":..}
      DynamicJsonDocument doc(8192);
      DeserializationError err = deserializeJson(doc, payload);
      if (err) {
        Serial.println("[API] No se pudo parsear JSON.");
        Serial.print("      Payload (primeros 200): ");
        Serial.println(payload.substring(0, 200));
      } else {
        bool ok = doc["ok"] | false;
        const char* model = doc["model"] | "";
        const char* text = doc["text"] | "";
        const char* error = doc["error"] | "";

        Serial.println();
        Serial.println("========================================");
        if (ok) {
          Serial.print("Modelo: ");
          Serial.println(model);
          Serial.println("----------------------------------------");
          Serial.println(text);
        } else {
          Serial.print("[API] Error: ");
          Serial.println(error);
        }
        Serial.println("========================================");
      }
    } else {
      Serial.print("[API] Error HTTP: ");
      Serial.println(httpCode);
      Serial.println(http.getString().substring(0, 200));
    }
  } else {
    Serial.print("[API] Falla de conexión, error: ");
    Serial.println(http.errorToString(httpCode).c_str());
  }

  http.end();
}

// ---------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println("=== Mega Free Chat - ESP8266 ===");
  Serial.println("Conectando a la red y listo para preguntas.");
  Serial.println("Escribe una pregunta y pulsa ENTER.");

  connectWiFi();
  bufIndex = 0;
  memset(questionBuf, 0, sizeof(questionBuf));
}

// ---------------------------------------------------------------------
// Loop: leer pregunta por serial y consultar la API
// ---------------------------------------------------------------------
void loop() {
  // Reconexión periódica si WiFi se cae
  if (WiFi.status() != WL_CONNECTED && millis() - lastReconnect > 15000) {
    lastReconnect = millis();
    connectWiFi();
  }

  // Leer caracteres del monitor serie hasta ENTER ('\n')
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n') {
      if (bufIndex > 0) {
        questionBuf[bufIndex] = '\0'; // terminar cadena
        String q = String(questionBuf);
        q.trim();
        Serial.print("Pregunta: ");
        Serial.println(q);
        ask(q);
      }
      bufIndex = 0;
      memset(questionBuf, 0, sizeof(questionBuf));
    } else if (c == '\r') {
      // ignorar retorno de carro
    } else if (bufIndex < BUFFER_SIZE - 1) {
      questionBuf[bufIndex++] = c;
    }
  }
}
