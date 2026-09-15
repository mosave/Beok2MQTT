#ifndef config_h
#define config_h
#include <Arduino.h>

#define VERSION "BEOK2MQTT/Tuya v1.05"
#ifdef MQTT_MAX_PACKET_SIZE
    #undef MQTT_MAX_PACKET_SIZE
#endif
#define MQTT_MAX_PACKET_SIZE 512

/// Define this to enable extra debug output & Log topic
//#define THERM_DEBUG
/// Define this to enable SendCommand topic
//#define THERM_SEND_COMMAND

/// Device host name and MQTT ClientId, "%s" to be replaced with MAC address
/// Disables SetName topic processing
// #define WIFI_HostName "Therm_%s"

// Define this to use external THU21D based sensor (temperature & humidity)
#define USE_TAH
#define TAH_HTU21D

// Define this to autosynchronize time if NTP server is available.
// Check "tz.h" for timezone constants
#define TIMEZONE TZ_Europe_Moscow

#ifndef WIFI_SSID
    #include "Config.AE.h"
#endif

//#define USE_SOFT_SERIAL

#ifdef USE_SOFT_SERIAL
  // D6
  #define THERM_RX 12
  // D7
  #define THERM_TX 13
#else
  #define aePrintf( ... )
  #define aePrint( ... )
  #define aePrintln( ... )
#endif

#ifdef USE_TAH
  #define SDA_Pin 0
  #define SCL_Pin 2
#endif

#endif
