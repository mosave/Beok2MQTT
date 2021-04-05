// Tuya module configuration to reflash:
// Board: ESP8266 generic (Chip is ESP8266EX)
// Crystal is 26MHz
// Flash size: 1MB
// Erase Flash: ALL content

#include <stdarg.h>
#include "Config.h"
#include "Storage.h"
#include "Comms.h"
#include "Thermostat.h"

#ifdef USE_HTU21D
  #include <Wire.h>
  #include "tah_htu21d.h"
#endif

static char* TOPIC_State PROGMEM = "State";
static char* TOPIC_SendCommand PROGMEM = "SendCommand";


//*****************************************************************************************
// MQTT support
//*****************************************************************************************
void mqttConnect() {
#ifdef THERM_DEBUG  
  mqttSubscribeTopic( TOPIC_SendCommand );
#endif
}

bool mqttCallback(char* topic, byte* payload, unsigned int length) {
  //printf("mqttCallback(\"%s\", %u, %u )\r\n", topic, payload, length);
  //char s[63];
  //print("Payload=");
  //if( payload != NULL ) { println( (char*)payload ); } else { println( "empty" );}
  
#ifdef THERM_DEBUG  
  if( mqttIsTopic( topic, TOPIC_SendCommand ) ) {
    if( (payload != NULL) && (length > 0) && (length<250) ) {
      char b[255];
      memset( b, 0, sizeof(b) );
      strncpy( b, ((char*)payload), length );
      thermSendMessage( b );
    }
    return true;
  }
#endif  
  return false;
}

void publishState() {
  if( !mqttConnected() ) return;

  unsigned long t=millis();
  char s[127];

}


//*****************************************************************************************
// Setup
//*****************************************************************************************
void setup() {
#ifdef USE_SOFT_SERIAL
  Serial.begin(115200);
  delay(500); 
#endif  
  aePrintln();  aePrintln("Initializing");

  storageInit();
  commsInit();
#ifdef USE_HTU21D
  Wire.begin(SDA_Pin, SCL_Pin);
  tahInit();
#endif
  mqttRegisterCallbacks( mqttCallback, mqttConnect );

  thermInit();
  //commsEnableOTA();
}

void loop() {
  Loop();
  publishState();
  delay(10);
}
