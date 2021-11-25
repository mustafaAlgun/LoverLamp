// syncenlight version based on tueftla, based on the Netzbasteln version
// This version belongs to Mustafa Algun
#include <FS.h>                   // File system, this needs to be first.
#include <ESP8266WiFi.h>          // ESP8266 Core WiFi Library
#include <WiFiClientSecure.h>
#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     // Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <Adafruit_NeoPixel.h>    // LED
#include <PubSubClient.h>         // MQTT client
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson
#include <Ticker.h>

//#include <ESP8266HTTPClient.h>
//#include <ESP8266httpUpdate.h>

//---------------------------------------------------------
// Pins and configuration
#define PIXEL_PIN D4 // D4
#define PIXEL_COUNT 16 // Number of LEDs
#define LOOP_PERIOD 250 // Time in milliseconds for each loop (how long need to hold touch sensor)
#define touchPin D0 // Pin for capactitive touch sensor

// Defaults
char mqttServer[80] = "******.s1.eu.hivemq.cloud";
char mqttPort[40] = "8883";
char mqttUser[40] = "mustafa";
char mqttPassword[40] = "password";
char friendCode[40] = "my/test/topic";

bool lastSensorState = false;

//---------------------------------------------------------
bool shouldSaveConfig = false;
void save_config_callback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void colorLightChanged(uint8_t bright, uint32_t rgb);

WiFiManager wifiManager;

WiFiClientSecure wifiClient; // Use WiFiClient if do not want to use secure MQTT
//WiFiClient wifiClient; // Use WiFiClientSecure if want to use secure MQTT

PubSubClient mqttClient(wifiClient);

Adafruit_NeoPixel leds = Adafruit_NeoPixel(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800); // NEO_RGBW for Wemos Mini LED Modules, NEO_GRB for most Stripes 

uint16_t hue = 0; // 0-359
unsigned int brightness = 255;

bool lightsOn = true;

extern const uint8_t gamma8[];

Ticker swooshTicker;
unsigned int swooshTime;
uint16_t swooshHue = 240; // blue swoosh

String chipId = String(ESP.getChipId(), HEX);
char chipIdCharArr[7];

