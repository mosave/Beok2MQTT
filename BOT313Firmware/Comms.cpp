#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include "Config.h"
#include "Comms.h"
#include "Storage.h"

//#define Debug

// Time to wait between connection attempts
#define COMMS_ConnectTimeout ((unsigned long)(60 * 1000))
// Number of connection attempts before resetting controller
#define COMMS_ConnectAttempts 1000000
// Time to wait between RSSI reports
#define COMMS_RSSITimeout ((unsigned long)(10 * 1000))

#define MQTT_ActivityTimeout ((unsigned long)(10 * 1000))
#define MQTT_CbsSize 10
#define MQTT_ClientId 16
#define MQTT_RootSize 32
#define COMMS_StorageId 'C'


#ifdef VERSION
static char* TOPIC_Version PROGMEM = "Version";
#endif
static char* TOPIC_Online PROGMEM = "Online";
static char* TOPIC_Address PROGMEM = "Address";
static char* TOPIC_RSSI PROGMEM = "RSSI";
static char* TOPIC_Activity PROGMEM = "Activity";
static char* TOPIC_Reset PROGMEM = "Reset";
static char* TOPIC_EnableOTA PROGMEM = "EnableOTA";
#ifndef WIFI_HostName
static char* TOPIC_SetName PROGMEM = "SetName";
#endif
#ifndef MQTT_Root
static char* TOPIC_SetRoot PROGMEM = "SetRoot";
#endif

struct CommsConfig {
  // *********** Device configuration
  // Both device host name and MQTT client id  
  // Default is "ESP_<MAC_ADDR>" 
  // Use SetName MQTT command to change
  char hostName[32];
    
  // Top level of MQTT topic tree, trailing "/" is optional
  // An %s is to be replaced with device name.
  // Default is "new/%s".
  // Use SetRoot MQTT command to change
  char mqttRoot[63];
  // *********** End of device configuration
  bool disabled;
} commsConfig;

#define CONFIG_Timeout ((unsigned long)(15*60*1000))

unsigned long commsConnecting;
unsigned long commsConnectAttempt = 0;
unsigned long commsPaused;
unsigned long commsRssiReported;

unsigned long otaEnabled;
bool otaShouldInit;
unsigned int otaProgress;

unsigned long mqttActivity;
bool mqttDisableCallback = false;

WiFiClient wifiClient;
PubSubClient mqttClient( wifiClient );

struct MQTTCallbacks {
  MQTT_CALLBACK;
  MQTT_CONNECT;
};

unsigned int mqttCbsCount=0;
MQTTCallbacks mqttCbs[MQTT_CbsSize];

//**************************************************************************
//                          WIFI helper functions
//**************************************************************************
char* wifiHostName() {
  return commsConfig.hostName;
}

bool wifiConnected() {
  if( commsConfig.disabled ) return false;
  return (WiFi.status() == WL_CONNECTED);
}

bool wifiEnabled() {
  return !commsConfig.disabled;
}

void wifiEnable(){
  aePrintln(F("WIFI: Enabling"));
  commsConfig.disabled = false;
  commsRestart();
}

void wifiDisable(){
  aePrintln(F("WIFI: Disabling"));
  mqttPublish( TOPIC_Online, (long)0, true );
  commsConfig.disabled = true;
  commsRestart();
}


// Internal use only
void commsConnect() {
  if( commsConfig.disabled ) return;
  commsConnectAttempt ++;
  if( commsConnectAttempt > COMMS_ConnectAttempts ) {
    commsRestart();
  }
  if( commsConnectAttempt>1 ) {
    aePrint(F("WIFI: Connecting, attempt ")); 
    aePrintln(commsConnectAttempt);
  } else {
    aePrintln(F("WIFI: Connecting")); 
  }
  commsConnecting = millis();
  commsPaused = 0;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);  
  
  if( strlen(commsConfig.hostName)<=0 ) {
    uint8_t macAddr[6];
    WiFi.macAddress(macAddr);
    sprintf( commsConfig.hostName, "ESP_%02X%02X%02X%02X%02X%02X", macAddr[0], macAddr[1], macAddr[2],macAddr[3], macAddr[4], macAddr[5]);
  }

  if( strlen(commsConfig.mqttRoot)<=0 ) {
    strcpy( commsConfig.mqttRoot, "new/%s/" );
  }
  storageSave();

  WiFi.hostname(commsConfig.hostName);
  WiFi.begin(WIFI_SSID, WIFI_Password);
}

void commsReconnect() {
  if( commsConfig.disabled ) return;
  if( mqttClient.connected() ) mqttClient.disconnect();
  MDNS.end();
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  if( commsConnectAttempt >= COMMS_ConnectAttempts ) {
    commsRestart();
  } else {
    commsConnect();
  }
}

