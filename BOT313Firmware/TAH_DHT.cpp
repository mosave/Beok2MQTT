#include <Arduino.h>

#include "AELib.h"
#include "Comms.h"
#include "TAH.h"

#ifdef TAH_DHT
#include <DHTesp.h> // beegee-tokyo DHTesp: https://github.com/beegee-tokyo/DHTesp

#ifndef DHT_Pin
  #define DHT_Pin 0
#endif

static char* TOPIC_TAHValid PROGMEM = "Sensors/TAHValid";
static char* TOPIC_Temperature PROGMEM = "Sensors/Temperature";
static char* TOPIC_Humidity PROGMEM = "Sensors/Humidity";
static char* TOPIC_AbsHumidity PROGMEM = "Sensors/AbsHumidity";
static char* TOPIC_HeatIndex PROGMEM = "Sensors/HeatIndex";

#define DetectionTimeout ((unsigned long)(15*1000))
#define ValidityTimeout ((unsigned long)(30*1000))
#define TemperatureAccuracy (float)0.25
#define HumidityAccuracy 1.9

DHTesp dht;

float tahHumidity;
float tahTemperature;
bool tahSensorFound;
unsigned long tahUpdatedOn = 0;
unsigned long tahDetection = 0;

float tahGetTemperature() {
  return tahTemperature;
}
float tahGetHumidity() {
  return tahHumidity;
}

float tahGetHeatIndex() {
  return dht.computeHeatIndex(tahTemperature, tahHumidity, false);
}

float tahGetAbsHumidity() {
  return 6.112*pow(2.71828,(17.67*tahTemperature)/(tahTemperature+243.5))*tahHumidity*2.1674/(275.15+tahTemperature);
}

int tahGetPressure() {
    return 0;
}

int tahGetCO2() {
    return 0;
}

int tahGetVOC() {
    return 0;
}

void publishStatus() {
  if( !mqttConnected() ) return;
  static int _valid = -1;
  int valid = ((unsigned long)(millis() - tahUpdatedOn) < ValidityTimeout ) ? 1 : 0;
  if( valid != _valid ) {
    if( mqttPublish( TOPIC_TAHValid, valid, true ) ) _valid = valid;
  }
  if( valid==0 ) return;
  
  char b[31];
  bool hindex = false;
  float delta;
  
  static float _temperature = -1000;
  delta = tahTemperature - _temperature;  if(delta<0) delta = -delta;
  if( delta > TemperatureAccuracy ){
    hindex = true;
    dtostrf( tahTemperature, 0, 1, b );
    if( mqttPublish( TOPIC_Temperature, b, true ) ) _temperature = tahTemperature;
  }

  static float _humidity = -1000;
  delta = tahHumidity - _humidity;  if(delta<0) delta = -delta;
  if( delta > HumidityAccuracy ){
    hindex = true;
    if( mqttPublish( TOPIC_Humidity, (int)tahHumidity, true ) ) _humidity = tahHumidity;
  }
  
  if( hindex ) {
    dtostrf( tahGetHeatIndex(), 0, 1, b );
    mqttPublish( TOPIC_HeatIndex, b, true );
    float absHumidity = tahGetAbsHumidity();
    dtostrf( ((float)((int)(absHumidity*5)))/5.0, 0, 1, b );
    mqttPublish( TOPIC_AbsHumidity, b, true );
  }
}

bool tahAvailable() {
  return tahSensorFound;
}


void tahLoop() {
  if( (tahDetection == 0) && !tahSensorFound ) {
    static bool reported = false;
    if( !reported && mqttPublish( TOPIC_TAHValid, (long)0, true ) ) reported = true;
    return;
  }

  unsigned long t = millis();
  static unsigned long checkedOn;
  
  if( (unsigned long)(t - checkedOn) > (unsigned long)2500 ) {
    checkedOn = t;
    TempAndHumidity tah = dht.getTempAndHumidity();
    if( dht.getStatus() == DHTesp::ERROR_NONE ) {
      //aePrintf("t=%f, h=%f hindex=%f\n", tahTemperature, tahHumidity, dht.computeHeatIndex(tahTemperature, tahHumidity, false));
      tahTemperature = tah.temperature;
      tahHumidity = tah.humidity;
      tahUpdatedOn = t;
      if( tahDetection>0 ) {
        aePrintln(F("DHT sensor found"));
        tahDetection = 0;
        tahSensorFound = true;
      }
      publishStatus();
    } else {
      if( (tahDetection>0) && (unsigned long)(t - tahDetection) > DetectionTimeout ) {
        tahDetection = 0;
        aePrintln(F("DHT sensor not found"));
      } else  if( tahSensorFound ) {
        publishStatus();
        aePrintf("DHT error: %d\n", dht.getStatus());
      }
    }
  }
}


void tahInit() {
  tahSensorFound = false;
  if( DHT_Pin>0 ) {
    dht.setup( DHT_Pin, DHTesp::DHT22 );
    aePrintln(F("Detecting DHT sensor"));
    tahDetection = millis();
  } else {
    tahDetection = 0;
  }
  aeRegisterLoop( tahLoop );
}
#endif