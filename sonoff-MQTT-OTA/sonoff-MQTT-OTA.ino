/* 
  Lvgeek modified sonoff-MQTT for local MQTT and HA with OTA support
  
  Alternative firmware for Itead Sonoff switches, based on the MQTT protocol 
  The very initial version of this firmware was a fork from the SonoffBoilerplate (tzapu)
  
  This firmware can be easily interfaced with Home Assistant, with the MQTT switch 
  component: https://home-assistant.io/components/switch.mqtt/
 
  Libraries :
    - ESP8266 core for Arduino :  https://github.com/esp8266/Arduino
    - PubSubClient:               https://github.com/knolleary/pubsubclient
    - WiFiManager:                https://github.com/tzapu/WiFiManager
  
  Sources :
    - File > Examples > ES8266WiFi > WiFiClient
    - File > Examples > PubSubClient > mqtt_auth
    - https://github.com/tzapu/SonoffBoilerplate
    
  Schematic:
    - VCC (Sonoff) -> VCC (FTDI)
    - RX  (Sonoff) -> TX  (FTDI)
    - TX  (Sonoff) -> RX  (FTDI)
    - GND (Sonoff) -> GND (FTDI)
    
  Steps:
    - Upload the firmware
    - Connect to the new Wi-Fi AP and memorize its name 
    - Choose your network and enter your MQTT username, password, broker 
      IP address and broker port
    - Update your configuration in Home Assistant
  
  MQTT topics and payload:
    - State:    <Chip_ID>/switch/state      ON/OFF
    - Command:  <Chip_ID>/switch/switch     ON/OFF
  
  Configuration (Home Assistant) : 
    switch:
      platform: mqtt
      name: 'Switch'
      state_topic: 'CBFxxx/switch/state'
      command_topic: 'CBFxxx/switch/switch'
      optimistic: false
      
  Modified version of Samuel M. - v1.2 - 11.2016
  If you like this example, please add a star! Thank you!
  https://github.com/mertenats/sonoff
  
  Lvgeek 2016
  https://github.com/lvgeek/ESP8266-Home/tree/master/sonoff-MQTT-OTA
*/

#include <ESP8266WiFi.h>    // https://github.com/esp8266/Arduino
#include <PubSubClient.h>   // https://github.com/knolleary/pubsubclient/releases/tag/v2.6
#include <ArduinoOTA.h>

// WiFi Settings
const char* ssid     = "xxxxxxxx";
const char* password = "xxxxxxxx";
const char* MQTT_SERVER = "192.168.0.80";


#define           STRUCT_CHAR_ARRAY_SIZE 24   // size of the arrays for MQTT username, password, etc.
#define           DEBUG                       // enable debugging

// macros for debugging
#ifdef DEBUG
  #define         DEBUG_PRINT(x)    Serial.print(x)
  #define         DEBUG_PRINTLN(x)  Serial.println(x)
#else
  #define         DEBUG_PRINT(x)
  #define         DEBUG_PRINTLN(x)
#endif

// Sonoff properties
const uint8_t     BUTTON_PIN = 0;
const uint8_t     RELAY_PIN  = 12;
const uint8_t     LED_PIN    = 13;

// MQTT
char              MQTT_CLIENT_ID[7]                                 = {0};
char              MQTT_SWITCH_STATE_TOPIC[STRUCT_CHAR_ARRAY_SIZE]   = {0};
char              MQTT_SWITCH_COMMAND_TOPIC[STRUCT_CHAR_ARRAY_SIZE] = {0};
const char*       MQTT_SWITCH_ON_PAYLOAD                            = "ON";
const char*       MQTT_SWITCH_OFF_PAYLOAD                           = "OFF";

enum CMD {
  CMD_NOT_DEFINED,
  CMD_PIR_STATE_CHANGED,
  CMD_BUTTON_STATE_CHANGED,
};

volatile uint8_t cmd = CMD_NOT_DEFINED;

