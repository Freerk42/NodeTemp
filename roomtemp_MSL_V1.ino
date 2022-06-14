/***************************************************
  
  based on: Adafruit MQTT Library ESP8266 Example
  MQTT room thermometer Makerspace Leiden 
  Freerk 04-2022
  Based on board: NodeMCU 1.0 (ESP-12E Module)


   ****************************************************/

#define DS_PIN 2                // "D4", DS18S20 pin en blauwe led
   
#include <ESP8266WiFi.h>
#include <OneWire.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

/************************* WiFi Access Point *********************************/

#define WLAN_SSID       "MakerSpaceLeiden_deelnemers"
#define WLAN_PASS       "M@@K1234"

/************************* Adafruit.io Setup *********************************/

#define AIO_SERVER      "space.makerspaceleiden.nl"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    ""
#define AIO_KEY         ""

/************ Global State (you don't need to change this!) ******************/

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

/****************************** Feeds ***************************************/

// Setup a feed called 'temperature' for publishing.
// Notice MQTT paths for MSL follow the form: makerspace/feed/<feedname>
Adafruit_MQTT_Publish temperatureFeed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "makerspace/temp/KL");

/*************************** Sketch Code ************************************/

// DS18S20 startup
OneWire  ds(DS_PIN);

// Bug workaround for Arduino 1.6.6, it seems to need a function declaration
// for some reason (only affects ESP8266, likely an arduino-builder bug).
void MQTT_connect();

void setup() {
  Serial.begin(115200);
  delay(500); // workaround so we can see bootup messages ...

  Serial.println(F("MSL roomtemp via MQTT"));
  Serial.println(F("roomtemp_MSL_V1.ino"));
  Serial.println(F("202200517 Freerk de Jong"));

  // Connect to WiFi access point.
  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WLAN_SSID);

  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.println("WiFi connected");
  Serial.println("IP address: "); Serial.println(WiFi.localIP());

}

void loop() {
  static float temperature; // Make a wild guess :-)
   
   
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).  See the MQTT_connect
  // function definition further below.
  MQTT_connect();

  temperature = DS_reader (); 


  // Now we can publish stuff!
  Serial.print(F("\nSending temperature "));
  Serial.print(temperature);
  Serial.print("...");
  if (! temperatureFeed.publish(temperature)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
    delay(5000);
  }

  // ping the server to keep the mqtt connection alive
  // NOT required if you are publishing once every KEEPALIVE seconds
  /*
  if(! mqtt.ping()) {
    mqtt.disconnect();
  }
  */
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(30000);  // wait 30 seconds
       retries--;
       if (retries == 0) {
         // basically die and wait for WDT to reset me
         while (1);
       }
  }
  Serial.println("MQTT Connected!");
}

float DS_reader () {
  /*
   * Reads one DS chip in two phases without using delay(). 
   */
  static long last_action;    // Time of last action in ms
  byte data[12];
  static byte addr[8];
  byte type_s, present = 0;
  static float celsius;
  enum states { // States of the DS read state machine 
    ds_start, 
    ds_read,
  };
  static int state = ds_start;  
  
  switch (state) { 
    case ds_start: {
      ds.reset_search();
      ds.search(addr); 
      if (OneWire::crc8(addr, 7) != addr[7]) {
        #ifdef debug_temp  
          Serial.println("CRC is not valid!");
        #endif
      }
      switch (addr[0]) {
        case 0x10:
          #ifdef debug_temp  
            Serial.println("  Chip = DS18S20");  // or old DS1820
          #endif
            type_s = 1;
        break;
        case 0x28:
          #ifdef debug_temp  
            Serial.println("  Chip = DS18B20");
          #endif
          type_s = 0;
        break;
        case 0x22:
          #ifdef debug_temp  
            Serial.println("  Chip = DS1822");
          #endif
          type_s = 0;
        break;
        default: {
        #ifdef debug_temp  
           Serial.println("Device is not a DS18x20 family device.");
         #endif 
        }
      }
      ds.reset();
      ds.select(addr);
      ds.write(0x44, 1); 
      state = ds_read; 
      last_action = millis(); 
    } break; 
    
    case ds_read: {
      if ((millis() - last_action) < 800) break; // doe pas na 1 seconde
      
      present = ds.reset();
      ds.select(addr);    
      ds.write(0xBE);         // Read Scratchpad
      #ifdef debug_temp  
        Serial.print("  Data = ");
        Serial.print(present, HEX);
        Serial.print(" ");
      #endif
          for (int i = 0; i < 9; i++) {           // we need 9 bytes
        data[i] = ds.read();
        #ifdef debug_temp  
          Serial.print(data[i], HEX);
          Serial.print(" "); 
        #endif
      }
      #ifdef debug_temp
      Serial.println(" ");
      Serial.print(" CRC=");
      Serial.print(OneWire::crc8(data, 8), HEX);
      Serial.println(" ");
      #endif
      int16_t raw = (data[1] << 8) | data[0];
      if (type_s) {
        raw = raw << 3; // 9 bit resolution default
        if (data[7] == 0x10) {
          // "count remain" gives full 12 bit resolution
          raw = (raw & 0xFFF0) + 12 - data[6];
        }
      } else {
        byte cfg = (data[4] & 0x60);
        // at lower res, the low bits are undefined, so let's zero them
        if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
        else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
        else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
        //// default is 12 bit resolution, 750 ms conversion time
      }
      celsius = (float)raw / 16.0;
      #ifdef debug_temp
        Serial.print("  Temperature = ");
        Serial.print(celsius);
        Serial.println(" Celsius, ");
      #endif
      state = ds_start; 
    }
  }
  return celsius;  
}
