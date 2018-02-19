#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <Wire\Wire.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <SDP6x\SDP6x.h>
#include <Ticker.h>
#include <ESP8266HTTPClient.h>

#define SERIAL_DEBUG 1
//define your default values here, if there are different values in config.json, they are overwritten.


#define DAQ_INTERVAL 5
char sensorID[34] = "";
float difPressure;
//flag for saving data
bool shouldSaveConfig = false;
volatile bool measurementFlag = true;

const char* fingerprint = "C3 57 0A 47 01 AA 16 DA 6E DA 51 1C B7 AC ED 64 5E AB EB 00";

Ticker DAQTimer;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void setup() {
	pinMode(D4, OUTPUT);
	digitalWrite(D4, LOW);
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();  
  Wire.begin(D2, D1);
  twi_setClockStretchLimit(20000);

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json"); 
          strcpy(sensorID, json["sensorID"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_sensorID("sensorID", "Location", sensorID, 32);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //wifiManager.resetSettings();

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_sensorID);
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(120);
  wifiManager.setConnectTimeout(30);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  //wifiManager.autoConnect(String(ESP.getChipId()).c_str(), "dankmemes");
  if (!wifiManager.autoConnect(String(ESP.getChipId()).c_str())) {
	  digitalWrite(D4, HIGH);
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  //read updated parameters
  

  //save the custom parameters to FS
  if (shouldSaveConfig) {
	strcpy(sensorID, custom_sensorID.getValue());
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["sensorID"] = sensorID;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  DAQTimer.attach(DAQ_INTERVAL, setMeasurementFlag);
  difPressure = SDP6x.GetPressureDiff();

}

void loop() {
  // put your main code here, to run repeatedly:
	
	if (measurementFlag) {
		pushMqtt();
	}
	

}


void setMeasurementFlag() {
	measurementFlag = true;
}

void pushMqtt() {
	long rssi = WiFi.RSSI();
	static char result_str[128] = "";
	difPressure = SDP6x.GetPressureDiff();
	//Serial.println(difPressure);
	sprintf(result_str,
		"{\"id\":\"%s\","
		"\"dp\":%4.2f,"
		"\"uptime\":%lu,"
		"\"rssi\":%ld"
		"}", sensorID, difPressure, millis(), rssi);
#if SERIAL_DEBUG
	Serial.println(result_str);
#endif
	if (WiFi.status() != WL_CONNECTED) {
	WiFiManager wifiManager;
	wifiManager.setTimeout(1);
	wifiManager.setConnectTimeout(30);
		while (WiFi.status() != WL_CONNECTED) {
			wifiManager.autoConnect();
		}
	}
	HTTPClient http;

	String host = "iot.research.hamk.fi";
	String fingerprint = "C3:57:0A:47:01:AA:16:DA:6E:DA:51:1C:B7:AC:ED:64:5E:AB:EB:00";
	http.begin("iot.research.hamk.fi",443,"/api/v1/paivola/pressure","c3570a4701aa16da6eda511cb7aced645eabeb00");
	//http.writeToStream(&Serial);
	http.addHeader("Content-Type", "application/json");
	int postcode = http.POST(result_str);
	Serial.println(postcode);
	//http.writeToStream(&Serial);
	String payload = http.getString();
	Serial.println(payload);
	http.end();
	measurementFlag = false;
}