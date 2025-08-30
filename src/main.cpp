#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <stdlib.h>
#include "UbidotsEsp32Mqtt.h"
#include "data.h"
#include "Settings.h"

#define TFT_GREY 0x8410
#define BUTTON_LEFT 0        // btn activo en bajo
#define LONG_PRESS_TIME 3000 // 3000 milis = 3s

TFT_eSPI tft = TFT_eSPI();
DHT dht(27, DHT11);
const char *UBIDOTS_TOKEN = "BBUS-bDpgJMre2jJSccBgvaifAnimVBmvfn";
const char *PUBLISH_DEVICE_LABEL = "esp32"; // Put here your Device label to which data  will be published
const char *PUBLISH_VARIABLE_LABEL1 = "humedad";
const char *PUBLISH_VARIABLE_LABEL2 = "temperatura"; // Put here your Variable label to which data  will be published
const char *SUBSCRIBE_DEVICE_LABEL = "esp32";        // Replace with the device label to subscribe to
const char *SUBSCRIBE_VARIABLE_LABEL1 = "sw1";
const char *SUBSCRIBE_VARIABLE_LABEL2 = "sw2";
const int PUBLISH_FREQUENCY = 5000;

unsigned long timer;

Ubidots ubidots(UBIDOTS_TOKEN);

WebServer server(80);

Settings settings;
int lastState = LOW; // para el btn
int currentState;    // the current reading from the input pin
unsigned long pressedTime = 0;
unsigned long releasedTime = 0;

void drop(int x, int y);
void callback(char *topic, byte *payload, unsigned int length);
void load404();
void loadIndex();
void loadFunctionsJS();
void restartESP();
void saveSettings();
bool is_STA_mode();
void AP_mode_onRst();
void STA_mode_onRst();
void detect_long_press();

#define TX 80  // X termómetro
#define TY 80  // Y termómetro
#define TW 18  // ancho tubo
#define TH 120 // alto tubo
#define BR 12  // radio bulbo
#define BX 25  // barra humedad X
#define BY 40  // barra humedad Y
#define BW 85  // ancho barra
#define BH 10  // alto barra
int a = 0;
// Rutina para iniciar en modo AP (Access Point) "Servidor"
void startAP()
{
  WiFi.disconnect();
  delay(19);
  Serial.println("Starting WiFi Access Point (AP)");
  WiFi.softAP("fabio_AP", "facil123");
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
}

// Rutina para iniciar en modo STA (Station) "Cliente"
void start_STA_client()
{
  WiFi.softAPdisconnect(true);
  WiFi.disconnect();
  delay(100);
  Serial.println("Starting WiFi Station Mode");
  WiFi.begin((const char *)settings.ssid.c_str(), (const char *)settings.password.c_str());
  WiFi.mode(WIFI_STA);

  int cnt = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    // Serial.print(".");
    if (cnt == 100) // Si después de 100 intentos no se conecta, vuelve a modo AP
      AP_mode_onRst();
    cnt++;
    Serial.println("attempt # " + (String)cnt);
  }

  WiFi.setAutoReconnect(true);
  Serial.println(F("WiFi connected"));
  Serial.println(F("IP address: "));
  Serial.println(WiFi.localIP());
  pressedTime = millis();
  // Rutinas de Ubidots
  ubidots.setCallback(callback);
  ubidots.setup();
  ubidots.reconnect();
  ubidots.subscribeLastValue(SUBSCRIBE_DEVICE_LABEL, SUBSCRIBE_VARIABLE_LABEL1);
  ubidots.subscribeLastValue(SUBSCRIBE_DEVICE_LABEL, SUBSCRIBE_VARIABLE_LABEL2);
  timer = millis();
}

