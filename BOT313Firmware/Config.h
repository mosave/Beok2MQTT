#ifndef config_h
#define config_h
#include <Arduino.h>

#define VERSION "1.0"
#define MQTT_MAX_PACKET_SIZE 512
#define mqtt_max_packet_size 512

// Define this to enable SendCommand topic and extra debug output
//#define THERM_DEBUG

#define LED_Pin 2

// Device host name and MQTT ClientId, "%s" to be replaced with MAC address
// Disables SetName topic processing
#define WIFI_HostName "Therm_%s"

//#define WIFI_SSID "SSID"
//#define WIFI_Password  "Password"
//#define MQTT_Address "1.1.1.33"
//#define MQTT_Port 1883

#ifndef WIFI_SSID
    #include "Config.AE.h"
#endif

//#define USE_SOFT_SERIAL

#ifdef USE_SOFT_SERIAL
  #define aePrintf( ... ) Serial.printf( __VA_ARGS__ )
  #define aePrint( ... ) Serial.print( __VA_ARGS__ )
  #define aePrintln( ... ) Serial.println( __VA_ARGS__ )

  // D6
  #define THERM_RX 12
  // D7
  #define THERM_TX 13
#else
  #define aePrintf( ... )
  #define aePrint( ... )
  #define aePrintln( ... )
#endif


#define LOOP std::function<void()>

void registerLoop( LOOP loop );
void Loop();

#endif
