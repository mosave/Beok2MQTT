#include <Arduino.h>
#include <errno.h>
#include "AELib.h"
#include "Comms.h"
#include "Thermostat.h"

#ifdef USE_TAH
#include "TAH.h"
#endif
#pragma region Constants

#ifdef TIMEZONE
#include <time.h>
#endif

#define P3(str) ((char*)(((uint)str)+3))

static char* TOPIC_SetLocked PROGMEM = "SetLocked";
#define TOPIC_Locked P3(TOPIC_SetLocked)

static char* TOPIC_SetInverted PROGMEM = "SetInverted";
#define TOPIC_Inverted P3(TOPIC_SetInverted)

static char* TOPIC_SetSound PROGMEM = "SetSound";
#define TOPIC_Sound P3(TOPIC_SetSound)

static char* TOPIC_SetPower PROGMEM = "SetPower";
#define TOPIC_Power P3(TOPIC_SetPower)

static char* TOPIC_SetAntiFroze PROGMEM = "SetAntiFroze";
#define TOPIC_AntiFroze P3(TOPIC_SetAntiFroze)

static char* TOPIC_SetBrightness PROGMEM = "SetBrightness";
#define TOPIC_Brightness P3(TOPIC_SetBrightness)

static char* TOPIC_Heating PROGMEM = "Heating";
static char* TOPIC_RoomTemp PROGMEM = "RoomTemp";
static char* TOPIC_FloorTemp PROGMEM = "FloorTemp";

static char* TOPIC_SetFloorTempMax PROGMEM = "SetFloorTempMax";
#define TOPIC_FloorTempMax P3(TOPIC_SetFloorTempMax)

static char* TOPIC_SetTargetTemp PROGMEM = "SetTargetTemp";
#define TOPIC_TargetTemp P3(TOPIC_SetTargetTemp)

static char* TOPIC_SetTargetTempMax PROGMEM = "SetTargetTempMax";
#define TOPIC_TargetTempMax P3(TOPIC_SetTargetTempMax)

static char* TOPIC_SetSensor PROGMEM = "SetSensor";
#define TOPIC_Sensor P3(TOPIC_SetSensor)

static char* TOPIC_SetHysteresis PROGMEM = "SetHysteresis";
#define TOPIC_Hysteresis P3(TOPIC_SetHysteresis)

static char* TOPIC_SetAdjTemp PROGMEM = "SetAdjTemp";
#define TOPIC_AdjTemp P3(TOPIC_SetAdjTemp)

static char* TOPIC_SensorAdjTempOn PROGMEM = "Sensors/AdjTempOn";
static char* TOPIC_SetSensorAdjTempOn PROGMEM = "Sensors/SetAdjTempOn";
static char* TOPIC_SensorAdjTempOff PROGMEM = "Sensors/AdjTempOff";
static char* TOPIC_SetSensorAdjTempOff PROGMEM = "Sensors/SetAdjTempOff";

static char* TOPIC_HAction PROGMEM = "HAction";
static char* TOPIC_SetHAMode PROGMEM = "SetHAMode";
#define TOPIC_HAMode P3(TOPIC_SetHAMode)

static char* HAMODE_Off PROGMEM = "off"; // 0 
static char* HAMODE_Heat PROGMEM = "heat"; // 1

static char* HACTION_Off PROGMEM = "off"; // 0
static char* HACTION_Idle PROGMEM = "idle"; // 1
static char* HACTION_Heating PROGMEM = "heating"; // 2

static char* TOPIC_SetAutoAdjMode PROGMEM = "SetAutoAdjMode";
static char* TOPIC_AutoAdjMode P3(TOPIC_SetAutoAdjMode);



char* HAMODE(int haMode) {
	return
		(haMode == 1) ? HAMODE_Heat :
		HAMODE_Off;
}
char* HACTION(int hAction) {
	return
		(hAction == 2) ? HACTION_Heating :
		(hAction == 1) ? HACTION_Idle :
		HACTION_Off;
}
#pragma endregion

#pragma region Types and Vars
enum ThermWiFiState {
	Off,
	BlinkFast,
	Blink,
	On
};