void setup()
{

  Serial.begin(115200);
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_SKYBLUE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(TFT_BLACK, TFT_SKYBLUE);
  tft.drawString("Esperando WiFi", 67, 120);
  dht.begin();
  delay(2000);

  EEPROM.begin(4096);                 // Se inicializa la EEPROM con su tamaño max 4KB
  pinMode(BUTTON_LEFT, INPUT_PULLUP); // btn activo en bajo

  // settings.reset();
  settings.load(); // se carga SSID y PWD guardados en EEPROM
  settings.info(); // ... y se visualizan

  Serial.println("");
  Serial.println("starting...");

  if (is_STA_mode())
  {
    start_STA_client();
  }
  else // Modo Access Point & WebServer
  {
    startAP();

    /* ========== Modo Web Server ========== */

    /* HTML sites */
    server.onNotFound(load404);

    server.on("/", loadIndex);
    server.on("/index.html", loadIndex);
    server.on("/functions.js", loadFunctionsJS);

    /* JSON */
    server.on("/settingsSave.json", saveSettings);
    server.on("/restartESP.json", restartESP);

    server.begin();
    Serial.println("HTTP server started");
  }
}

void loop()
{
  if (is_STA_mode()) // Rutina para modo Station (cliente Ubidots)
  {
    float t = dht.readTemperature(), h = dht.readHumidity();

    if (a == 0)
    {
      a = 1;
      tft.fillScreen(TFT_BLACK);
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextFont(1);
      tft.setCursor(8, 100);
      tft.print("Temp:");
      tft.setCursor(8, 25);
      tft.print("Hume:");

      tft.drawRect(BX, BY, BW, BH, TFT_WHITE);
      for (int i = 0; i <= 5; i++)
      {
        int x = BX + BW * i / 5;
        tft.drawLine(x, BY + BH + 1, x, BY + BH + 4, TFT_WHITE);
        tft.setCursor(x - 5, BY + BH + 6);
        tft.printf("%d", i * 20);
      }
      drop(BX + BW + 12, BY + BH / 2);
      tft.drawRect(TX, TY, TW, TH, TFT_WHITE);
      tft.drawCircle(TX + TW / 2, TY + TH + 8, BR, TFT_WHITE);
      tft.drawCircle(40, 150, 15, TFT_WHITE);
      tft.drawCircle(40, 190, 15, TFT_WHITE);
      tft.fillCircle(40, 150, 14, TFT_GREY);
      tft.fillCircle(40, 190, 14, TFT_GREY);
      for (int i = 0; i <= 6; i++)
      {
        int y = TY + TH - i * TH / 6;
        tft.drawLine(TX + TW + 2, y, TX + TW + 8, y, TFT_WHITE);
        tft.setCursor(TX + TW + 10, y - 4);
        tft.printf("%d", 10 + i * 5);
      }
    }

    tft.fillRect(50, 10, 50, 10, TFT_BLACK);
    tft.setCursor(40, 100);
    tft.printf("%.1fC", t);
    tft.fillRect(50, 25, 50, 10, TFT_BLACK);
    tft.setCursor(40, 25);
    tft.printf("%.1f%%", h);

    tft.fillRect(BX + 1, BY + 1, BW - 2, BH - 2, TFT_BLACK);
    for (int i = 0; i < map(h, 0, 100, 0, BW); i++)
      tft.drawFastVLine(BX + i + 1, BY + 1, BH - 2, tft.color565(0, i * 255 / BW, 255 - i * 255 / BW));

    tft.fillRect(TX + 1, TY + 1, TW - 2, TH - 2, TFT_BLACK);
    int lv = map(t, 10, 40, 0, TH);
    for (int i = 0; i < lv; i++)
      tft.drawFastHLine(TX + 1, TY + TH - i, TW - 2, tft.color565(i * 255 / TH, 0, 255 - i * 255 / TH));
    tft.fillCircle(TX + TW / 2, TY + TH + 8, BR - 1, TFT_BLUE);
    // put your main code here, to run repeatedly:
    if (!ubidots.connected())
    {
      ubidots.reconnect();
      ubidots.subscribeLastValue(SUBSCRIBE_DEVICE_LABEL, SUBSCRIBE_VARIABLE_LABEL1);
      ubidots.subscribeLastValue(SUBSCRIBE_DEVICE_LABEL, SUBSCRIBE_VARIABLE_LABEL2); // Insert the device and variable's Labels, respectively
    }
    if ((millis() - timer) > PUBLISH_FREQUENCY) // triggers the routine every 5 seconds
    {
      if (isnan(h) || isnan(t))
      {
        Serial.println(F("Fallo al intentar leer información del DHT!"));
        return;
      }
      Serial.print(F("Humedad: "));
      Serial.print(h);
      Serial.print(F("%  Temperatura: "));
      Serial.print(t);
      Serial.println(F("°C "));
      ubidots.add(PUBLISH_VARIABLE_LABEL1, h);
      ubidots.add(PUBLISH_VARIABLE_LABEL2, t); // Insert your variable Labels and the value to be sent
      ubidots.publish(PUBLISH_DEVICE_LABEL);
      timer = millis();
    }
    ubidots.loop();
    delay(1000);
  }
  else // rutina para AP + WebServer
    server.handleClient();

  delay(10);
  detect_long_press();
}

