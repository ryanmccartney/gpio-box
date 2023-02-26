// NAME: index.ino
// AUTH: Ryan McCartney
// DATE: 24/02/2023
// DESC:

#include <WebServer_WT32_ETH01.h> // https://github.com/khoih-prog/WebServer_WT32_ETH01
#include <AsyncTCP.h>             // https://github.com/me-no-dev/AsyncTCP
#include <ESPAsyncWebServer.h>    // https://github.com/me-no-dev/ESPAsyncWebServer
#include <AsyncElegantOTA.h>      // https://github.com/ayushsharma82/AsyncElegantOTA
#include <WebSocketsClient.h>     // https://github.com/Links2004/arduinoWebSockets
#include <SocketIOclient.h>       // https://github.com/Links2004/arduinoWebSockets
#include <WebSerial.h>            // https://github.com/ayushsharma82/WebSerial
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson
#include <ESP32Ping.h>
#include "FS.h"
#include "SPIFFS.h"

#define DEBUG_ETHERNET_WEBSERVER_PORT Serial
#define _ETHERNET_WEBSERVER_LOGLEVEL_ 4 // Debug Level from 0 to 4
#define FORMAT_SPIFFS_IF_FAILED true
#define CONFIGSTORAGE_DEBUG true
#define CS_USE_SPIFFS true

/// Socket.IO Settings
const char *host = "192.168.0.112";
int port = 3000;
const char *path = "/socket.io/?EIO=4";

AsyncWebServer server(80);

WiFiClient ethClient;
SocketIOclient socketIO;

// Set LED GPIO
const int ledPin = 2;
// Stores LED state
String ledState;

// Replaces placeholder with LED state value
String processor(const String &var)
{
  if (var == "STATE")
  {
    if (digitalRead(ledPin))
    {
      ledState = "ON";
    }
    else
    {
      ledState = "OFF";
    }
    WebSerial.print(ledState);
    return ledState;
  }
  return String();
}

void handleNotFound(AsyncWebServerRequest *request)
{
  String method = (request->method() == HTTP_GET) ? "GET" : "POST";
  String data = "{";

  for (uint8_t i = 0; i < request->args(); i++)
  {
    data += "\"" + request->argName(i) + "\":\"" + request->arg(i) + "\"";
    if (i != request->args() - 1)
    {
      data += ",";
    }
  }
  data += "}";

  request->send(404, "application/json", "{\"state\":false,\"message\":\"Path not found on server.\",\"method\":\"" + method + "\",\"url\":\"" + request->url() + "\",\"data\":" + data + "}");
}

/* Message callback of WebSerial */
void recvMsg(uint8_t *data, size_t len)
{
  WebSerial.println("Received Data...");
  String d = "";
  for (int i = 0; i < len; i++)
  {
    d += char(data[i]);
  }
  WebSerial.println(d);
}

void hexdump(const void *mem, uint32_t len, uint8_t cols = 16)
{
  const uint8_t *src = (const uint8_t *)mem;
  Serial.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
  for (uint32_t i = 0; i < len; i++)
  {
    if (i % cols == 0)
    {
      Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
    }
    Serial.printf("%02X ", *src);
    src++;
  }
  Serial.printf("\n");
}

