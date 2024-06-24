#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <OneWire.h>
#include <DallasTemperature.h>

const char *ssid = "Xiaomi_11T_Pro";
const char *password = "josselin@2486";

#define TdsSensorPin 34 // GPIO TDS Sensor (doit le GND du module doit etre relier à l'ESP ainsi que 3.3V)
#define VREF 3.3        // analog reference voltage(Volt) of the ADC
#define SCOUNT 30       // sum of sample point

int analogBuffer[SCOUNT]; // store the analog value in the array, read from ADC
int analogBufferTemp[SCOUNT];
int analogBufferIndex = 0;
int copyIndex = 0;
float averageVoltage = 0;
unsigned long previousMillis = 0;
unsigned long interval = 30000;

// ================== Aqua =======================

const int WaterPump = 33;

const int SondeTemp_PIN = 27;   // GPIO where the DS18B20 is connected to do it (le GND du module doit etre relier à l'ESP ainsi que 3.3V)
OneWire oneWire(SondeTemp_PIN); // Setup a oneWire instance to communicate with any OneWire devices

const int WaterLevel_PIN = 32; // YELLOW = GND. BROWN = VCC. BLUE = DATA
DallasTemperature sensors(&oneWire);
float GetTDSValue();

// ================== Fonctions ========================

int getMedianNum(int bArray[], int iFilterLen)
{
  int bTab[iFilterLen];
  for (byte i = 0; i < iFilterLen; i++)
    bTab[i] = bArray[i];
  int i, j, bTemp;
  for (j = 0; j < iFilterLen - 1; j++)
  {
    for (i = 0; i < iFilterLen - j - 1; i++)
    {
      if (bTab[i] > bTab[i + 1])
      {
        bTemp = bTab[i];
        bTab[i] = bTab[i + 1];
        bTab[i + 1] = bTemp;
      }
    }
  }
  if ((iFilterLen & 1) > 0)
    bTemp = bTab[(iFilterLen - 1) / 2];
  else
    bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
  return bTemp;
}

float GetTDSValue()
{
  // Calcul de la valeur de tdsValue
  sensors.requestTemperatures();
  float temperature = sensors.getTempCByIndex(0);

  static unsigned long analogSampleTimepoint = millis();
  if (millis() - analogSampleTimepoint > 40U)
  {
    analogSampleTimepoint = millis();
    analogBuffer[analogBufferIndex] = analogRead(TdsSensorPin);
    analogBufferIndex++;
    if (analogBufferIndex == SCOUNT)
      analogBufferIndex = 0;
  }

  static unsigned long printTimepoint = millis();
  if (millis() - printTimepoint > 800U)
  {
    printTimepoint = millis();
    for (copyIndex = 0; copyIndex < SCOUNT; copyIndex++)
      analogBufferTemp[copyIndex] = analogBuffer[copyIndex];
    averageVoltage = getMedianNum(analogBufferTemp, SCOUNT) * (float)VREF / 1024.0;
    float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
    float compensationVolatge = averageVoltage / compensationCoefficient;
    return (133.42 * compensationVolatge * compensationVolatge * compensationVolatge - 255.86 * compensationVolatge * compensationVolatge + 857.39 * compensationVolatge) * 0.5;
  }
  return 0; // Retourne 0 si la valeur de tdsValue n'a pas été mise à jour
}

String verifyWaterLevel(int pin)
{
  int valeur = analogRead(pin);

  if (valeur < 500)
  {
    return "Bas";
  }
  else
  {
    return "Ok";
  }
}

String PumpStatut()
{

  if (digitalRead(WaterPump) == LOW)
  {
    return "On";
  }
  else
  {
    return "Off";
  }
}

float GetTemperatureValue()
{
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

float tdsValue = 0;
float temperature = 0;

// ================== Aqua ========================

AsyncWebServer server(80);

void setup()
{
  //----------------------------------------------------Serial
  Serial.begin(115200);
  Serial.println("\n");
  sensors.begin();

  //----------------------------------------------------GPIO
  pinMode(WaterPump, OUTPUT);
  digitalWrite(WaterPump, LOW);
  pinMode(SondeTemp_PIN, INPUT);
  pinMode(WaterLevel_PIN, INPUT);
  pinMode(TdsSensorPin, INPUT);

  //----------------------------------------------------SPIFFS
  if (!SPIFFS.begin())
  {
    Serial.println("Erreur SPIFFS...");
    return;
  }

  File root = SPIFFS.open("/");
  File file = root.openNextFile();

  Serial.println("\n");

  while (file)
  {
    Serial.print("Main conected to File: ");
    Serial.println(file.name());
    file.close();
    file = root.openNextFile();
  }

  Serial.println("\n");

  //----------------------------------------------------WIFI
  WiFi.begin(ssid, password);
  Serial.print("Tentative de connexion...");

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(100);
  }

  Serial.println("\n");
  Serial.println("Connexion etablie!");
  Serial.print("Adresse IP: ");
  Serial.println(WiFi.localIP());

  //----------------------------------------------------SERVER
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/index.html", "text/html"); });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/style.css", "text/css"); });

  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/script.js", "text/javascript"); });

  server.on("/Righteous-Regular.ttf", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/Righteous-Regular.ttf", "application/x-font-ttf"); });
  // server.on("fonts/HELV.ttf", HTTP_GET, [](AsyncWebServerRequest *request)
  //           { request->send(FILESYSTEM, "fonts/HELV.ttf", "application/x-font-ttf"); });

  // renvoyer la valeur au script EAU
  server.on("/lireNiveauEau", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String val = verifyWaterLevel(WaterLevel_PIN);
    String WaterLevel = String(val);
    request->send(200, "text/plain", WaterLevel); });

  // renvoyer la valeur au script
  server.on("/lireTemperature", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    int valTemperature = GetTemperatureValue();
    String temp = String(valTemperature);
    request->send(200, "text/plain", temp); });

  // renvoyer la valeur au script
  server.on("/lireTDS", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    int val = GetTDSValue();
    String tds = String(val);
    request->send(200, "text/plain", tds); });

  // renvoyer la valeur au script
  server.on("/lirePumpStatut", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String PumpSt = PumpStatut();
    request->send(200, "text/plain", PumpSt); });

  server.begin();
  Serial.println("Serveur actif!");
}

void loop()
{
  String waterLevelStatus = verifyWaterLevel(WaterLevel_PIN);

  if (waterLevelStatus == "Bas")
  {
    digitalWrite(WaterPump, HIGH);
  }
  else if (waterLevelStatus == "Ok")
  {
    digitalWrite(WaterPump, LOW);
  }

  Serial.println(waterLevelStatus);

  unsigned long currentMillis = millis();
  // if WiFi is down, try reconnecting
  if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillis >= interval))
  {
    Serial.print(millis());
    Serial.println("Reconnecting to WiFi...");
    WiFi.disconnect();
    WiFi.reconnect();
    previousMillis = currentMillis;
    delay(1000);
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("WiFi Reconected !");
    }
  }
  delay(1000);
}