ThermConfig thermConfig;
ThermWiFiState thermWiFiState = ThermWiFiState::Off;

unsigned char thermData[64];
int thermDataLen = 0;

// Current thermostat state
ThermState thermState;

unsigned long thermLastMessage = 0;
unsigned long thermActivityDetectionLocked = 0;
bool thermDisabled = false;

#ifdef USE_SOFT_SERIAL
#include <SoftwareSerial.h>
SoftwareSerial therm(THERM_RX, THERM_TX);
#else
#define therm Serial
#endif

bool thermStateIsValid() {
	return (thermState.roomTemp != UNKNOWN);
}

void thermInvalidate(ThermState* state) {
	state->brightness = UNKNOWN;
	state->inverted = UNKNOWN;
	state->locked = UNKNOWN;
	state->power = UNKNOWN;
	state->heating = UNKNOWN;
	state->sound = UNKNOWN;

	state->roomTemp = UNKNOWN;
	state->targetTemp = UNKNOWN;
	state->targetTempMax = UNKNOWN;

	state->floorTemp = UNKNOWN;
	state->floorTempMax = UNKNOWN;

	state->sensor = UNKNOWN;
	state->hysteresis = UNKNOWN;
	state->adjTemp = UNKNOWN;
	state->antiFroze = UNKNOWN;
}
#pragma endregion

#pragma region Message Sending
void thermTriggerActivity() {
	if (timedOut(millis(), thermActivityDetectionLocked, 5000)) {
		triggerActivity();
	}
}

void thermSendMessage(const char* data, bool blockActivityDetection) {
	if (blockActivityDetection) {
		thermActivityDetectionLocked = millis();
	}
	char hex[3] = { 0,0,0 };
	char* p = (char*)data;
	uint8_t crc = 0;
	while (*p > '\0') {
		hex[0] = *p; p++;
		hex[1] = *p; p++;
		while (*p == ' ') p++;
		uint8_t d = strtoul(hex, NULL, 16);
		therm.write(d);
		crc += d;
	}

	therm.write(crc);

#ifdef THERM_DEBUG
	char s[256];
	sprintf(s, "> %s:%02x\n", data, crc);
	mqttPublish("Log", s, false);
	aePrintf(s);
#endif
	delay(50);
	thermLastMessage = millis();
}

// 0: off
// 1: fast blink
// 2: slow blink
// 3: on
void thermSetWiFiSign(ThermWiFiState wifiState) {
	char data[64];
	if (wifiState == thermWiFiState) return;

	thermWiFiState = wifiState;
	uint8 mode =
		(wifiState == ThermWiFiState::BlinkFast) ? 2 :
		(wifiState == ThermWiFiState::Blink) ? 3 :
		(wifiState == ThermWiFiState::On) ? 4 :
		0;
	static char* cmd PROGMEM = "55aa00030001%02x";
	sprintf(data, cmd, mode);
	thermSendMessage(data, false);
}

/// <summary>
/// Set brightness level 0(black screen) .. 3 (full brightness)
/// </summary>
/// <param name="brightness">0..3</param>
void thermSetBrightness(char brightness) {
	char data[32];
	brightness = brightness < 0 ? 0 : brightness > 3 ? 3 : brightness;
	if ((int)brightness == (int)thermState.brightness) return;
	thermState.brightness = brightness;
	static char* cmd PROGMEM = "55aa000600056a040001%02x";
	sprintf(data, cmd, brightness);
	thermSendMessage(data, true);
}

void thermSetTargetTemp(float targetTemp) {
	char data[32];
	if (thermState.targetTempMax == UNKNOWN) return;
	if (targetTemp < 5) targetTemp = 5;
	if (targetTemp > thermState.targetTempMax) targetTemp = thermState.targetTempMax;
	targetTemp = round(targetTemp * 2.0) / 2.0;
	if (targetTemp == thermState.targetTemp) return;
	thermState.targetTemp = targetTemp;
	static char* cmd PROGMEM = "55aa0006000802020004%08x";
	sprintf(data, cmd, (int32)(targetTemp * 10));
	thermSendMessage(data, true);
}