uint8_t           relayState                                        = HIGH;  // HIGH: closed switch
uint8_t           buttonState                                       = HIGH; // HIGH: opened switch
uint8_t           currentButtonState                                = buttonState;
long              buttonStartPressed                                = 0;
long              buttonDurationPressed                             = 0;

WiFiClient        wifiClient;

PubSubClient      mqttClient(wifiClient);


///////////////////////////////////////////////////////////////////////////
//   MQTT
///////////////////////////////////////////////////////////////////////////
/*
   Function called when a MQTT message arrived
   @param p_topic   The topic of the MQTT message
   @param p_payload The payload of the MQTT message
   @param p_length  The length of the payload
*/
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
  // handle the MQTT topic of the received message
  if (String(MQTT_SWITCH_COMMAND_TOPIC).equals(p_topic)) {
    if ((char)p_payload[0] == (char)MQTT_SWITCH_ON_PAYLOAD[0] && (char)p_payload[1] == (char)MQTT_SWITCH_ON_PAYLOAD[1]) {
      if (relayState != HIGH) {
        relayState = HIGH;
        setRelayState();
      }
    } else if ((char)p_payload[0] == (char)MQTT_SWITCH_OFF_PAYLOAD[0] && (char)p_payload[1] == (char)MQTT_SWITCH_OFF_PAYLOAD[1] && (char)p_payload[2] == (char)MQTT_SWITCH_OFF_PAYLOAD[2]) {
      if (relayState != LOW) {
        relayState = LOW;
        setRelayState();
      }
    }
  }
}

/*
  Function called to publish the state of the Sonoff relay
*/
void publishSwitchState() {
  if (relayState == HIGH) {
    if (mqttClient.publish(MQTT_SWITCH_STATE_TOPIC, MQTT_SWITCH_ON_PAYLOAD, true)) {
      DEBUG_PRINT(F("INFO: MQTT message publish succeeded. Topic: "));
      DEBUG_PRINT(MQTT_SWITCH_STATE_TOPIC);
      DEBUG_PRINT(F(". Payload: "));
      DEBUG_PRINTLN(MQTT_SWITCH_ON_PAYLOAD);
    } else {
      DEBUG_PRINTLN(F("ERROR: MQTT message publish failed, either connection lost, or message too large"));
    }
  } else {
    if (mqttClient.publish(MQTT_SWITCH_STATE_TOPIC, MQTT_SWITCH_OFF_PAYLOAD, true)) {
      DEBUG_PRINT(F("INFO: MQTT message publish succeeded. Topic: "));
      DEBUG_PRINT(MQTT_SWITCH_STATE_TOPIC);
      DEBUG_PRINT(F(". Payload: "));
      DEBUG_PRINTLN(MQTT_SWITCH_OFF_PAYLOAD);
    } else {
      DEBUG_PRINTLN(F("ERROR: MQTT message publish failed, either connection lost, or message too large"));
    }
  }
}

/*
  Function called to connect/reconnect to the MQTT broker
 */
void reconnect() {
  uint8_t i = 0;
  while (!mqttClient.connected()) {
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      DEBUG_PRINTLN(F("INFO: The client is successfully connected to the MQTT broker"));
    } else {
      DEBUG_PRINTLN(F("ERROR: The connection to the MQTT broker failed"));
      delay(1000);
      if (i == 3) {
        restart();
      }
      i++;
    }
  }

  if (mqttClient.subscribe(MQTT_SWITCH_COMMAND_TOPIC)) {
    DEBUG_PRINT(F("INFO: Sending the MQTT subscribe succeeded. Topic: "));
    DEBUG_PRINTLN(MQTT_SWITCH_COMMAND_TOPIC);
  } else {
    DEBUG_PRINT(F("ERROR: Sending the MQTT subscribe failed. Topic: "));
    DEBUG_PRINTLN(MQTT_SWITCH_COMMAND_TOPIC);
  }
}




/*
  Function called to toggle the state of the LED
 */
void tick() {
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}



///////////////////////////////////////////////////////////////////////////
//   Sonoff switch
///////////////////////////////////////////////////////////////////////////
/*
 Function called to set the state of the relay
 */