//**************************************************************************
//                      MQTT helper functions
//**************************************************************************
bool mqttConnected() {
  return mqttClient.connected();
}
char* mqttTopic( char* buffer, char* TOPIC_Name ) {
  return mqttTopic( buffer, TOPIC_Name, NULL, NULL );
}
char* mqttTopic( char* buffer, char* TOPIC_Name, char* topicVar ) {
  return mqttTopic( buffer, TOPIC_Name, topicVar, NULL );
}
char* mqttTopic( char* buffer, char* TOPIC_Name, char* topicVar1, char* topicVar2 ) {
  char fstr[64]; // format string
  char empty[2] = ""; // to replace NULL variables
  
  sprintf( fstr, commsConfig.mqttRoot, commsConfig.hostName );
  // Append "/" to the end of path
  if( fstr[strlen(fstr)-1] != '/' ) strcat( fstr, "/" );
  // Delete "/" from the TOPIC_Name beginning
  while( (*TOPIC_Name) == '/' ) TOPIC_Name++;
  
  strcat( fstr, TOPIC_Name );
  sprintf( buffer, fstr, (topicVar1!=NULL) ? topicVar1 : empty, (topicVar2!=NULL) ? topicVar2 : empty );
  return( buffer );
}

// Check if "topic" string conforms TOPIC_Name template (should be "%/Name")
bool mqttIsTopic( char* topic, char* TOPIC_Name ) {
  char topicName[63];
  return (strcmp( topic, mqttTopic( topicName, TOPIC_Name ) )==0);
}
bool mqttIsTopic( char* topic, char* TOPIC_Name, char* topicVar ){
  char topicName[63];
  return (strcmp( topic, mqttTopic( topicName, TOPIC_Name, topicVar ) )==0);
}
bool mqttIsTopic( char* topic, char* TOPIC_Name, char* topicVar1, char* topicVar2 ) {
  char topicName[63];
  return (strcmp( topic, mqttTopic( topicName, TOPIC_Name, topicVar1, topicVar2 ) )==0);
}

// Wrappers to mqtt subscribtion
void mqttSubscribeTopic( char* TOPIC_Name ) {
  mqttSubscribeTopic( TOPIC_Name, NULL, NULL );
}
void mqttSubscribeTopic( char* TOPIC_Name, char* topicVar ) {
  mqttSubscribeTopic( TOPIC_Name, topicVar, NULL );
}
void mqttSubscribeTopic( char* TOPIC_Name, char* topicVar1, char* topicVar2  ) {
  char topic[63];
  mqttSubscribeTopicRaw( mqttTopic( topic, TOPIC_Name, topicVar1, topicVar2 ) );
}
void mqttSubscribeTopicRaw( char* topic ) {
  mqttClient.subscribe( topic );
}

// Wrappers to mqtt publish function
bool mqttPublish( char* TOPIC_Name, long value, bool retained ) {
  return mqttPublish( TOPIC_Name, NULL, NULL, value, retained );
}
bool mqttPublish( char* TOPIC_Name, char* topicVar, long value, bool retained ) {
  return mqttPublish( TOPIC_Name, topicVar, NULL, value, retained );
}
bool mqttPublish( char* TOPIC_Name, char* topicVar1, char* topicVar2, long value, bool retained ) {
  char topic[63];
  return mqttPublishRaw( mqttTopic( topic, TOPIC_Name, topicVar1, topicVar2 ), value, retained );
}

bool mqttPublishRaw( char* topic, long value, bool retained ) {
  char sValue[32];
  sprintf( sValue, "%u", value );
  return mqttPublishRaw( topic, sValue, retained );
}

bool mqttPublish( char* TOPIC_Name, char* value, bool retained ) {
  return mqttPublish( TOPIC_Name, NULL, NULL, value, retained );
}
bool mqttPublish( char* TOPIC_Name, char* topicVar, char* value, bool retained ) {
  return mqttPublish( TOPIC_Name, topicVar, NULL, value, retained );
}
bool mqttPublish( char* TOPIC_Name, char* topicVar1, char* topicVar2, char* value, bool retained ) {
  char topic[63];
  return mqttPublishRaw( mqttTopic( topic, TOPIC_Name, topicVar1, topicVar2 ), value, retained );
}

bool mqttPublishRaw( char* topic, char* value, bool retained ) {
  if( !mqttConnected() ) return false;
#ifdef Debug  
  aePrint(F("Publish ")); aePrint(topic); aePrint(F("="));  aePrintln(value );
#endif  
  return mqttClient.publish( topic, value, retained );
}