void thermSetFloorTempMax(float floorTempMax) {
	char data[32];
	floorTempMax = round((floorTempMax < 0 ? 0 : floorTempMax>40 ? 40 : floorTempMax) * 10) / 10.0;
	if (floorTempMax == thermState.floorTempMax) return;
	thermState.floorTempMax = floorTempMax;
	static char* cmd PROGMEM = "55aa0006000866020004%08x";
	sprintf(data, cmd, (int32)floorTempMax);
	thermSendMessage(data, false);
}

void thermSetAntiFroze(bool antiFroze) {
	char data[32];
	if ((char)antiFroze == (char)thermState.antiFroze) return;
	thermState.antiFroze = antiFroze;
	static char* cmd PROGMEM = "55aa0006000567010001%02x";
	sprintf(data, cmd, antiFroze ? 1 : 0);
	thermSendMessage(data, false);
}

void thermSetHysteresis(float hysteresis) {
	char data[32];
	hysteresis = round((hysteresis < 0.5 ? 0.5 : hysteresis > 5 ? 5 : hysteresis) * 2.0) / 2.0;
	if (hysteresis == thermState.hysteresis) return;
	thermState.hysteresis = hysteresis;
	static char* cmd PROGMEM = "55aa0006000865020004%08x";
	sprintf(data, cmd, (int32)(hysteresis * 10));
	thermSendMessage(data, false);
}

void thermSetTargetTempMax(float targetTempMax) {
	char data[32];
	targetTempMax = round(targetTempMax < 15 ? 15 : targetTempMax > 40 ? 40 : targetTempMax);

	if (targetTempMax == thermState.targetTempMax) return;
	thermState.targetTempMax = targetTempMax;
	static char* cmd PROGMEM = "55aa000600080f020004%08x";
	sprintf(data, cmd, (int32)targetTempMax);
	thermSendMessage(data, false);
}

/// <summary>
/// 0: Manual mode
/// 1: Program mode
/// 2: temp program mode
///</summary>
void thermSetMode(char mode) {
	char data[32];
	if (mode < 0 || mode > 2) return;
	static char* cmd PROGMEM = "55aa0006000504040001%02x";
	sprintf(data, cmd, mode);
	thermSendMessage(data, false);
}

void thermSetPower(bool power) {
	char data[32];
	if ((char)power == (char)thermState.power) return;
	thermState.power = power;
	static char* cmd PROGMEM = "55aa0006000501010001%02x";
	sprintf(data, cmd, power ? 1 : 0);
	thermSendMessage(data, true);
}

void thermSetInverted(bool inverted) {
	char data[32];
	if ((char)inverted == (char)thermState.inverted) return;
	thermState.inverted = inverted;
	static char* cmd PROGMEM = "55aa000600056c010001%02x";
	sprintf(data, cmd, inverted ? 1 : 0);
	thermSendMessage(data, false);
}

void thermSetAdjTemp(float adjTemp) {
	char data[32];
	adjTemp = round((adjTemp < -9.9 ? -9.9 : adjTemp > 9.9 ? 9.9 : adjTemp) * 10.0) / 10.0;
	if (adjTemp == thermState.adjTemp) return;
	thermState.adjTemp = adjTemp;
	static char* cmd PROGMEM = "55aa0006000813020004%08x";
	sprintf(data, cmd, (int32)(adjTemp * 10));
	thermSendMessage(data, false);
}

/// <summary>
/// 0: Internal sensor
/// 1 : External sensor
/// 2 : Both sensors
///</summary>
void thermSetSensor(char sensor) {
	char data[32];
	if (sensor < 0 || sensor > 2) return;
	if (sensor == thermState.sensor) return;
	thermState.sensor = sensor;
	static char* cmd PROGMEM = "55aa000600056e040001%02x";
	sprintf(data, cmd, sensor);
	thermSendMessage(data, false);
}

void thermSetSound(bool sound) {
	char data[32];
	if ((char)sound == (char)thermState.sound) return;
	thermState.sound = sound;
	static char* cmd PROGMEM = "55aa000600056d010001%02x";
	sprintf(data, cmd, sound ? 1 : 0);
	thermSendMessage(data, false);
}