void setup() {
  Serial.begin(9600);
  pinMode(touchPin, INPUT);
  //wifiClient.setInsecure();  // Do not force certificate authenication, uncomment if use WiFiClientSecure
  
  // Initialize LEDs and swoosh animation (played during startup and configuration)
  leds.begin();
  leds.setBrightness(brightness);
  swooshTime = 0;
  swooshTicker.attach_ms(10, update_swoosh);
  
  // Read out chip ID and construct SSID for hotspot
  chipId.toUpperCase();
  chipId.toCharArray(chipIdCharArr, 7);
  String ssid = "Bağlanmak İçin Lütfen Wifi Bilgilerinizi Giriniz";
  int ssidCharArrSize = ssid.length() + 1;
  char ssidCharArr[ssidCharArrSize];
  ssid.toCharArray(ssidCharArr, ssidCharArrSize);

  //Serial.println("Hi!");
  Serial.print("Chip ID: ");
  Serial.println(chipId);

  // Read configuration from FS json.
  Serial.println("Mounting FS ...");
  // Clean FS, for testing
  //SPIFFS.format();
  if (SPIFFS.begin()) {
    Serial.println("Mounted file system.");
    if (SPIFFS.exists("/config.json")) {
      // File exists, reading and loading.
      Serial.println("Reading config file.");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("Opened config file.");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nParsed json.");
          strcpy(mqttServer, json["mqtt_server"]);
          strcpy(mqttPort, json["mqtt_port"]);
          strcpy(mqttUser, json["mqtt_user"]);
          strcpy(mqttPassword, json["mqtt_password"]);
          strcpy(friendCode, json["friend_code"]);
        } else {
          Serial.println("Failed to load json config.");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("Failed to mount FS.");
  }
  //end read

  // The extra parameters to be configured.
 // WiFiManagerParameter customMqttServer("Server", "MQTT Server", mqttServer, 40);
 // WiFiManagerParameter customMqttPort("Port", "MQTT Port", mqttPort, 40);
 // WiFiManagerParameter customMqttUser("User", "MQTT User", mqttUser, 40);
 // WiFiManagerParameter customMqttPassword("Password", "MQTT Password", mqttPassword, 40);
//  WiFiManagerParameter customFriendCode("FriendCode", "FriendCode", friendCode, 40);
  
  wifiManager.setSaveConfigCallback(save_config_callback);
  // Add all parameters.
  //wifiManager.addParameter(&customMqttServer);
  //wifiManager.addParameter(&customMqttPort);
 // wifiManager.addParameter(&customMqttUser);
  //wifiManager.addParameter(&customMqttPassword);
 // wifiManager.addParameter(&customFriendCode);

  wifiManager.autoConnect(ssidCharArr);

  // We are connected.
  Serial.println("WiFi Connected.");
  
  // Read updated parameters.
  //strcpy(mqttServer, customMqttServer.getValue());
 // strcpy(mqttPort, customMqttPort.getValue());
  //strcpy(mqttUser, customMqttUser.getValue());
 // strcpy(mqttPassword, customMqttPassword.getValue());
//  strcpy(friendCode, customFriendCode.getValue());
/*
  // Save the custom parameters to FS.
  if (shouldSaveConfig) {
    Serial.println("Saving config.");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
//    json["mqtt_server"] = mqttServer;
//    json["mqtt_port"] = mqttPort;
//    json["mqtt_user"] = mqttUser;
//    json["mqtt_password"] = mqttPassword;
//    json["friend_code"] = friendCode;
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("Failed to open config file for writing.");
    }
    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  }
  // End save.
  */
  // Start MQTT client.
  String s = String((char*)mqttPort);
  unsigned int p = s.toInt();

 
  mqttClient.setServer(mqttServer, p);
  mqttClient.setCallback(mqtt_callback);
 // mqttClient.setKeepAlive(600); // Disable Keepalive to stop disconnects
  Serial.println("MQTT client started.");

  
  wifiClient.setInsecure();

  swooshTicker.detach();

  // start WebServer
   
}

void loop() {
  long startTime = millis();
 
  // If not connected anymore try to reconnect
  if (!mqttClient.connected()) {
    swooshTicker.attach_ms(10, update_swoosh);
    mqtt_reconnect();
  }

  // Necessary to keep up MQTT connection
  

  // Read capacitive sensor, if touched change color
  long sensorValue;
  sensorValue = digitalRead(touchPin);
  
  // Register button press if sensor value is above threshold
  //Serial.print("sensor: ");
  //Serial.println(sensorValue);
  if (sensorValue == HIGH) {
    //hue = hue + 1;
    //hue = (hue + 1) % 360;
    hue = random(360); // Random Hue
    
  //  EspalexaDevice* d = espalexa.getDevice(0);
    
  
    lightsOn = !lightsOn;    // Toggle Light State
    
    //Serial.print("value: ");
    //Serial.print(lightsOn);
    
    if (!lightsOn){         // Turn off Light
      off_led();
  //    Serial.print("Turning Off");
    }else{                  // Turn on Light
      //Serial.print("Turning On"); 
      update_led();
    }

    
    char payload[1];
    itoa(hue+361*!lightsOn, payload, 10); // Encode Hue and current light status in MQTT Payload
    mqttClient.publish(friendCode, payload, true); // Send payload to freindCode Topic
   
    //Serial.print("New color: ");
    //Serial.println(hue);
  }

  // For determining first loop after touch is released
  if (sensorValue == HIGH) {
    lastSensorState = true;
  } else {
    lastSensorState = false;
  }


  // If not connected anymore try to reconnect
  if (!mqttClient.connected()) {
    swooshTicker.attach_ms(10, update_swoosh);
    mqtt_reconnect();
  }

  // Necessary to keep up MQTT connection
  mqttClient.loop();
  yield();
  // Debug output
  //Serial.print("Sensor value: ");
  //Serial.print(sensorValue);

  //Serial.print("hue: ");
  //Serial.print(hue);
  //Serial.print("\t");
  //Serial.print("Processing time in loop: ");
  //Serial.print(millis() - startTime);
  //Serial.print("\n");

   
  int delayValue = LOOP_PERIOD - (millis() - startTime);
  if (delayValue > 0) {
    delay(delayValue);
  }
}





//////////
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  //Serial.print("Received [");
  //Serial.print(topic);
  //Serial.print("]: ");
  //for (int i = 0; i < length; i++) {
  //  Serial.print((char)payload[i]);
  //}
  //Serial.print(" (length ");
  //Serial.print(length);
  //Serial.print(")");
  //Serial.println();
 
 
  // Update color of LEDs
  if (length <= 3) {
    payload[length] = '\0';
    String s = String((char*)payload);
    //Serial.println(s);
    
    unsigned int newHue = s.toInt();

    //Serial.print("new hue: ");
    //Serial.print(newHue);
     //Serial.print(" old hue: ");
    //Serial.print(hue);
    
    // if same state as before, do not change
    if (newHue == hue)
      return;

    hue = newHue;      // update state
   
    // if Color below 360, Hue should be on  
    if (newHue >= 0 && newHue < 360) {
      update_led();      // change the color
      lightsOn = true;   // Change internal state track to on
    }

    // if color is above 360, lights should be off
    if (newHue > 360) {
      //Serial.print("I should turn off");
      off_led();         // Turn off the lights
      lightsOn = false;  // turn off light state
    }
    

    
  }
}