void setRelayState() {
  digitalWrite(RELAY_PIN, relayState);
  digitalWrite(LED_PIN, (relayState + 1) % 2);
  publishSwitchState();
}

/*
  Function called to restart the switch
 */
void restart() {
  DEBUG_PRINTLN(F("INFO: Restart..."));
  ESP.restart();
  delay(1000);
}


///////////////////////////////////////////////////////////////////////////
//   ISR
///////////////////////////////////////////////////////////////////////////
/*
  Function called when the button is pressed/released
 */
void buttonStateChangedISR() {
  cmd = CMD_BUTTON_STATE_CHANGED;
}

///////////////////////////////////////////////////////////////////////////
//   Setup() and loop()
///////////////////////////////////////////////////////////////////////////
void setup() {

  // init the I/O
  pinMode(LED_PIN,    OUTPUT);
  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
  attachInterrupt(BUTTON_PIN, buttonStateChangedISR, CHANGE);

 
  // get the Chip ID of the switch and use it as the MQTT client ID
  sprintf(MQTT_CLIENT_ID, "%06X", ESP.getChipId());
  DEBUG_PRINT(F("INFO: MQTT client ID/Hostname: "));
  DEBUG_PRINTLN(MQTT_CLIENT_ID);

  // set the state topic: <Chip ID>/switch/state
  sprintf(MQTT_SWITCH_STATE_TOPIC, "%06X/switch/state", ESP.getChipId());
  DEBUG_PRINT(F("INFO: MQTT state topic: "));
  DEBUG_PRINTLN(MQTT_SWITCH_STATE_TOPIC);

  // set the command topic: <Chip ID>/switch/switch
  sprintf(MQTT_SWITCH_COMMAND_TOPIC, "%06X/switch/switch", ESP.getChipId());
  DEBUG_PRINT(F("INFO: MQTT command topic: "));
  DEBUG_PRINTLN(MQTT_SWITCH_COMMAND_TOPIC);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(1000);
    ESP.restart();
  }

  ArduinoOTA.onStart([]() {
    DEBUG_PRINTLN("Start");
  });
  ArduinoOTA.onEnd([]() {
    DEBUG_PRINTLN("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DEBUG_PRINT(F("Progress: %u%%\r", (progress / (total / 100))));
  
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if      (error == OTA_AUTH_ERROR   ) DEBUG_PRINTLN("Auth Failed");
    else if (error == OTA_BEGIN_ERROR  ) DEBUG_PRINTLN("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) DEBUG_PRINTLN("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) DEBUG_PRINTLN("Receive Failed");
    else if (error == OTA_END_ERROR    ) DEBUG_PRINTLN("End Failed");
  });
  ArduinoOTA.begin();
  DEBUG_PRINTLN("Ready");
  DEBUG_PRINT("IP address: ");
  DEBUG_PRINTLN(WiFi.localIP());  

 


  // configure MQTT
  mqttClient.setServer(MQTT_SERVER, 1883);
  mqttClient.setCallback(callback);

  // connect to the MQTT broker
  reconnect();
  
  
  setRelayState();
  delay(1000);
}


void loop() {
  //ArduinoOTA.handle();

  //yield();

  switch (cmd) {
    case CMD_NOT_DEFINED:
      // do nothing
      break;
   case CMD_BUTTON_STATE_CHANGED:
      currentButtonState = digitalRead(BUTTON_PIN);
      if (buttonState != currentButtonState) {
        // tests if the button is released or pressed
        if (buttonState == LOW && currentButtonState == HIGH) {
          buttonDurationPressed = millis() - buttonStartPressed;
          if (buttonDurationPressed < 500) {
            relayState = relayState == HIGH ? LOW : HIGH;
            setRelayState();
          } else if (buttonDurationPressed < 5000) {
            restart();
          } 
        } else if (buttonState == HIGH && currentButtonState == LOW) {
          buttonStartPressed = millis();
        }
        buttonState = currentButtonState;
      }
      cmd = CMD_NOT_DEFINED;
      break;
  }

  yield();
  
  // keep the MQTT client connected to the broker
  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();

  yield();
}