void thermSetLock(bool locked) {
	char data[32];
	if ((char)locked == (char)thermState.locked) return;
	thermState.locked = locked;
	static char* cmd PROGMEM = "55aa0006000509010001%02x";
	sprintf(data, cmd, locked ? 1 : 0);
	thermSendMessage(data, false);
}


void thermSetTime() {
	tm* tm = commsGetTime();
	if (tm == NULL) return;
	char data[32];
	//                                         YY  MM  DD  hh  mm  ss  WD
	static char* cmd PROGMEM = "55aa001c000801%02x%02x%02x%02x%02x%02x%02x";
	sprintf(data, cmd,
		tm->tm_year % 100, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec,
		(tm->tm_wday > 0) ? tm->tm_wday : 7
	);
	thermSendMessage(data, false);
}

#pragma endregion

#pragma region ProcessMessage
void thermProcessMessage() {

	// Вернулся какой-то статус
	if (thermData[2] == 0x03 && thermData[3] == 0x07) {
		int32 i32 = (((((thermData[thermDataLen - 5] << 8) | thermData[thermDataLen - 4]) << 8) | thermData[thermDataLen - 3]) << 8) | thermData[thermDataLen - 2];
		signed short i16 = (thermData[thermDataLen - 3] << 8) | thermData[thermDataLen - 2];
		char i8 = thermData[thermDataLen - 2];

		switch (thermData[6]) {
		case 0x01:
			thermState.power = i8 != 0;
			return;
		case 0x02:
			thermState.targetTemp = i32 / 10.0;
			return;
		case 0x03:
			thermState.roomTemp = i32 / 10.0;
			return;
		case 0x04:
			if (i8 != 0) thermSetMode(0);
			return;
		case 0x05:
			thermState.heating = i8 != 0;
			return;
		case 0x09:
			thermState.locked = i8 != 0;
			return;
		case 0x0B:
			// 1 byte
			break;
		case 0x0F:
			thermState.targetTempMax = i32;
			return;
		case 0x13:
			thermState.adjTemp = i32 / 10.0;
			return;
		case 0x65:
			thermState.hysteresis = i32 / 10.0;
			return;
		case 0x66:
			thermState.floorTempMax = i32;
			return;
		case 0x67:
			thermState.antiFroze = i8 != 0;
			return;
		case 0x68:
			// 1 byte
			break;
		case 0x69:
			// Calendar
			return;
		case 0x6A:
			thermState.brightness = i8;
			return;
		case 0x6B:
			// 1 byte
			break;
		case 0x6C:
			thermState.inverted = i8 != 0;
			return;
		case 0x6D:
			thermState.sound = i8 != 0;
			return;
		case 0x6E:
			thermState.sensor = i8;
			return;
		}
	}

#ifdef THERM_DEBUG
	char s[255];
	char hex[4];
	strcpy(s, "< ");

	for (int i = 0; i < thermDataLen; i++) {
		sprintf(hex, "%02x ", thermData[i]);
		if (i == thermDataLen - 1) strcat(s, ": ");
		strcat(s, hex);
	}

	aePrintln(s);
	mqttPublish("Log", s, false);
#endif
}
#pragma endregion

#pragma region State publishing
void thermPublish(char* topic, float value, float* _value, bool retained, bool activity) {
	if (value == UNKNOWN || value == *_value) return;
	char s[8];
	dtostrf(value, 4, 1, s);
	char* p = s;
	while (*p == ' ') p++;
	if (!mqttPublish(topic, p, retained)) return;
	*_value = value;
	if (activity) thermTriggerActivity();
}

void thermPublish(char* topic, bool value, bool* _value, bool retained, bool activity) {
	if ((char)value == UNKNOWN || (char)value == (char)(*_value) || !mqttPublish(topic, value ? 1 : 0, retained)) return;
	*_value = value;
	if (activity) thermTriggerActivity();
}

void thermPublish(char* topic, char value, char* _value, bool retained, bool activity) {
	if ((char)value == UNKNOWN || value == (*_value) || !mqttPublish(topic, (int)value, retained)) return;
	*_value = value;
	if (activity) thermTriggerActivity();
}

