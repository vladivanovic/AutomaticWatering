#include <Arduino.h>
// Initiate I2C Protocol
#include <Wire.h>
#include <Adafruit_Sensor.h>
// Screen
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// Library for UNO R4 WiFi networking
#include <WiFiS3.h>
#include "arduino_secrets.h"
// Grove Temp Sensor
#include <DHT.h>
// R4Http Client for sending messages to Splunk
#include <ArduinoHttpClient.h>

//------------------------------

//BME280
#define I2C_SCL 5
#define I2C_SDA 4
Adafruit_BME280 bme;
// Screen
#define SCREEN_WIDTH 128    // OLED display width, in pixels
#define SCREEN_HEIGHT 32    // OLED display height, in pixels
#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
// Wifi - please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;        // your network password (use for WPA, or use as key for WEP)
int status = WL_IDLE_STATUS;     // the WiFi radio's status
// Ultrasonic Sensor
int Trig = 8;
int Echo = 9;
int Duration;
float Distance;
// LED
int led = 7;
// Moisture sensor
int water_count = 0;
#define THRESHOLD 420 // Higher than 530 = Dry
// relay
int relay = 13;
// Temp Sensor
#define DHTTYPE 22
#define DHTPIN 2
DHT dht(DHTPIN, DHTTYPE);
// Splunk Forwarder key in arduino_secrets.h
WiFiClient wifi;
char splunkServerAddress[] = "192.168.128.224";
int splunkPort = 8088;
char splunkPath[] = "/services/collector/event";
char splunk_key[] = SPLUNK_KEY;
// Initialize the HttpClient object once
HttpClient http(wifi, splunkServerAddress, splunkPort);

//
// Setup routine
void setup() {
  Serial.begin(9600);
  // Screen
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  //initialize with the I2C addr 0x3C (128x64)
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(25,10);  
  display.println("Booting up");
  display.display();
  // Wifi - check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }
  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }
  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);
    // wait 10 seconds for connection:
    delay(10000);
  }
  // you're connected now, so print out the data:
  Serial.println("You're connected to the network");
  printCurrentNet();
  // Ultrasonic Sensor
  pinMode(Trig,OUTPUT);
  pinMode(Echo,INPUT);
  // LED
  pinMode(led,OUTPUT);
  // Relay
  pinMode(relay, OUTPUT);
  //BME280
  Wire.begin();
  dht.begin();
  delay(1000);
}

// Loop Functions
void loop() {
  //Check temperature, humidity, and soil humidity once a minute.
  float temp, hum;
  int moist;
  //
  checkWater();
  delay(10000);
  //
  checkMoisture(moist);
  delay(10000);
  //
  checkTemp(temp, hum);
  delay(10000);
  //
  printWifiData();
  IPAddress ipadd = WiFi.localIP();
  display.clearDisplay();
  display.setCursor(25,10);  
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.println("Wifi");
  display.setCursor(25,20);
  display.setTextSize(1);
  display.print(ipadd);
  display.display();
  // Send to Splunk
  char all_data[256];
  //snprintf(all_data, sizeof(all_data), "Temperature: %.1f, Humidity: %.1f, Soil Moisture: %d", temp, hum, moist);
  snprintf(all_data, sizeof(all_data),
         "{\"event\":{\"temperature\":%.1f,\"humidity\":%.1f,\"soil_moisture\":%d},\"sourcetype\":\"arduino:autowater\",\"host\":\"ArduinoUnoR4\",\"index\":\"autowater\"}",
         temp,
         hum,
         moist);
  Serial.println(all_data);
  splunkConnect(all_data);
  delay(10000);
}

// All other functions used by Loop
// Grove Temp Sensor
void checkTemp(float &temperature, float &humidity){
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    float f = dht.readTemperature(true); // use if you want Farenheit
    // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(t) || isnan(f)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      return;
    }
    temperature = t;       // Temp C
    humidity = h;         // Humidity %
    display.clearDisplay();
    display.setCursor(25,10);  
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.println("Temperature");
    display.setCursor(25,20);
    display.setTextSize(1);
    display.print(t);
    display.print("C ");
    display.print(h);
    display.print("\%");
    display.display();
}

