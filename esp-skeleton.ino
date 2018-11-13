#if defined(ESP8266)
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <ESP8266WebServer.h>
#else
#include <SPIFFS.h>
#include <WiFi.h>          //https://github.com/esp8266/Arduino
#include <WebServer.h>
#endif

//needed for library
#include <DNSServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>
#include <time.h>
#include <ArduinoOTA.h>

#include <Bounce2.h>

#include "trace.h"

//
//@******************************* constants *********************************

const int buttonPin = 11;


//@******************************* variables *********************************

char device_id[16] = "device01";
char ota_password[20] = "changeme";

// 
// Network resources
//
bool shouldSaveConfig = false;
bool wifiConnected = false;

// 
// IO resources
// 
Bounce testButton = Bounce(); // Instantiate a Bounce object


//
//@******************************* functions *********************************

void readConfig();
void writeConfig();

void saveConfigCallback() 
{
  ALERT("Will save config");
  shouldSaveConfig = true;
}

void readConfig() 
{
  if (!SPIFFS.begin()) {
    ALERT("failed to mount FS");
    return;
  }

  NOTICE("mounted file system");
  if (!SPIFFS.exists("config.json")) {
    ALERT("No configuration file found");
    return;
  }

  //file exists, reading and loading
  NOTICE("reading config file");
  File configFile = SPIFFS.open("config.json", "r");
  if (!configFile) {
    ALERT("Cannot read config file");
    return;
  }

  size_t size = configFile.size();
  NOTICE("Parsing config file, size=%d", size);

  DynamicJsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  if (error) {
    ALERT("Failed to parse config file");
    configFile.close();
    return;
  }

  JsonObject root = doc.as<JsonObject>();
  {
    char buf[256];
    serializeJson(root, buf, sizeof(buf));
    NOTICE("Read config: %s", buf);
  }

  strlcpy(device_id, root["device_id"]|"device00", sizeof(device_id));
  strlcpy(ota_password, root["ota_password"]|"changeme", sizeof(ota_password));

  configFile.close();

}

void writeConfig() 
{
  ALERT("saving config to flash");

  if (!SPIFFS.begin()) {
    ALERT("failed to mount FS");
    return;
  }

  File configFile = SPIFFS.open("config.json", "w");
  if (!configFile) {
    ALERT("Unable to create new config file");
    return;
  }

  DynamicJsonDocument doc;
  JsonObject root = doc.to<JsonObject>();

  root["device_id"] = device_id;
  root["ota_password"] = ota_password;

  if (serializeJson(doc, configFile) == 0) {
    ALERT("Failed to serialise configuration");
  }
  {
    char buf[256];
    serializeJson(root, buf, sizeof(buf));
    NOTICE("Written config: %s", buf);
  }
  configFile.close();
}

void setup_wifiMgr() 
{
  readConfig();

  ALERT("Wifi manager setup");
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_device_id("device_id", "Device ID", device_id, sizeof(device_id));
  WiFiManagerParameter custom_ota_password("ota_password", "Update Password", ota_password, sizeof(ota_password));

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  
  //add all your parameters here
  wifiManager.addParameter(&custom_device_id);
  wifiManager.addParameter(&custom_ota_password);

  //reset settings - for testing

  if (testButton.read() == LOW) {
    ALERT("Factory Reset");
    wifiManager.resetSettings();
  }
  
  if (!wifiManager.autoConnect()
    ) {
    ALERT("Failed to connect to WiFi after timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
#ifdef ESP8266
    ESP.reset();
#else
    ESP.restart();
#endif
    
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  ALERT("Connected to WiFi");
  wifiConnected = true;

  //read updated parameters
  strlcpy(device_id, custom_device_id.getValue(), sizeof(device_id));
  strlcpy(ota_password, custom_ota_password.getValue(), sizeof(ota_password));

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    writeConfig();
  }
  
  ALERT("My IP Address: %s", WiFi.localIP().toString().c_str());
}

void setup_OTAUpdate() {
  ArduinoOTA.setHostname(device_id);
  ArduinoOTA.setPassword(ota_password);
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    ALERT("Start OTA update (%s)", type.c_str());
  });
  ArduinoOTA.onEnd([]() {
    ALERT("OTA Complete");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    NOTICE("OTA in progress: %u%%", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    ALERT("OTA Error [%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      ALERT("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      ALERT("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      ALERT("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      ALERT("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      ALERT("End Failed");
    }
  });
  ArduinoOTA.begin();
}


//
//@********************************* setup ***********************************

void setup()
{
  // 
  // Set up the serial port for diagnostic trace
  // 
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Hello World");

  // 
  // Set up the GPIO Pins
  //
  ALERT("IO Setup");
  testButton.attach(buttonPin,INPUT_PULLUP); 
  testButton.interval(25); 
 
  // 
  // Set up the WiFi connection
  // 
  setup_wifiMgr();

  // 
  // Set up the Over-the-Air (OTA) update mechanism
  //
  ALERT("OTA Setup %s", device_id);
  setup_OTAUpdate();
  

  ALERT("Setup complete");
}

//
//@********************************** loop ***********************************

void loop()
{  
  unsigned long now = millis();

  // 
  // Handle OTA Events
  // 
  ArduinoOTA.handle();

  // 
  // Handle Button events
  // 
  testButton.update();

  // 
  // If the egress button is pushed, unlock the door and send an event
  // (the button is active-LOW)
  //
  if (testButton.fell()) {
    ALERT("Test button pushed");
  }

}


// Local Variables:
// mode: C++
// c-basic-offset: 2
// End:
