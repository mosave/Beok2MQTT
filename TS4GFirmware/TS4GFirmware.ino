// ESP-01S module:
// Board: ESP8266 generic (Chip is ESP8266EX)
// Crystal is 26MHz
// Flash size: 1MB, FS:None, OTA:502KB
// Erase Flash: ALL content

#include <stdarg.h>
#include "AELib.h"
#include "Comms.h"
#include "Thermostat.h"

#ifdef USE_HTU21D
  #include <Wire.h>
  #include "TAH.h"
#endif

static char* TOPIC_SendCommand PROGMEM = "SendCommand";


//*****************************************************************************************
// MQTT support
//*****************************************************************************************
void mqttConnect() {
#ifdef THERM_SEND_COMMAND  
  mqttSubscribeTopic( TOPIC_SendCommand );
#endif
}

bool mqttCallback(char* topic, byte* payload, unsigned int length) {
  
#ifdef THERM_SEND_COMMAND
  if( mqttIsTopic( topic, TOPIC_SendCommand ) ) {
    if( (payload != NULL) && (length > 0) && (length<250) ) {
      char b[255];
      memset( b, 0, sizeof(b) );
      strncpy( b, ((char*)payload), length );
      thermSendMessage( b, true );
    }
    return true;
  }
#endif  
  return false;
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

  aeInit();
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
  aeLoop();
  delay(10);
}