// funciones para responder al cliente desde el webserver:
// load404(), loadIndex(), loadFunctionsJS(), restartESP(), saveSettings()

void load404()
{
  server.send(200, "text/html", data_get404());
}

void loadIndex()
{
  server.send(200, "text/html", data_getIndexHTML());
}

void loadFunctionsJS()
{
  server.send(200, "text/javascript", data_getFunctionsJS());
}

void restartESP()
{
  server.send(200, "text/json", "true");
  ESP.restart();
}

void saveSettings()
{
  if (server.hasArg("ssid"))
    settings.ssid = server.arg("ssid");
  if (server.hasArg("password"))
    settings.password = server.arg("password");

  settings.save();
  server.send(200, "text/json", "true");
  STA_mode_onRst();
}

// Rutina para verificar si ya se guardó SSID y PWD del cliente
// is_STA_mode retorna true si ya se guardaron
bool is_STA_mode()
{
  if (EEPROM.read(flagAdr))
    return true;
  else
    return false;
}

void AP_mode_onRst()
{
  EEPROM.write(flagAdr, 0);
  EEPROM.commit();
  delay(100);
  ESP.restart();
}

void STA_mode_onRst()
{
  EEPROM.write(flagAdr, 1);
  EEPROM.commit();
  delay(100);
  ESP.restart();
}

void detect_long_press()
{
  // read the state of the switch/button:
  currentState = digitalRead(BUTTON_LEFT);

  if (lastState == HIGH && currentState == LOW) // button is pressed
    pressedTime = millis();
  else if (lastState == LOW && currentState == HIGH)
  { // button is released
    releasedTime = millis();

    // Serial.println("releasedtime" + (String)releasedTime);
    // Serial.println("pressedtime" + (String)pressedTime);

    long pressDuration = releasedTime - pressedTime;

    if (pressDuration > LONG_PRESS_TIME)
    {
      Serial.println("(Hard reset) returning to AP mode");
      delay(500);
      AP_mode_onRst();
    }
  }

  // save the the last state
  lastState = currentState;
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  if (((char)payload[0] == '1') && (strstr(topic, "sw1")))
  {
    tft.fillCircle(40, 150, 14, TFT_BLACK);
    tft.fillCircle(40, 150, 14, TFT_RED);
  }
  if (((char)payload[0] == '1') && (strstr(topic, "sw2")))
  {
    tft.fillCircle(40, 190, 14, TFT_BLACK);
    tft.fillCircle(40, 190, 14, TFT_GREEN);
  }
  if (((char)payload[0] == '0') && (strstr(topic, "sw1")))
  {
    tft.fillCircle(40, 150, 14, TFT_BLACK);
    tft.fillCircle(40, 150, 14, TFT_GREY);
  }
  if (((char)payload[0] == '0') && (strstr(topic, "sw2")))
  {
    tft.fillCircle(40, 190, 14, TFT_BLACK);
    tft.fillCircle(40, 190, 14, TFT_GREY);
  }
}
void drop(int x, int y)
{
  tft.fillCircle(x, y + 3, 3, TFT_CYAN);
  tft.fillTriangle(x - 3, y + 3, x + 3, y + 3, x, y - 4, TFT_CYAN);
}