void triggerActivity() {
  mqttActivity = millis();
}

// Regiater Callback and Connect functions to call on MQTT events
void mqttRegisterCallbacks( MQTT_CALLBACK, MQTT_CONNECT ) {
  if( mqttCbsCount >= MQTT_CbsSize ) return;
  mqttCbs[mqttCbsCount].callback = callback;
  mqttCbs[mqttCbsCount].connect = connect;
  mqttCbsCount++;
}


// Internal proxy function to process "default" topics
void mqttCallbackProxy(char* topic, byte* payload, unsigned int length) {
  if( mqttDisableCallback ) return;

  for(int i=0; i<mqttCbsCount; i++ ) {
    if( mqttCbs[i].callback != NULL ) {
      if( mqttCbs[i].callback( topic, payload, length) ) return;
    }
  }

  if( mqttIsTopic( topic, TOPIC_Reset ) ) {
    aePrintln(F("MQTT: Resetting by request"));
    commsClearTopicAndRestart( TOPIC_Reset );

#ifndef WIFI_HostName
  } else if( mqttIsTopic( topic, TOPIC_SetName ) ) {
    if( (payload != NULL) && (length > 1) && (length<32) ) {
      char topic[63];
      mqttTopic(topic, TOPIC_Online);
      memset( commsConfig.hostName, 0, sizeof(commsConfig.hostName) );
      strncpy( commsConfig.hostName, ((char*)payload), length );
      aePrint(F("MQTT: Device name set to ")); aePrintln(commsConfig.hostName);
      
      mqttPublishRaw( topic, (long)0, true );
      commsClearTopicAndRestart( TOPIC_SetName );
    }
#endif
#ifndef MQTT_Root
  } else if( mqttIsTopic( topic, TOPIC_SetRoot ) ) {
    if( (payload != NULL) && (length > 3) && (length<63) ) {
      char topic[63];
      mqttTopic(topic, TOPIC_Online);
      strncpy( commsConfig.mqttRoot, ((char*)payload),length);
      commsConfig.mqttRoot[length]=0;
      aePrint(F("MQTT: Device root set to ")); aePrintln(commsConfig.mqttRoot);
      storageSave();
      mqttPublishRaw( topic, (long)0, true );
      commsClearTopicAndRestart( TOPIC_SetRoot );
    }
#endif
  } else if( mqttIsTopic( topic, TOPIC_EnableOTA ) ) {
    commsEnableOTA();
  }
  
}

//**************************************************************************
//                            Comms engine
//**************************************************************************