void thermPublish(bool force) {
	static unsigned long _t = 0;
	static ThermState _thermState;
	unsigned long t = millis();
	if (force) {
		thermInvalidate(&_thermState);
		_t = 0;
	}
	if (!thermStateIsValid()) return;

	if (!timedOut(t, _t, 500)) {
		return;
	}
	_t = t;

	//char s[128];
	//char s1[128];
	thermPublish(TOPIC_Brightness, thermState.brightness, &_thermState.brightness, true, true);
	thermPublish(TOPIC_Inverted, thermState.inverted, &_thermState.inverted, true, true);
	thermPublish(TOPIC_Locked, thermState.locked, &_thermState.locked, true, true);
	thermPublish(TOPIC_Power, thermState.power, &_thermState.power, true, true);
	thermPublish(TOPIC_Heating, thermState.heating, &_thermState.heating, true, false);
	thermPublish(TOPIC_Sound, thermState.sound, &_thermState.sound, true, true);
	if (abs(thermState.roomTemp - _thermState.roomTemp) > 0.15) {
		thermPublish(TOPIC_RoomTemp, thermState.roomTemp, &_thermState.roomTemp, true, false);
	}
	thermPublish(TOPIC_TargetTemp, thermState.targetTemp, &_thermState.targetTemp, true, true);
	thermPublish(TOPIC_TargetTempMax, thermState.targetTempMax, &_thermState.targetTempMax, true, false);
	thermPublish(TOPIC_FloorTemp, thermState.floorTemp, &_thermState.floorTemp, true, false);
	thermPublish(TOPIC_FloorTempMax, thermState.floorTempMax, &_thermState.floorTempMax, true, false);
	thermPublish(TOPIC_Sensor, thermState.sensor, &_thermState.sensor, true, false);
	thermPublish(TOPIC_Hysteresis, thermState.hysteresis, &_thermState.hysteresis, true, false);
	thermPublish(TOPIC_AdjTemp, thermState.adjTemp, &_thermState.adjTemp, true, false);
	thermPublish(TOPIC_AntiFroze, thermState.antiFroze, &_thermState.antiFroze, true, false);

	//thermPublish(TOPIC_PowerOnMemory, thermState.powerOnMemory, &_thermState.powerOnMemory, true, false);

	static int _haMode = -1;
	static int _hAction = -1;
	int haMode, hAction;
	if (!thermState.power) {
		haMode = 0; // off
		hAction = 0; // off
	} else {
		haMode = 1; // on
		hAction = thermState.heating ? 2 : 1; // Heat / Idle
	}
	if ((_haMode != haMode) && mqttPublish(TOPIC_HAMode, HAMODE(haMode), true)) {
		_haMode = haMode;
	}
	if ((_hAction != hAction) && mqttPublish(TOPIC_HAction, HACTION(hAction), true)) {
		_hAction = hAction;
	}



#ifdef USE_TAH
	static int _autoAdjMode = 99;
	if (_autoAdjMode != thermConfig.autoAdjMode) {
		if (mqttPublish(TOPIC_AutoAdjMode, thermConfig.autoAdjMode, true)) {
			_autoAdjMode = thermConfig.autoAdjMode;
		}
	}

	static float _sensorAdjTempOn = 99;
	thermPublish(TOPIC_SensorAdjTempOn, thermConfig.sensorAdjTempOn, &_sensorAdjTempOn, true, false);

	static float _sensorAdjTempOff = 99;
	thermPublish(TOPIC_SensorAdjTempOff, thermConfig.sensorAdjTempOff, &_sensorAdjTempOff, true, false);
#endif
}
#pragma endregion

#pragma region MQTT subscribtion handling
void thermConnect() {
	mqttSubscribeTopic(TOPIC_SetBrightness);
	mqttSubscribeTopic(TOPIC_SetInverted);
	mqttSubscribeTopic(TOPIC_SetLocked);
	mqttSubscribeTopic(TOPIC_SetPower);
	mqttSubscribeTopic(TOPIC_SetSound);
	mqttSubscribeTopic(TOPIC_SetTargetTemp);
	mqttSubscribeTopic(TOPIC_SetTargetTempMax);
	mqttSubscribeTopic(TOPIC_SetFloorTempMax);
	mqttSubscribeTopic(TOPIC_SetSensor);
	mqttSubscribeTopic(TOPIC_SetHysteresis);
	mqttSubscribeTopic(TOPIC_SetAdjTemp);
	mqttSubscribeTopic(TOPIC_SetAntiFroze);
	mqttSubscribeTopic(TOPIC_SetHAMode);

#ifdef USE_TAH
	mqttSubscribeTopic(TOPIC_SetAutoAdjMode);
	mqttSubscribeTopic(TOPIC_SetSensorAdjTempOn);
	mqttSubscribeTopic(TOPIC_SetSensorAdjTempOff);
#endif
	thermPublish(true);
}


