#include <Arduino.h>

#include "AELib.h"
#include "Comms.h"
#include "TAH.h"
#ifdef TAH_SCD4x

// https://github.com/Sensirion/arduino-i2c-scd4x
#include <SensirionCore.h>
#include <SensirionI2cScd4x.h>
//#define TAH_DBG

static char* TOPIC_TAHValid PROGMEM = "Sensors/TAHValid";
static char* TOPIC_Temperature PROGMEM = "Sensors/Temperature";
static char* TOPIC_Humidity PROGMEM = "Sensors/Humidity";
static char* TOPIC_HeatIndex PROGMEM = "Sensors/HeatIndex";
static char* TOPIC_AbsHumidity PROGMEM = "Sensors/AbsHumidity";
static char* TOPIC_CO2 PROGMEM = "Sensors/CO2";

#define ValidityTimeout ((unsigned long)(30*1000))

//Create an instance of the object
SensirionI2cScd4x tahSensor;

uint64_t tahSerialNumber;
float tahHumidity;
float tahTemperature;
uint16_t tahCO2;

float tahHumidityAdj;
float tahTemperatureAdj;

unsigned long tahUpdatedOn = 0;

bool tahAvailable() {
	return (tahUpdatedOn > 0) && (((unsigned long)(millis() - tahUpdatedOn) < ValidityTimeout));
}

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

float tahAbsHumidity() {
	return 6.112 * pow(2.71828, (17.67 * tahTemperature) / (tahTemperature + 243.5)) * tahHumidity * 2.1674 / (275.15 + tahTemperature);
}

float tahGetTemperature() {
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
int tahGetPressure() {
	return 0;
}
int tahGetCO2() {
	return tahCO2;
}
int tahGetVOC() {
    return 0;
}


void tahPublishStatus() {
	static int _valid = -1;
	static float _temperature = -1000;
	static float _humidity = -1000;
	static float _co2 = -1000;
	if (!mqttConnected()) {
		_valid = -1;
		_temperature = -1000;
		_humidity = -1000;
		_co2 = -1000;
		return;
	}

	int valid = tahAvailable() ? 1 : 0;
	if (valid != _valid) {
		if (mqttPublish(TOPIC_TAHValid, valid, true)) _valid = valid;
	}
	if (valid == 0) return;

	char b[31];
	bool hindex = false;
	float delta;

	delta = tahTemperature - _temperature;  if (delta < 0) delta = -delta;

	if (delta > 0.2) {
		hindex = true;
		dtostrf(((float)((int)(tahTemperature * 5))) / 5.0, 0, 1, b);
		if (mqttPublish(TOPIC_Temperature, b, true)) _temperature = tahTemperature;
	}

	delta = tahHumidity - _humidity;  if (delta < 0) delta = -delta;
	if (delta > 1.4) {
		hindex = true;
		if (mqttPublish(TOPIC_Humidity, (int)tahHumidity, true)) _humidity = tahHumidity;
	}

	if ((abs(tahCO2 - _co2) > 10) && mqttPublish(TOPIC_CO2, (int)tahCO2, true)) {
		_co2 = tahCO2;
	}

	if (hindex) {
		dtostrf(((float)((int)(tahHeatIndex() * 2))) / 2.0, 0, 1, b);
		mqttPublish(TOPIC_HeatIndex, b, true);
		dtostrf(((float)((int)(tahAbsHumidity() * 2))) / 2.0, 0, 1, b);
		mqttPublish(TOPIC_AbsHumidity, b, true);
	}
}

bool tahIsError(int16_t resultCode, char* operation, bool errorsOnly = false) {
#ifndef TAH_DBG
	errorsOnly = true;
#endif
	if ((resultCode == 0) && errorsOnly) {
		return false;
	}

	aePrint("SCD4x: ");
	aePrint(operation);
	aePrint(": ");
	if (resultCode == 0) {
		aePrintln("OK");
		return false;
	}
	char errorMessage[64];
	errorToString(resultCode, errorMessage, sizeof(errorMessage));
	aePrintln(errorMessage);
	return true;
}

void tahLoop() {
	unsigned long t = millis();
	static unsigned long delay = 1000;
	static unsigned long checkedOn;
	bool dataIsReady;

	if (timedOut(t, checkedOn, delay)) {
		checkedOn = t;
		if (tahIsError(tahSensor.getDataReadyStatus(dataIsReady), "reading data readiness", true)) {
			return;
		}
		if (!dataIsReady) {
			delay = 500;
			return;
		}
		delay = 29500;

		uint16_t co2;
		float temperature;
		float humidity;
		if (tahIsError(tahSensor.readMeasurement(co2, temperature, humidity), "reading data", true)) {
			return;
		}
		if( co2>200 && co2<10000 ) tahCO2 = co2;
		tahTemperature = temperature + tahTemperatureAdj;
		tahHumidity = humidity + tahHumidityAdj;
		tahUpdatedOn = t;
		// aePrintf("SCD4x: co2=%uppm, t=%f, h=%f\r\n", co2, temperature, humidity);

		tahPublishStatus();
	}
}


void tahInit() {
	tahSensor.begin(Wire, SCD41_I2C_ADDR_62);

	if (tahIsError(tahSensor.wakeUp(), "waking up") ||
		tahIsError(tahSensor.stopPeriodicMeasurement(), "stopping measurements") ||
		tahIsError(tahSensor.reinit(), "re-initializing") ||
		tahIsError(tahSensor.getSerialNumber(tahSerialNumber), "reading serial") ||
		// tahIsError(tahSensor.startPeriodicMeasurement(), "starting measurements")
		tahIsError(tahSensor.startLowPowerPeriodicMeasurement(), "starting low power measurements")
		) {
		return;
	}
	aePrint("SCD4x serial number: ");
	aePrintln(tahSerialNumber);
	aeRegisterLoop(tahLoop);
}
#endif