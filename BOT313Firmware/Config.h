#ifndef config_h
#define config_h
#include <Arduino.h>

// Use either HTU21D or SCD4x based sensor (temperature / humidity / CO2 sensors)
//#define TAH_HTU21D
#define TAH_SCD4x

#define VERSION_PREFIX "BEOK2MQTT v1.09"
#ifdef TAH_HTU21D
#define VERSION VERSION_PREFIX " [HTU21]"
#define USE_HTU
#else
#ifdef TAH_SCD4x
#define VERSION VERSION_PREFIX " [SCD4x]"
#define USE_HTU
#else
#define VERSION VERSION_PREFIX
#endif
#endif


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


// Define this to disable AUTO thermostat mode.
// All scheduling support will be disabled as well
#define MANUAL_MODE_ONLY

// Define this to autosynchronize time if NTP server is available.
// Check "tz.h" for timezone constants
#define TIMEZONE TZ_Europe_Moscow

#ifndef WIFI_SSID
#include "Config.AE.h"
#endif

//#define USE_SOFT_SERIAL

#ifndef USE_SOFT_SERIAL
// D6
#define THERM_RX 12
// D7
#define THERM_TX 13
#endif

#define aePrintf( ... )
#define aePrint( ... )
#define aePrintln( ... )

#ifdef USE_HTU
#define SDA_Pin 0
#define SCL_Pin 2
#endif

#endif