bool thermCallback(char* topic, byte* payload, unsigned int length) {
	char s[64];
	float f;
	bool b;
	int i;

	memset(s, 0, sizeof(s));
	if ((payload != NULL) && (length > 0)) {
		if (length > sizeof(s) - 1) length = sizeof(s) - 1;
		strncpy(s, ((char*)payload), length);
	}

	if (mqttIsTopic(topic, TOPIC_SetBrightness)) {
		if (parseInt(s, 0, 3, &i)) thermSetBrightness(i);
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetInverted)) {
		if (parseBool(s, &b)) thermSetInverted(b);
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetLocked)) {
		if (parseBool(s, &b)) thermSetLock(b);
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetPower)) {
		if (parseBool(s, &b)) thermSetPower(b);
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetSound)) {
		if (parseBool(s, &b)) thermSetSound(b);
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetTargetTemp)) {
		if (parseFloat(s, 5, thermState.targetTempMax, &f)) thermSetTargetTemp(f);
		return true;
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetTargetTempMax)) {
		if (parseFloat(s, 20, 35, &f)) thermSetTargetTempMax(f);
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetFloorTempMax)) {
		if (parseFloat(s, 20, 45, &f)) thermSetFloorTempMax(f);
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetSensor)) {
		if (parseInt(s, 0, 2, &i)) thermSetSensor(i);
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetHysteresis)) {
		if (parseFloat(s, 0, 5, &f)) thermSetHysteresis(f);
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetAdjTemp)) {
		if (parseFloat(s, -10, 10, &f)) thermSetAdjTemp(f);
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetAntiFroze)) {
		if (parseBool(s, &b)) thermSetAntiFroze(b);
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetHAMode)) {
		if ((payload != NULL) && (length > 0) && (length <= 15)) {
			thermSetMode(0);
			if (strcmp(s, HAMODE(0)) == 0) { // Off
				thermSetPower(false);
			} else if (strcmp(s, HAMODE(1)) == 0) { // Heat
				thermSetPower(true);
			}
		}
		return true;
	}
#ifdef USE_TAH
	if (mqttIsTopic(topic, TOPIC_SetAutoAdjMode)) {
		int autoAdjMode;
		if (parseInt(s, 0, 2, &autoAdjMode) && (thermState.sensor == 0) && (autoAdjMode != thermConfig.autoAdjMode)) {
			thermConfig.autoAdjMode = autoAdjMode;
			storageSave();
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetSensorAdjTempOn)) {
		if (parseFloat(s, -5, 5, &f)) {
			thermConfig.sensorAdjTempOn = f;
			if (thermState.power) tahTemperatureAdj = f;
			storageSave();
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetSensorAdjTempOff)) {
		if (parseFloat(s, -5, 5, &f)) {
			thermConfig.sensorAdjTempOff = f;
			if (!thermState.power) tahTemperatureAdj = f;
			storageSave();
		}
		return true;
	}
#endif    
	return false;
}
#pragma endregion