// Ultrasonic Sensor
void checkWater(){
  //Check the water level in the bucket.
  digitalWrite(Trig,LOW);
  delayMicroseconds(1);
  digitalWrite(Trig,HIGH);
  delayMicroseconds(11);
  digitalWrite(Trig,LOW);
  Duration = pulseIn(Echo,HIGH);
  if (Duration>0)  {
    Distance = Duration/2;
    Distance = Distance*340*100/1000000; // ultrasonic  speed is 340m/s = 34000cm/s = 0.034cm/us 
    Serial.print(Distance);
    Serial.println(" cm");
    display.clearDisplay();
    display.setCursor(25,10);  
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.println("Water Level");
    display.setCursor(25,20);
    display.setTextSize(1);
    display.print(Distance);
    display.display();
  }
}

// Moisture sensor
void checkMoisture(int &moisture){
  // Measure soil humidity
  moisture = analogRead(A0);
  if(moisture  >= THRESHOLD){
    digitalWrite(led, HIGH);
    water_count++;
    if(water_count == 5){ //To wait for the water to go through the pot.
      watering();
      water_count = 0;
    }
  } else {
    digitalWrite(led, LOW);
  }
  display.clearDisplay();
  display.setCursor(25,10);  
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.println("Soil Dryness");
  display.setCursor(25,20);
  display.setTextSize(1);
  display.print(moisture);
  display.display();
}

// Watering
void watering(){
  digitalWrite(relay, HIGH);
  delay(2000);
  digitalWrite(relay, LOW);
  delay(8000);
}

// IP Address on Wifi
void printWifiData() {
  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
}

// SSID and other Wifi related information
void printCurrentNet() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.println(rssi);
}

// HTTP Functions
void splunkConnect(char* requestBody) {
  Serial.println("\n--- Splunk Connection Attempt ---");

  // 1. Start the HTTP POST request
  http.beginRequest();
  http.post(splunkPath); // Specify the path for the POST request

  // 2. Add necessary headers
  http.sendHeader("User-Agent", "Arduino Uno R4 Wifi");
  http.sendHeader("Content-Type", "application/json"); // HEC typically expects JSON
  http.sendHeader("Content-Length", String(strlen(requestBody))); // Must specify content length

  // 3. Form and add the Authorization header
  String authHeaderValue = "Splunk " + String(splunk_key);
  http.sendHeader("Authorization", authHeaderValue);

  // --- Debugging output for your Arduino Serial Monitor ---
  Serial.print("Target URL: http://");
  Serial.print(splunkServerAddress);
  Serial.print(":");
  Serial.print(splunkPort);
  Serial.println(splunkPath);
  Serial.print("Authorization Header: ");
  Serial.println(authHeaderValue);
  Serial.print("Request Body being sent: ");
  Serial.println(requestBody);
  // --- End Debugging output ---

  // 4. End headers and send the request body
  http.endRequest();
  http.print(requestBody); // Send the actual request body

  // 5. Get the response
  int httpResponseCode = http.responseStatusCode();
  String responseBody = http.responseBody();

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    Serial.print("Splunk Response Body: ");
    Serial.println(responseBody);
  } else {
    Serial.print("HTTP POST failed, Error code: ");
    Serial.println(httpResponseCode);
    // Additionally, check for write errors on the underlying client
    int writeError = http.getWriteError();
    if (writeError != 0) {
      Serial.print("Underlying WiFiClient write error: ");
      Serial.println(writeError);
      // You might need to map these error codes to WiFiClient specific errors
      // e.g., if (writeError == ECONNREFUSED) etc.
    }
  }
  // No explicit http.stop() needed here as the HttpClient object typically manages the WiFiClient.
  // The connection might be kept alive for subsequent requests or closed by the client library.
}