void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length)
{
  WebSerial.println(type);
  switch (type)
  {
  case sIOtype_DISCONNECT:
    WebSerial.println("[Socket.IO] Disconnected");
    break;
  case sIOtype_CONNECT:
    WebSerial.println("[Socket.IO] Connected to url");
    break;
  case sIOtype_EVENT:
  {
    char *sptr = NULL;
    int id = strtol((char *)payload, &sptr, 10);
    Serial.printf("[Socket.IO] get event: %s id: %d", payload, id);
    if (id)
    {
      payload = (uint8_t *)sptr;
    }
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error)
    {
      WebSerial.println(F("deserializeJson() failed: "));
      WebSerial.println(error.c_str());
      return;
    }

    String eventName = doc[0];
    Serial.printf("[Socket.IO] event name: %s\n", eventName.c_str());

    // Message Includes a ID for a ACK (callback)
    if (id)
    {
      // creat JSON message for Socket.IO (ack)
      DynamicJsonDocument docOut(1024);
      JsonArray array = docOut.to<JsonArray>();

      // add payload (parameters) for the ack (callback function)
      JsonObject param1 = array.createNestedObject();
      param1["now"] = millis();

      // JSON to String (serializion)
      String output;
      output += id;
      serializeJson(docOut, output);

      // Send event
      socketIO.send(sIOtype_ACK, output);
    }
  }
  break;
  case sIOtype_ACK:
    Serial.printf("[Socket.IO] get ack: %u\n", length);
    break;
  case sIOtype_ERROR:
    Serial.printf("[Socket.IO] get error: %u\n", length);
    break;
  case sIOtype_BINARY_EVENT:
    Serial.printf("[Socket.IO] get binary: %u\n", length);
    break;
  case sIOtype_BINARY_ACK:
    Serial.printf("[Socket.IO] get binary ack: %u\n", length);
    break;
  }
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;

  // Initialize SPIFFS
  if (!SPIFFS.begin(true))
  {
    Serial.println("\nAn Error has occurred while mounting SPIFFS");
    return;
  }

  // To be called before ETH.begin()
  WT32_ETH01_onEvent();

  ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER);

  WT32_ETH01_waitForConnect();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/index.html", String(), false, processor); });

  // Route to load chota.min.css file
  server.on("/chota.min.css", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/chota.min.css", "text/css"); });

  server.on("/logo.svg", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/logo.svg", "image/svg+xml"); });

  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/favicon.ico", "image/x-icon"); });

  // Route to set GPIO to HIGH
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    digitalWrite(ledPin, HIGH);    
    request->send(SPIFFS, "/index.html", String(), false, processor); });

  // Route to set GPIO to LOW
  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    digitalWrite(ledPin, LOW);    
    request->send(SPIFFS, "/index.html", String(), false, processor); });

  // Reboot ESP32
  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    request->send(200, "application/json","{\"state\":true,\"message\":\"Rebooted GPIO Box\"}");
    ESP.restart(); });

  // Ping Address
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    bool success = Ping.ping(host, 3);
    if (success)
    {
      request->send(200, "application/json", "{\"state\":true,\"message\":\"Pinged central server\"}");
    }
    else
    {
      request->send(200, "application/json", "{\"state\":false,\"message\":\"Could not ping central server\"}");
    } });

  server.onNotFound(handleNotFound);

  // ElegantOTA is accessible at "<IP Address>/update" in browser
  AsyncElegantOTA.begin(&server);

  // WebSerial is accessible at "<IP Address>/webserial" in browser
  WebSerial.begin(&server);
  WebSerial.msgCallback(recvMsg);

  server.begin();

  socketIO.begin(host, port, path);

  // event handler
  socketIO.onEvent(socketIOEvent);
  socketIO.setReconnectInterval(5000);

  // Join default namespace
  socketIO.send(sIOtype_CONNECT, "/");

  // Print Some Details
  Serial.println("HTTP server started with MAC: " + ETH.macAddress() + ", at IPv4: " + ETH.localIP().toString());
  Serial.println("GPIO Box running on " + String(ARDUINO_BOARD) + "with " + String(SHIELD_TYPE) + " " + String(WEBSERVER_WT32_ETH01_VERSION));
}

unsigned long messageTimestamp = 0;
void loop()
{
  socketIO.loop();

  uint64_t now = millis();

  if (now - messageTimestamp > 2000)
  {
    messageTimestamp = now;

    // creat JSON message for Socket.IO (event)
    DynamicJsonDocument doc(1024);
    JsonArray array = doc.to<JsonArray>();

    // add evnet name
    // Hint: socket.on('event_name', ....
    array.add("state");

    // add payload (parameters) for the event
    JsonObject param1 = array.createNestedObject();
    param1["now"] = (uint32_t)now;

    // JSON to String (serializion)
    String output;
    serializeJson(doc, output);

    // Send event
    socketIO.sendEVENT(output);

    // Print JSON for debugging
    WebSerial.println(output);
  }
}