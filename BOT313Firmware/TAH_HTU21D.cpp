#include <Arduino.h>
#include <EEPROM.h>
#include "Config.h"
#include "Comms.h"
#include "SparkFunHTU21D.h" // SparkFun HTU21D library: https://github.com/sparkfun/SparkFun_HTU21D_Breakout_Arduino_Library
#include "TAH_HTU21D.h"

static char* TOPIC_TAHValid PROGMEM = "Sensors/TAHValid";
static char* TOPIC_Temperature PROGMEM = "Sensors/Temperature";
static char* TOPIC_Humidity PROGMEM = "Sensors/Humidity";
static char* TOPIC_HeatIndex PROGMEM = "Sensors/HeatIndex";
static char* TOPIC_AbsHumidity PROGMEM = "Sensors/AbsHumidity";

#define ValidityTimeout ((unsigned long)(30*1000))

//Create an instance of the object
HTU21D tahSensor;

float tahHumidity;
float tahTemperature;
unsigned long tahUpdatedOn = 0;

bool tahAvailable() {
  return (tahUpdatedOn>0) && (((unsigned long)(millis() - tahUpdatedOn) < ValidityTimeout ));
}

//float toFahrenheit(float temp) { return 1.8 * temp + 32.0; };
//float toCelsius(float temp) { return (temp - 32.0) / 1.8; };

float tahHeatIndex() {
  float hi;
  // to farenheit
  float temperature = 1.8 * tahTemperature + 32.0;
  hi = 0.5 * (temperature + 61.0 + ((temperature - 68.0) * 1.2) + (tahHumidity * 0.094));

  if (hi > 79) {
    hi = -42.379 +
      2.04901523 * temperature +
      10.14333127 * tahHumidity +
      -0.22475541 * temperature * tahHumidity +
      -0.00683783 * pow(temperature, 2) +
      -0.05481717 * pow(tahHumidity, 2) +
      0.00122874 * pow(temperature, 2) * tahHumidity +
      0.00085282 * temperature * pow(tahHumidity, 2) +
      -0.00000199 * pow(temperature, 2) * pow(tahHumidity, 2);

    if ((tahHumidity < 13) && (temperature >= 80.0) && (temperature <= 112.0)) {
      hi -= ((13.0 - tahHumidity) * 0.25) * sqrt((17.0 - abs(temperature - 95.0)) * 0.05882);
    } else if ((tahHumidity > 85.0) && (temperature >= 80.0) && (temperature <= 87.0)) {
      hi += ((tahHumidity - 85.0) * 0.1) * ((87.0 - temperature) * 0.2);
    }
  }
  return (hi - 32.0) / 1.8;
}
float tahAbsHumidity(){
  return 6.112*pow(2.71828,(17.67*tahTemperature)/(tahTemperature+243.5))*tahHumidity*2.1674/(275.15+tahTemperature);
}

float tahGetTemperature(){
  return tahTemperature;
}
float tahGetHumidity() {
  return tahHumidity;
}
float tahGetHeatIndex() {
  return tahHeatIndex();
}
float tahGetAbsHumidity() {
  return tahAbsHumidity();
}

void tahPublishStatus() {
  if( !mqttConnected() ) return;

  static int _valid = -1;
  int valid = tahAvailable() ? 1 : 0;
  if( valid != _valid ) {
    if( mqttPublish( TOPIC_TAHValid, valid, true ) ) _valid = valid;
  }
  if( valid==0 ) return;
  
  char b[31];
  bool hindex = false;
  float delta;

  static float _temperature = -1000;
  delta = tahTemperature - _temperature;  if(delta<0) delta = -delta;

  //aePrintf("t=%f, _t=%f, delta=%f\n", tahTemperature, _temperature, delta );

  if( delta > 0.55 ){
    hindex = true;
    dtostrf( ((float)((int)(tahTemperature*2)))/2.0, 0, 1, b );
    if( mqttPublish( TOPIC_Temperature, b, true ) ) _temperature = tahTemperature;
  }

  static float _humidity = -1000;
  delta = tahHumidity - _humidity;  if(delta<0) delta = -delta;
  if( delta > 1.4 ){
    hindex = true;
    if( mqttPublish( TOPIC_Humidity, (int)tahHumidity, true ) ) _humidity = tahHumidity;
  }
  
  if( hindex ) {
    dtostrf( ((float)((int)(tahHeatIndex()*2)))/2.0, 0, 1, b );
    mqttPublish( TOPIC_HeatIndex, b, true );
    dtostrf( ((float)((int)(tahAbsHumidity()*2)))/2.0, 0, 1, b );
    mqttPublish( TOPIC_AbsHumidity, b, true );
  }
}

void tahLoop() {
  unsigned long t = millis();
  static unsigned long checkedOn;
  
  if( (unsigned long)(t - checkedOn) > (unsigned long)1000 ) {
    checkedOn = t;
    float humidity = tahSensor.readHumidity();
    float temperature = tahSensor.readTemperature();

    if( (humidity < 990) && (temperature<990) ) {
      tahTemperature = temperature;
      tahHumidity = humidity;

      //aePrintf("t=%f, h=%f\n", tahTemperature, tahHumidity );
      tahUpdatedOn = t;
    }
    tahPublishStatus();
  }
}


void tahInit() {
  tahSensor.begin();
  registerLoop( tahLoop );
}