void mqtt_reconnect() {
  while (!mqttClient.connected()) {
    Serial.println("Connecting MQTT...");
    if (mqttClient.connect(chipIdCharArr, mqttUser, mqttPassword)) {
      swooshTicker.detach();
      Serial.println("MQTT connected.");
      mqttClient.subscribe(friendCode); // QoS level 1
    }
    else {
      Serial.print("Error, rc=");
      Serial.print(mqttClient.state());
      delay(5000);
    }
  }
}

void update_led() {   // Update LEDs
  uint32_t color = hsv_to_rgb(hue, 255, brightness);
  
  for (uint16_t i=0; i < PIXEL_COUNT; i++) {
    leds.setPixelColor(i, color);
  }
  leds.show();  
}

void off_led() {  // Update LEDs

  for (uint16_t i=0; i < PIXEL_COUNT; i++) {
    leds.setPixelColor(i, 0);
  }
  leds.show();  
}

void update_swoosh() {
  swooshTime = swooshTime + 10;

  int value = (int) 127.5 * sin(2*3.14/1000 * swooshTime) + 127.5;
  for (int i = 0; i < PIXEL_COUNT; i++) {
    leds.setPixelColor(i, hsv_to_rgb(swooshHue, 255, value));
  }
  leds.show();
}


// hue: 0-359, sat: 0-255, val (lightness): 0-255
uint32_t hsv_to_rgb(unsigned int hue, unsigned int sat, unsigned int val) {
  int r, g, b, base;
  if (sat == 0) {
    r = g = b = val;
  }
  else {
    base = ((255 - sat) * val) >> 8;
    switch (hue / 60) {
      case 0:
        r = val;
        g = (((val - base) * hue) / 60) + base;
        b = base;
        break;
      case 1:
        r = (((val - base) * (60 - (hue % 60))) / 60) + base;
        g = val;
        b = base;
        break;
      case 2:
        r = base;
        g = val;
        b = (((val - base) * (hue % 60)) / 60) + base;
        break;
      case 3:
        r = base;
        g = (((val - base) * (60 - (hue % 60))) / 60) + base;
        b = val;
        break;
      case 4:
        r = (((val - base) * (hue % 60)) / 60) + base;
        g = base;
        b = val;
        break;
      case 5:
        r = val;
        g = base;
        b = (((val - base) * (60 - (hue % 60))) / 60) + base;
        break;
    }
  }
  
  return leds.Color(
    pgm_read_byte(&gamma8[r]),
    pgm_read_byte(&gamma8[g]),
    pgm_read_byte(&gamma8[b]));
}


//the color device callback function has two parameters
void colorLightChanged(uint8_t bright, uint32_t rgb) {

  //EspalexaDevice* d = espalexa.getDevice(0);
  //uint32_t myrgb = d->getRGB();
  //uint8_t myb = d->getPercent();

  // Update LEDs  
  for (uint16_t i=0; i < PIXEL_COUNT; i++) {
    leds.setPixelColor(i, rgb);
  }
  leds.setBrightness(brightness);
  leds.show();  

  // Update Internal Color State
  // Hue Saturation goes from 0 to 65535
  if(bright == 0)
    lightsOn = false;
  else
    lightsOn = true;  

  // Update MQTT state
  char payload[1];
  itoa(hue+361*!lightsOn, payload, 10); // Encode Hue and current light status in MQTT Payload
  mqttClient.publish(friendCode, payload, true); // Send payload to freindCode Topic
    
}

// Gamma correction curve
// https://learn.adafruit.com/led-tricks-gamma-correction/the-quick-fix
const uint8_t PROGMEM gamma8[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255
};
