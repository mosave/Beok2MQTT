#include <Arduino.h>

#include "AELib.h"
#include "Comms.h"
#include "TAH.h"
#ifdef TAH_BME68x

// // https://github.com/styropyr0/BME688
// #.include <BME688.h>

#include "bme68xLibrary.h"

//#define TAH_DBG

static char* TOPIC_TAHValid PROGMEM = "Sensors/TAHValid";
static char* TOPIC_Temperature PROGMEM = "Sensors/Temperature";
static char* TOPIC_Humidity PROGMEM = "Sensors/Humidity";
static char* TOPIC_HeatIndex PROGMEM = "Sensors/HeatIndex";
static char* TOPIC_AbsHumidity PROGMEM = "Sensors/AbsHumidity";
static char* TOPIC_Pressure PROGMEM = "Sensors/Pressure";
static char* TOPIC_IAQ PROGMEM = "Sensors/IAQ";

#define ValidityTimeout ((unsigned long)(30*1000))

Bme68x tahSensor;

float tahHumidity;
float tahTemperature;
float tahPressure;
float tahIAQ;

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
	return tahPressure;
}
int tahGetCO2() {
	return 0;
}
int tahGetVOC() {
	return tahIAQ;
}


void tahPublishStatus() {
	static int _valid = -1;
	static float _temperature = -1000;
	static float _humidity = -1000;
	static float _pressure = -1000;
	static float _iaq = -1000;
	if (!mqttConnected()) {
		_valid = -1;
		_temperature = -1000;
		_humidity = -1000;
		_iaq = -1000;
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

	delta = tahPressure - _pressure;  if (delta < 0) delta = -delta;
	if (delta > 1.4) {
		if (mqttPublish(TOPIC_Pressure, (int)tahPressure, true)) _pressure = tahPressure;
	}

	delta = tahIAQ - _iaq;  if (delta < 0) delta = -delta;
	if (delta > 3) {
		if (mqttPublish(TOPIC_IAQ, (round(tahIAQ/5))*5.0, true)) _iaq = tahIAQ;
	}

	if (hindex) {
		dtostrf(((float)((int)(tahHeatIndex() * 2))) / 2.0, 0, 1, b);
		mqttPublish(TOPIC_HeatIndex, b, true);
		dtostrf(((float)((int)(tahAbsHumidity() * 2))) / 2.0, 0, 1, b);
		mqttPublish(TOPIC_AbsHumidity, b, true);
	}
}

void tahLoop() {
	unsigned long t = millis();
	static byte mode = 0;
	static unsigned long delay = 5000;
	static unsigned long checkedOn;
	bool dataIsReady;

	if (timedOut(t, checkedOn, delay)) {
		checkedOn = t;
		switch (mode) {
		case 0: // idle
			// Force measures
			tahSensor.setOpMode(BME68X_FORCED_MODE);
			// Measure delay time
			delay = tahSensor.getMeasDur() / 1000 + 100;
			mode = 1;
			break;
		case 1: // Measure should be ready
			if (tahSensor.fetchData()) {
				tahUpdatedOn = t;
				bme68xData data;
				tahSensor.getData(data);
			tahTemperature = data.temperature; // degrees Celsius
			tahHumidity = data.humidity; // percent
			tahPressure = data.pressure * 0.00750062;  // mmHg

				/*
				aePrint("p.comp "); aePrint((101325.0 / data.pressure));
				aePrint(" h.comp "); aePrint((1.0 + 0.0008 * (data.humidity - 50) * (data.humidity - 50)));
				aePrint(" t.comp "); aePrint((1.0 + 0.003 * (data.temperature - 22.5)));
				aePrintln();
				*/

				double gas_compensated = data.gas_resistance
					// Normalize by pressure
					* (101325.0 / data.pressure)
					// Humidity compensation (optimal ~40 - 60 %)
					* (1.0 + 0.0008 * (data.humidity - 50) * (data.humidity - 50))
					// Temperature compensation (optimal ~20 - 25 C)
					* (1.0 + 0.003 * (data.temperature - 22.5));

				double gas_clean = 80000;  // Calibrate for your location
				// Air quality index (higher resistance = better air quality)
				tahIAQ = (gas_clean / gas_compensated) * 100;
				if (tahIAQ < 0) tahIAQ = 0;
				if (tahIAQ > 500) tahIAQ = 500;

				/*
				aePrint("Temperature "); aePrint(tahTemperature);
				aePrint(", pressure "); aePrint(tahPressure);
				aePrint(", humidity "); aePrint(tahHumidity);
				aePrint(", gas "); aePrint(data.gas_resistance);
				aePrint(", gas_compensated "); aePrint(gas_compensated);
				aePrint(", iaq "); aePrint(iaq);
				aePrintln();
				*/
				delay = 1000;
				mode = 0;
			} else {
				//aePrintln("DATA NOT READY");
				delay = 10;
			}
			tahPublishStatus();
		}
	}
}

void tahInit() {
#ifdef SDA
#ifdef SCL
	Wire.begin( SDA, SCL );
#else
	SCL not defined!
#endif
#else
	Wire.begin();
#endif

	tahSensor.begin(0x77, Wire);

	if (tahSensor.checkStatus()) {
		if (tahSensor.checkStatus() == BME68X_ERROR) {
			aePrintln("Sensor error:" + tahSensor.statusString());
			return;
		} else if (tahSensor.checkStatus() == BME68X_WARNING) {
			aePrintln("Sensor Warning:" + tahSensor.statusString());
		}
	}

	/* Set the default configuration for temperature, pressure and humidity */
	tahSensor.setTPH();

	// Forced mode
	/* Set the heater configuration to 300 deg C for 100ms for Forced mode */
	tahSensor.setHeaterProf(300, 100);

	// Parallel mode:
	///* Heater temperature in degree Celsius */
	//uint16_t tempProf[10] = { 320, 100, 100, 100, 200, 200, 200, 320, 320, 320 };
	///* Multiplier to the shared heater duration */
	//uint16_t mulProf[10] = { 5, 2, 10, 30, 5, 5, 5, 5, 5, 5 };
	///* Shared heating duration in milliseconds */
	//uint16_t sharedHeatrDur = MEAS_DUR - (tahSensor.getMeasDur(BME68X_PARALLEL_MODE) / 1000);
	//tahSensor.setHeaterProf(tempProf, mulProf, sharedHeatrDur, 10);
	//tahSensor.setOpMode(BME68X_PARALLEL_MODE);

	aeRegisterLoop(tahLoop);
}
#endif