#pragma region Init & Loop
void thermLoop() {
	if (thermDisabled) return;

	static unsigned long lastRead = 0;
	static unsigned long lastMaintenance = 0;

	unsigned long t = millis();

	// Read Thermostat MCU uart
	while (therm.available() > 0) {
		char ch = therm.read();
		if (thermDataLen == 0 && ch == 0x55 || thermDataLen > 0) {
			thermData[thermDataLen++] = ch;
		}

		if (thermDataLen >= 7 // Minimal packet length
			&& thermData[0] == 0x55 && thermData[1] == 0xAA // Packet signature
			&& thermDataLen == 6 + thermData[5] + 1 // header size + payload size + CRC
			) {

			uint8_t crc = 0;
			for (int i = 0; i < thermDataLen - 1; i++) {
				crc += thermData[i];
			}

			if (thermData[thermDataLen - 1] == crc) {
				thermProcessMessage();
			} else {
#ifdef THERM_DEBUG
				char s[255];
				char hex[32];
				strcpy(s, "< ");

				for (int i = 0; i < thermDataLen; i++) {
					sprintf(hex, "%02x ", thermData[i]);
					if (i == thermDataLen - 1) strcat(s, ": ");
					strcat(s, hex);
				}
				sprintf(hex, "\r\nCRC %02x != %02x", thermData[thermDataLen - 1], crc);
				strcat(s, hex);
				aePrintln(s);
				mqttPublish("Log", s, false);
#endif
			}
			thermDataLen = 0;
		}


		// Check buffer overflow 
		if (thermDataLen >= sizeof(thermData) - 1) {
			thermDataLen = 0;
		}
		lastRead = t;
	}

	// Heartbeat
	if (timedOut(t, thermLastMessage, 10000L)) {
		static char* cmd PROGMEM = "55aa00000000";
		thermSendMessage(cmd, false);
		return;
	}

	static bool initialized = false;
	// Periodical maintenance tasks:
	if (timedOut(t, lastMaintenance, initialized ? 60000L : 10000L)) {
		lastMaintenance = t;
		initialized = true;
		thermSetTime();
#ifdef USE_TAH
		if ((thermState.sensor == 0) && (thermConfig.autoAdjMode != 0) && (thermState.roomTemp != UNKNOWN)) {
			float t = 0;
			if (thermConfig.autoAdjMode == 1) {
				t = tahGetTemperature();
			} else if (thermConfig.autoAdjMode == 2) {
				t = tahGetHeatIndex();
			}
			if ((t > -10) && (abs(t - thermState.roomTemp) > 0.3)) {
				thermSetAdjTemp(t - (thermState.roomTemp - thermState.adjTemp));
			}
		}
#endif

		// Request all parameters:
		static char* cmd PROGMEM = "55aa00080000";
		thermSendMessage(cmd, false);
	}

#ifdef USE_TAH
	static unsigned long tmTempAdj = 0;
	// Maintain temperature adjastment sensor
	if (thermStateIsValid() && timedOut(t, tmTempAdj, 10000)) {
		// Шаг нагрева/остывания корпуса термостата, в градусах за 10 секунд
		float sensorAdjDelta = ((thermConfig.sensorAdjTempOn - thermConfig.sensorAdjTempOff) / (20 * 60 / 10.0F));

		if (tahTemperatureAdj == 0) {
			tahTemperatureAdj = thermState.power ? thermConfig.sensorAdjTempOn : thermConfig.sensorAdjTempOff;
		} else {
			tahTemperatureAdj += thermState.power ? sensorAdjDelta : -sensorAdjDelta;
		}

		if (tahTemperatureAdj > thermConfig.sensorAdjTempOff) {
			tahTemperatureAdj = thermConfig.sensorAdjTempOff;
		}

		if (tahTemperatureAdj < thermConfig.sensorAdjTempOn) {
			tahTemperatureAdj = thermConfig.sensorAdjTempOn;
		}
		//char s[20];
		//dtostrf(tahTemperatureAdj, 0, 5, s);
		//mqttPublish("tahTempAdj", s, false);
		tmTempAdj = t;
	}
#endif



	thermSetWiFiSign(
		!commsEnabled() ? ThermWiFiState::Off :
		!wifiConnected() ? ThermWiFiState::BlinkFast :
		!mqttConnected() ? ThermWiFiState::Blink :
		ThermWiFiState::On
	);

	thermPublish(false);
}

void thermInit() {
	thermInvalidate(&thermState);

	storageRegisterBlock('T', &thermConfig, sizeof(thermConfig));
	thermActivityDetectionLocked = millis();
	therm.begin(9600);
	delay(500);
	mqttRegisterCallbacks(thermCallback, thermConnect);
	aeRegisterLoop(thermLoop);
}

#pragma endregion