void commsLoop() {

  if( commsConfig.disabled ) return;
  
  unsigned long t = millis();
  // Check if connection is not timed out
  if (WiFi.status() == WL_CONNECTED) {  // WiFi is already connected
    if( commsConnecting >0 ) {
      commsConnecting = 0;
      aePrint(F("WIFI: Connected as ")); aePrint( WiFi.hostname() ); aePrint(F("/")); aePrintln( WiFi.localIP() );
      if( !MDNS.begin(commsConfig.hostName) ) {
        aePrintln(F("MDNQ: begin() failed"));
      }
      return; // Split activity to not overload loop
    }

    // Handle OTA things
    if( otaEnabled>0 ) {
      if( (unsigned long)(t - otaEnabled) < CONFIG_Timeout  ) {
        if( otaShouldInit ) {
          otaShouldInit = false;
          aePrintln(F("OTA: Enabling OTA"));
          ArduinoOTA.setHostname(commsConfig.hostName);
          ArduinoOTA.setPassword(WIFI_Password);
          ArduinoOTA.onStart([]() {
            mqttPublish( TOPIC_Online, (long)0, true );
//            aePrintln(F("OTA: Updating firmware"));
            storageSave();
            LittleFS.end();
          });
          ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
//            aePrint(F("."));
//            otaProgress++;
//            if(otaProgress>=100) {
//              otaProgress = 0; 
//              aePrintln();
//            }
          });
          ArduinoOTA.onEnd([]() {
            aePrintln(F("\nOTA: Firmware updated. Restarting"));
          });
          ArduinoOTA.onError([](ota_error_t error) {
            aePrintln(F("OTA: Error updating firmware. Restarting\r"));
            commsRestart();
          });      
          ArduinoOTA.begin();
          return;
        } else {
          ArduinoOTA.handle();
        }
      } else {
        mqttPublish( TOPIC_Online, (long)0, true );
        aePrintln(F("OTA: Timeout waiting for update. Restarting"));
        commsRestart();
      }
    }
    static bool wasConnected = false;
    
    // Handle MQTT connection and loops
    if( mqttClient.loop() ) {
      wasConnected = true;
      static bool activityReported = false;
      bool a = (mqttActivity != 0) && ((unsigned long)(t - mqttActivity) < MQTT_ActivityTimeout );
      if( (a != activityReported) && mqttPublish( TOPIC_Activity, a?1:0, false ) ) {
        activityReported = a;
      }

      if( (unsigned long)(t - commsRssiReported) > COMMS_RSSITimeout ) {
        char s[16];
        sprintf(s, "%d",(int)WiFi.RSSI());
        if( mqttPublish( TOPIC_RSSI, s, false ) ) commsRssiReported = t;
      }


    } else {
      if( wasConnected ) {
        aePrintln(F("MQTT: Connection lost"));
        wasConnected = false;
      }
      if( commsPaused == 0 ) {
        mqttClient.setServer( MQTT_Address, MQTT_Port);
        mqttClient.setCallback( mqttCallbackProxy );
        
        char willTopic[63];
        mqttTopic( willTopic, "" );
        aePrint(F("MQTT: Connecting as ")); aePrintln(willTopic);
        
        mqttTopic( willTopic, TOPIC_Online );
        if( mqttClient.connect( commsConfig.hostName, willTopic, 0, true, "0" ) ) {
          commsConnectAttempt = 0;
          aePrintln(F("MQTT: Connected"));
          // Subscribe
          mqttSubscribeTopic( TOPIC_Reset );
          mqttSubscribeTopic( TOPIC_EnableOTA );
#ifndef WIFI_HostName
          mqttSubscribeTopic( TOPIC_SetName );
#endif  

#ifndef MQTT_Root
          mqttSubscribeTopic( TOPIC_SetRoot );
#endif  
          mqttPublish( TOPIC_Online, (long)1, true );
#ifdef VERSION
          mqttPublish( TOPIC_Version, VERSION, true  );
#endif
          IPAddress ip = WiFi.localIP();
          sprintf( willTopic, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
          mqttPublish( TOPIC_Address, willTopic, true  );
          
          for(int i=0; i<mqttCbsCount; i++ ) {
            if( mqttCbs[i].connect != NULL ) mqttCbs[i].connect();
          }
          
          commsPaused = 0;
          wasConnected = true;
        } else {
          aePrint(F("MQTT: Connection error #")); aePrintln( mqttClient.state() );
          commsPaused = t;
        }
        return; // Split activity to not overload loop
      } else {
        if( (unsigned long)(t - commsPaused) > COMMS_ConnectTimeout ) {
          commsReconnect();
        }
      }
    }

  } else {
    if( (unsigned long)(t - commsConnecting) > COMMS_ConnectTimeout ) {
      aePrint(F("WIFI: Connection error #")); aePrintln(WiFi.status());
      commsReconnect();
    }
  }
}

bool commsOTAEnabled(){
  return (otaEnabled>0);
}
void commsEnableOTA() {
  aePrintln(F("OTA: Enabled"));
  if( otaEnabled == 0 ) {
    otaShouldInit = true;
  }
  otaEnabled = millis();
  Serial.end();
}

void commsRestart() {
  storageSave();
  aePrintln(F("Restarting device..."));
  mqttPublish( TOPIC_Online, (long)0, true );
  delay(1000);;
  ESP.restart();
}
void commsClearTopicAndRestart( char* topic) {
   commsClearTopicAndRestart( topic, NULL, NULL);
}
void commsClearTopicAndRestart( char* topic, char* topicVar1 ) {
   commsClearTopicAndRestart( topic, topicVar1, NULL);
}
void commsClearTopicAndRestart( char* topic, char* topicVar1, char* topicVar2 ) {
  mqttDisableCallback = true;
  mqttPublish( topic, topicVar1, topicVar2, (char*)NULL, false );
  commsRestart();
}


void commsInit() {
  otaEnabled = 0;
  otaShouldInit = false;
  commsPaused = 0;
  mqttActivity = 0;
  storageRegisterBlock( COMMS_StorageId, &commsConfig, sizeof(commsConfig) );

#ifdef WIFI_HostName
  uint8_t macAddr[6];
  char macS[16];
  WiFi.macAddress(macAddr);
  sprintf( macS, "%02X%02X%02X%02X%02X%02X", macAddr[0], macAddr[1], macAddr[2],macAddr[3], macAddr[4], macAddr[5]);
  sprintf( commsConfig.hostName, WIFI_HostName, macS);
#endif  

#ifdef MQTT_Root
  strcpy( commsConfig.mqttRoot, MQTT_Root );
#endif  
  
  commsConnect();
  registerLoop(commsLoop);
}
