#include <Arduino.h>
#include <errno.h>
#include "AELib.h"
#include "Comms.h"
#include "Thermostat.h"

#ifdef USE_HTU
#include "TAH.h"
#endif
#pragma region Constants

#ifdef TIMEZONE
#include <time.h>
#endif

// Room temperature averaging, ~1 measurement every 4 second:
#define ROOM_TEMP_SIZE (180 / 4)

static char* TOPIC_SetLocked PROGMEM = "SetLocked";
static char* TOPIC_SetPower PROGMEM = "SetPower";
static char* TOPIC_Heating PROGMEM = "Heating";
static char* TOPIC_TargetSetManually PROGMEM = "TargetSetManually";
static char* TOPIC_RoomTemp PROGMEM = "RoomTemp";
static char* TOPIC_FloorTemp PROGMEM = "FloorTemp";
static char* TOPIC_SetFloorTempMax PROGMEM = "SetFloorTempMax";
static char* TOPIC_SetTargetTemp PROGMEM = "SetTargetTemp";

static char* TOPIC_TargetTempMax PROGMEM = "TargetTempMax";
static char* TOPIC_TargetTempMin PROGMEM = "TargetTempMin";

#ifndef MANUAL_MODE_ONLY
static char* TOPIC_SetAutoMode PROGMEM = "SetAutoMode";
static char* TOPIC_SetLoopMode PROGMEM = "SetLoopMode";
static char* TOPIC_SetSchedule PROGMEM = "SetSchedule";
static char* TOPIC_SetSchedule2 PROGMEM = "SetSchedule2";
#endif

static char* TOPIC_SetSensor PROGMEM = "SetSensor";
static char* TOPIC_Hysteresis PROGMEM = "Hysteresis";
static char* TOPIC_SetAdjTemp PROGMEM = "SetAdjTemp";

static char* TOPIC_SensorAdjTempOn PROGMEM = "Sensors/AdjTempOn";
static char* TOPIC_SetSensorAdjTempOn PROGMEM = "Sensors/SetAdjTempOn";
static char* TOPIC_SensorAdjTempOff PROGMEM = "Sensors/AdjTempOff";
static char* TOPIC_SetSensorAdjTempOff PROGMEM = "Sensors/SetAdjTempOff";


static char* TOPIC_HAction PROGMEM = "HAction";
static char* TOPIC_SetHAMode PROGMEM = "SetHAMode";

static char* HAMODE_Off PROGMEM = "off"; // 0 
static char* HAMODE_Heat PROGMEM = "heat"; // 1
static char* HAMODE_Auto PROGMEM = "auto"; // 2

static char* HACTION_Off PROGMEM = "off"; // 0
static char* HACTION_Idle PROGMEM = "idle"; // 1
static char* HACTION_Heating PROGMEM = "heating"; // 2

char* HAMODE(int haMode) {
	return
		(haMode == 2) ? HAMODE_Auto :
		(haMode == 1) ? HAMODE_Heat :
		HAMODE_Off;
}
char* HACTION(int hAction) {
	return
		(hAction == 2) ? HACTION_Heating :
		(hAction == 1) ? HACTION_Idle :
		HACTION_Off;
}


#ifdef USE_HTU
static char* TOPIC_SetAutoAdjMode PROGMEM = "SetAutoAdjMode";
#endif
static char* TOPIC_SetAntiFroze PROGMEM = "SetAntiFroze";
static char* TOPIC_SetPowerOnMemory PROGMEM = "SetPowerOnMemory";


#define P3(str) ((char*)(((uint)str)+3))
#pragma endregion

#pragma region Types and Vars
enum ThermWiFiState {
	Off,
	BlinkFast,
	Blink,
	On
};

ThermConfig thermConfig;

char thermData[128];
int thermDataLen = 0;

float thermRoomTemp[ROOM_TEMP_SIZE];

// Current thermostat state
ThermState thermState;
unsigned long thermLastStatusRequest = 0;
unsigned long thermLastStatus = 0;
unsigned long thermActivityLocked = 0;
uint16 thermCRC = 0;
bool thermDisabled = false;

#ifdef USE_SOFT_SERIAL
#include <SoftwareSerial.h>
SoftwareSerial therm(THERM_RX, THERM_TX);
#else
#define therm Serial
#endif
#pragma endregion

#pragma region CRC
void thermCRCStart() {
	thermCRC = 0xFFFF;
}
void thermCRCNext(uint8 nextByte) {
	char i;
	thermCRC ^= nextByte;
	for (i = 0; i < 8; i++) {
		if (thermCRC & 0x0001) {
			thermCRC = (thermCRC >> 1) ^ 0xA001;
		} else {
			thermCRC >>= 1;
		}
	}
}
#pragma endregion

#pragma region Message Sending
void thermSendMessage(const char* data, bool appendCRC) {
	char hex[3] = { 0,0,0 };
	char* p = (char*)data;
	thermCRCStart();
	while (*p > '\0') {
		hex[0] = *p; p++;
		hex[1] = *p; p++;
		if (*p == ' ') p++;
		uint8_t d = strtoul(hex, NULL, 16);
		thermCRCNext(d);
		therm.write(d);
	}
	if (appendCRC) {
		therm.write(thermCRC & 0x00FF);
		therm.write(thermCRC >> 8);
	}

#ifdef THERM_DEBUG
	char s[256];
	if (appendCRC) {
		sprintf(s, "> %s:%02x%02x\n", data, (thermCRC & 0x00FF), (thermCRC >> 8));
	} else {
		sprintf(s, "> %s\n", data);
	}
	mqttPublish("Log", s, false);
#endif
	delay(150);
}

void thermSendMessage(const char* data) {
	thermSendMessage(data, true);
}

void thermSendAdvancedParams() {
	char data[64];
	int16_t a = round(thermState.adjTemp * 2.0);
	thermActivityLocked = millis();

	sprintf(data, "0110000200050a %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		thermState.loopMode,
		thermState.sensor,
		(int)thermState.floorTempMax,
		(int)(thermState.hysteresis * 2.0),
		(int)thermState.targetTempMax,
		(int)thermState.targetTempMin,
		(a >> 8) & 0xFF, a & 0xFF,
		thermState.antiFroze ? 1 : 0,
		thermState.powerOnMemory ? 1 : 0
	);
	//  mqttPublish("Log",data,false);
	thermSendMessage(data);
}

// 0: off
// 1: fast blink
// 2: slow blink
// 3: on
void thermSetWiFiSign(ThermWiFiState wifiState) {
	char data[64];
	if (wifiState == ThermWiFiState::Off) {
		strcpy(data, "a5a55a5a99c1e90300000000");
	} else {
		//   0: BlinkFast
		//   1: Blink
		//   2: On
		uint8 mode = (wifiState == ThermWiFiState::BlinkFast) ? 0 : (wifiState == ThermWiFiState::Blink) ? 1 : 2;
		sprintf(data, "a5a55a5aa1c1ec0304000000 %02x 000000", mode);
	}
	thermSendMessage(data, false);
}

#pragma endregion

#pragma region ProcessMessage
bool thermProcessMessage() {
	// have at least 23 bytes
	if (thermDataLen < 23) return false;

	thermCRCStart();
	for (int i = 0; i < thermDataLen - 2; i++) {
		thermCRCNext(thermData[i]);
	}

#ifdef THERM_DEBUG
	char s[255];
	static const char hexChars[] = "0123456789abcdef";
	size_t pos = 0;
	s[pos++] = '<';
	s[pos++] = ' ';

	for (int i = 0; i < thermDataLen; i++) {
		size_t separatorLength = (i == thermDataLen - 2) ? 2 : 0;
		if (pos + 3 + separatorLength >= sizeof(s)) break;
		uint8_t currentByte = (uint8_t)thermData[i];
		s[pos++] = hexChars[currentByte >> 4];
		s[pos++] = hexChars[currentByte & 0x0F];
		s[pos++] = ' ';
		if (separatorLength > 0) {
			s[pos++] = ':';
			s[pos++] = ' ';
		}
	}
	snprintf(s + pos, sizeof(s) - pos, "[%04x]", thermCRC);

	mqttPublish("Log", s, false);
#endif


	// Test if it is valid "Status" packet
	if ((thermDataLen > 22) // have at least 23 bytes
		&& (thermData[0] == 0x01) && (thermData[1] == 0x03) // signature is for "Status" packet type
		&& (thermData[11] > thermData[12]) // temperature range is valid
		&& ((uint8_t)thermData[thermDataLen - 2] == (uint8_t)(thermCRC & 0xFF)) // CRC low byte is valid
		&& ((uint8_t)thermData[thermDataLen - 1] == (uint8_t)(thermCRC >> 8)) // CRC high byte is valid
		) {

		thermLastStatus = millis();
		thermLastStatusRequest = thermLastStatus;

		thermState.locked = thermData[3] & 1;
		thermState.power = thermData[4] & 1;
		thermState.heating = (thermData[4] >> 4) & 1;
		thermState.targetSetManually = (thermData[4] >> 6) & 1;


		memmove(&thermRoomTemp[1], &thermRoomTemp[0], sizeof(float) * (ROOM_TEMP_SIZE - 1));

		thermRoomTemp[0] = (float)(thermData[5] & 255) / 2.0;
		float temp = 0.0;
		for (int i = 0; i < ROOM_TEMP_SIZE; i++ ) {
			temp += (thermRoomTemp[i] > -50.0) ? thermRoomTemp[i] : thermRoomTemp[0];
		}
		thermState.roomTemp = round(temp * 4.0 / (float)ROOM_TEMP_SIZE ) / 4.0;


		thermState.targetTemp = (thermData[6] & 255) / 2.0;
		thermState.targetTempMax = thermData[11];
		thermState.targetTempMin = thermData[12];

		thermState.floorTemp = (thermData[18] & 0xFF) / 2.0;
		thermState.floorTempMax = thermData[9];

		thermState.autoMode = thermData[7] & 0x01;
		thermState.loopMode = (thermData[7] >> 4) & 0x0F;
		thermState.sensor = thermData[8];
		thermState.hysteresis = thermData[10] / 2.0;

		thermState.adjTemp = ((int16_t)((thermData[13] << 8) + thermData[14])) / 2.0;

		thermState.antiFroze = (thermData[15] & 1);
		thermState.powerOnMemory = (thermData[16] & 1);

		thermState.hours = thermData[19];
		thermState.minutes = thermData[20];
		thermState.seconds = thermData[21];
		thermState.weekday = thermData[22];

		// If status packet have schedule data
		if (thermDataLen > 46) {
			for (int i = 0; i < 6; i++) {
				thermState.schedule[i].h = thermData[2 * i + 23];
				thermState.schedule[i].m = thermData[2 * i + 24];
				thermState.schedule[i].t = (float)(thermData[i + 39] / 2.0);
				//aePrintf("%d: %d %d %f\n", i, thermState.schedule[i].h, thermState.schedule[i].m, thermState.schedule[i].t );
				if (i < 2) {
					thermState.schedule2[i].h = thermData[2 * (i + 6) + 23];
					thermState.schedule2[i].m = thermData[2 * (i + 6) + 24];
					thermState.schedule2[i].t = (float)(thermData[i + 6 + 39] / 2.0);
				}
			}
		}
		return true;
	}
	return false;
}
#pragma endregion

#pragma region Schedule helpers
char* thermPrintSchedule(char* s, ThermScheduleRecord schedule[], int recordCount) {
	*s = 0;
	char sr[16];
	char sf[8];
	if ((schedule[0].h > 23) || (schedule[0].m > 30)) return s;

	for (int i = 0; i < recordCount; i++) {
		dtostrf(schedule[i].t, 4, 1, sf);
		char* p = sf;
		while (*p == ' ') p++;

		sprintf(sr, "%02d:%02d %s", schedule[i].h, schedule[i].m, p);
		strcat(s, sr);
		if (i + 1 < recordCount) strcat(s, ";");
	}
	return s;
}

void thermParseSchedule(char* payload, unsigned int length, ThermScheduleRecord schedule[], int recordCount) {
	if ((payload == NULL) || (length < 10)) return;
	char s[256];
	char s2[16];
	ThermScheduleRecord sch[6];
	char* p = s;
	bool changed = false;
	errno = 0;
	memset(s, 0, sizeof(s));
	strncpy(s, payload, length);

	for (int i = 0; i < recordCount; i++) {
		if (*p == 0) return;

		sch[i].h = (int8)strtol(p, &p, 10);
		if ((errno != 0) || (sch[i].h < 0) || (sch[i].h > 23)) return;
		while ((*p != 0) && ((*p < '0') || (*p > '9'))) p++;

		sch[i].m = (int8)strtol(p, &p, 10);
		if ((errno != 0) || (sch[i].m < 0) || (sch[i].m > 60)) return;
		while ((*p != 0) && ((*p < '0') || (*p > '9'))) p++;

		sch[i].t = ((int)(strtof(p, &p) * 2)) / 2.0;
		if ((errno != 0) || (sch[i].t < thermState.targetTempMin) || (sch[i].t > thermState.targetTempMax)) return;
		while ((*p != 0) && ((*p < '0') || (*p > '9'))) p++;

		if ((sch[i].h != schedule[i].h) || (sch[i].m != schedule[i].m) || ((int)(sch[i].t * 2) != (int)(schedule[i].t * 2))) changed = true;
	}

	if (!changed) return;

	//aePrintln(thermPrintSchedule( s, sch, recordCount ));
	memcpy(schedule, sch, sizeof(ThermScheduleRecord) * recordCount);

	strcpy(s, "0110000a000c18");
	for (int i = 0; i < 6; i++) {
		sprintf(s2, "%02x%02x ", thermState.schedule[i].h, thermState.schedule[i].m);
		strcat(s, s2);
	}
	for (int i = 0; i < 2; i++) {
		sprintf(s2, "%02x%02x ", thermState.schedule2[i].h, thermState.schedule2[i].m);
		strcat(s, s2);
	}
	for (int i = 0; i < 6; i++) {
		sprintf(s2, "%02x ", (int)(thermState.schedule[i].t * 2));
		strcat(s, s2);
	}
	for (int i = 0; i < 2; i++) {
		sprintf(s2, "%02x ", (int)(thermState.schedule2[i].t * 2));
		strcat(s, s2);
	}
	thermSendMessage(s);
}
#pragma endregion

#pragma region State publishing
void thermTriggerActivity() {
	if (timedOut(millis(), thermActivityLocked, 5000)) {
		triggerActivity();
	}
}

void thermPublish(char* topic, float value, float* _value, bool retained, bool activity) {
	if ((value != *_value)) {
		char s[8];
		dtostrf(value, 4, 1, s);
		char* p = s;
		while (*p == ' ') p++;
		if (mqttPublish(topic, p, retained)) {
			*_value = value;
			if (activity) thermTriggerActivity();
		}
	}
}
void thermPublish(char* topic, bool value, bool* _value, bool retained, bool activity) {
	if (((int)value != (int)(*_value)) && mqttPublish(topic, value ? 1 : 0, retained)) {
		*_value = value;
		if (activity) thermTriggerActivity();
	}
}

void thermPublish(char* topic, int value, int* _value, bool retained, bool activity) {
	if ((value != (*_value)) && mqttPublish(topic, value, retained)) {
		*_value = value;
		if (activity) thermTriggerActivity();
	}
}

void thermPublish(bool force) {
	static ThermState _thermState;
	static unsigned long _t = 0;
	unsigned long t = millis();
	if (force) {
		memset(&_thermState, 0xAF, sizeof(_thermState));
		_t = 0;
	}
	// To avoid MQTT spam
	if ((thermLastStatus == 0) || !timedOut(t, _t, 500)) {
		return;
	}
	_t = t;
	char s[128];
	char s1[128];
	thermPublish(P3(TOPIC_SetLocked), thermState.locked, &_thermState.locked, true, true);
	thermPublish(P3(TOPIC_SetPower), thermState.power, &_thermState.power, true, true);
	thermPublish(TOPIC_Heating, thermState.heating, &_thermState.heating, true, false);
	thermPublish(TOPIC_TargetSetManually, thermState.targetSetManually, &_thermState.targetSetManually, true, false);
	if ((thermState.roomTemp > -50) && (abs(thermState.roomTemp - _thermState.roomTemp) >= 0.5)) {
		dtostrf( round(thermState.roomTemp * 2.0)/2.0, 4, 1, s);
		char* p = s;
		while (*p == ' ') p++;
		if (mqttPublish(TOPIC_RoomTemp, p, true)) {
			_thermState.roomTemp = thermState.roomTemp;
		}
	}
	thermPublish(P3(TOPIC_SetTargetTemp), thermState.targetTemp, &_thermState.targetTemp, true, true);
	thermPublish(TOPIC_TargetTempMax, thermState.targetTempMax, &_thermState.targetTempMax, true, false);
	thermPublish(TOPIC_TargetTempMin, thermState.targetTempMin, &_thermState.targetTempMin, true, false);
	thermPublish(TOPIC_FloorTemp, thermState.floorTemp, &_thermState.floorTemp, true, false);
	thermPublish(P3(TOPIC_SetFloorTempMax), thermState.floorTempMax, &_thermState.floorTempMax, true, false);

#ifndef MANUAL_MODE_ONLY
	thermPublish(P3(TOPIC_SetAutoMode), thermState.autoMode, &_thermState.autoMode, true, true);
	thermPublish(P3(TOPIC_SetLoopMode), thermState.loopMode, &_thermState.loopMode, true, false);
	thermPrintSchedule(s, thermState.schedule, 6);
	thermPrintSchedule(s1, _thermState.schedule, 6);
	if ((strlen(s) > 0) && (strcmp(s, s1) != 0)) {
		if (mqttPublish(P3(TOPIC_SetSchedule), s, true)) {
			memcpy(_thermState.schedule, thermState.schedule, sizeof(_thermState.schedule));
			thermLastPublished = millis();
		}
	}

	thermPrintSchedule(s, thermState.schedule2, 2);
	thermPrintSchedule(s1, _thermState.schedule2, 2);
	if ((strlen(s) > 0) && (strcmp(s, s1) != 0)) {
		if (mqttPublish(P3(TOPIC_SetSchedule2), s, true)) {
			memcpy(_thermState.schedule2, thermState.schedule2, sizeof(_thermState.schedule2));
			thermLastPublished = millis();
		}
	}
#endif
	thermPublish(P3(TOPIC_SetSensor), thermState.sensor, &_thermState.sensor, true, false);
	thermPublish(TOPIC_Hysteresis, thermState.hysteresis, &_thermState.hysteresis, true, false);
	thermPublish(P3(TOPIC_SetAdjTemp), thermState.adjTemp, &_thermState.adjTemp, true, false);

	thermPublish(P3(TOPIC_SetAntiFroze), thermState.antiFroze, &_thermState.antiFroze, true, false);
	thermPublish(P3(TOPIC_SetPowerOnMemory), thermState.powerOnMemory, &_thermState.powerOnMemory, true, false);

	static int _haMode = -1;
	static int _hAction = -1;
	int haMode, hAction;
	if (!thermState.power) {
		haMode = 0; // off
		hAction = 0; // off
	} else {
		// Auto / Heat
		haMode = thermState.autoMode ? 2 : 1;

		// Heat / Idle
		hAction = thermState.heating ? 2 : 1;
	}
	if ((_haMode != haMode) && mqttPublish(P3(TOPIC_SetHAMode), HAMODE(haMode), true)) {
		_haMode = haMode;
	}
	if ((_hAction != hAction) && mqttPublish(TOPIC_HAction, HACTION(hAction), true)) {
		_hAction = hAction;
	}



#ifdef USE_HTU
	static int _autoAdjMode = 99;
	if (_autoAdjMode != thermConfig.autoAdjMode) {
		if (mqttPublish(P3(TOPIC_SetAutoAdjMode), thermConfig.autoAdjMode, true)) {
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
	mqttSubscribeTopic(TOPIC_SetTargetTemp);
	mqttSubscribeTopic(TOPIC_SetPower);
	mqttSubscribeTopic(TOPIC_SetLocked);
#ifndef MANUAL_MODE_ONLY
	mqttSubscribeTopic(TOPIC_SetAutoMode);
	mqttSubscribeTopic(TOPIC_SetLoopMode);
	mqttSubscribeTopic(TOPIC_SetSchedule);
	mqttSubscribeTopic(TOPIC_SetSchedule2);
#endif
	mqttSubscribeTopic(TOPIC_SetSensor);
	mqttSubscribeTopic(TOPIC_SetAdjTemp);
	mqttSubscribeTopic(TOPIC_SetAntiFroze);
	mqttSubscribeTopic(TOPIC_SetPowerOnMemory);
	mqttSubscribeTopic(TOPIC_SetFloorTempMax);
	mqttSubscribeTopic(TOPIC_SetHAMode);

#ifdef USE_HTU
	mqttSubscribeTopic(TOPIC_SetAutoAdjMode);
	mqttSubscribeTopic(TOPIC_SetSensorAdjTempOn);
	mqttSubscribeTopic(TOPIC_SetSensorAdjTempOff);
#endif
	thermActivityLocked = millis();
	thermPublish(true);
}

void thermSetPower(bool power) {
	thermActivityLocked = millis();
	thermState.power = power ? 1 : 0;
	char s[31];
	sprintf(s, "01060000%02x%02x", thermState.locked, thermState.power);
	thermSendMessage(s);
}

void thermSetAutoMode(bool autoMode) {
#ifdef MANUAL_MODE_ONLY
	autoMode = false;
#endif
	thermActivityLocked = millis();
	thermState.autoMode = autoMode ? 1 : 0;
	char s[31];
	sprintf(s, "01060002%02x%02x", (((thermState.loopMode ? 1 : 0) << 4) | thermState.autoMode), thermState.sensor);
	thermSendMessage(s);
}

bool thermCallback(char* topic, byte* payload, unsigned int length) {
	char s[64];
	float f;
	bool b;

	memset(s, 0, sizeof(s));
	if ((payload != NULL) && (length > 0)) {
		if (length > sizeof(s) - 1) length = sizeof(s) - 1;
		strncpy(s, ((char*)payload), length);
	}

	if (mqttIsTopic(topic, TOPIC_SetTargetTemp)) {
		if (parseFloat(s, thermState.targetTempMin, thermState.targetTempMax, &f)) {
			float temp = ((int)(f * 2)) / 2.0;
			if (temp != thermState.targetTemp) {
				thermActivityLocked = millis();
				sprintf(s, "0106000100%02x", (int)(temp * 2));
				thermSendMessage(s);
				thermState.targetTemp = temp;
			}
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetAdjTemp)) {
		if (parseFloat(s, -10, 10, &f)) {
			float adjTemp = ((int)(f * 2)) / 2.0;
			if (adjTemp != thermState.adjTemp) {
				thermState.adjTemp = adjTemp;
				thermSendAdvancedParams();
			}
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetFloorTempMax)) {
		int ftMax;
		if (parseInt(s, 20, 45, &ftMax) && (thermState.floorTempMax != ftMax)) {
			thermState.floorTempMax = ftMax;
			thermSendAdvancedParams();
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetAntiFroze)) {
		if (parseBool(s, &b) && (b != thermState.antiFroze)) {
			thermState.antiFroze = b;
			thermSendAdvancedParams();
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetPowerOnMemory)) {
		if (parseBool(s, &b) && (b != thermState.powerOnMemory)) {
			thermState.powerOnMemory = b;
			thermSendAdvancedParams();
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetPower)) {
		if (parseBool(s, &b) && (b != thermState.power)) {
			thermSetPower(b);
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetLocked)) {
		if (parseBool(s, &b) && (b != thermState.locked)) {
			thermActivityLocked = millis();
			thermState.locked = b;
			sprintf(s, "01060000%02x%02x", thermState.locked ? 1 : 0, thermState.power ? 1 : 0);
			thermSendMessage(s);
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetSensor)) {
		int sensor;
		if (parseInt(s, 0, 7, &sensor) && (thermState.sensor != sensor)) {
			thermActivityLocked = millis();
			thermState.sensor = sensor;
			char s[31];
			sprintf(s, "01060002%02x%02x", (((thermState.loopMode ? 1 : 0) << 4) | (thermState.autoMode ? 1 : 0)), thermState.sensor);
			thermSendMessage(s);
		}
		return true;
	}

#ifndef MANUAL_MODE_ONLY
	if (mqttIsTopic(topic, TOPIC_SetAutoMode)) {
		if (parseBool(s, &b) && (b != thermState.autoMode)) {
			thermSetAutoMode(b);
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetLoopMode)) {
		int loopMode;
		if (parseInt(s, 0, 7, &loopMode) && (thermState.loopMode != loopMode)) {
			thermActivityLocked = millis();
			thermState.loopMode = loopMode;
			char s[31];
			sprintf(s, "01060002%02x%02x", (((thermState.loopMode ? 1 : 0) << 4) | (thermState.autoMode ? 1 : 0)), thermState.sensor);
			thermSendMessage(s);
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetSchedule)) {
		if ((length > 10) && (length < 255)) {
			thermParseSchedule((char*)payload, length, thermState.schedule, 6);
		}
		return true;
	}

	if (mqttIsTopic(topic, TOPIC_SetSchedule2)) {
		if ((length > 10) && (length < 255)) {
			thermParseSchedule((char*)payload, length, thermState.schedule2, 2);
		}
		return true;
	}
#endif

	if (mqttIsTopic(topic, TOPIC_SetHAMode)) {
		if ((payload != NULL) && (length > 0) && (length <= 15)) {
			char s[16];
			memset(s, 0, sizeof(s));
			memcpy(s, payload, length);
			if (strcmp(s, HAMODE(0)) == 0) { // Off
				thermSetPower(false);
				thermSetAutoMode(false);
			} else if (strcmp(s, HAMODE(1)) == 0) { // Heat
				thermSetPower(true);
				thermSetAutoMode(false);
			} else if (strcmp(s, HAMODE(2)) == 0) { // Auto
				thermSetPower(true);
				thermSetAutoMode(true);
			}
		}
		return true;
	}
#ifdef USE_HTU
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

	if (mqttIsTopic(topic, "EnableOTA")) {
		thermSetWiFiSign(ThermWiFiState::BlinkFast);
		thermDisabled = true;
		commsEnableOTA();
		return true;
	}
	return false;
}
#pragma endregion

#pragma region Init & Loop
void thermLoop() {
	if (thermDisabled) return;

	static unsigned long lastRead = 0;
	static unsigned long lastMaintenance = 0;

	unsigned long t = millis();

	if ((thermDataLen > 0) && timedOut(t, lastRead, 200)) {
		thermProcessMessage();
		thermDataLen = 0;
	}

	// Read Thermostat MCU uart
	while (therm.available() > 0) {
		// Check buffer overflow 
		if (thermDataLen >= sizeof(thermData) - 1) {
			thermProcessMessage();
			thermDataLen = 0;
		}

		thermData[thermDataLen++] = therm.read();
		lastRead = t;
	}

	// Get MCU status every few seconds
	if ((unsigned long)(t - thermLastStatusRequest) > (unsigned long)4000) {
		thermSendMessage("010300000016");
		thermLastStatusRequest = t;
		return;
	}

	// Periodical maintenance tasks
	if (timedOut(t, lastMaintenance, 10000) && timedOut(t, lastRead, 500)) {
		//*** Thermostat time validation
		tm* lt = commsGetTime();
		if (mqttConnected() && (lt != NULL)) {
			int weekday = (lt->tm_wday > 0) ? lt->tm_wday : 7;

			unsigned long tc = (((weekday * 24) + lt->tm_hour) * 60 + lt->tm_min) * 60 + lt->tm_sec;
			unsigned long tt = (((thermState.weekday * 24) + thermState.hours) * 60 + thermState.minutes) * 60 + thermState.seconds;
			// If more than 20 seconds difference:
			if (((tc < tt) ? (tt - tc) : (tc - tt)) > 20) {
				thermActivityLocked = millis();
				thermState.hours = lt->tm_hour;
				thermState.minutes = lt->tm_min;
				thermState.seconds = lt->tm_sec;
				thermState.weekday = weekday;
				char s[31];
				sprintf(s, "01100008000204%02x%02x%02x%02x", thermState.hours, thermState.minutes, thermState.seconds, thermState.weekday);
				thermSendMessage(s);
			}
		}

#ifdef MANUAL_MODE_ONLY
		if (thermState.autoMode) {
			thermSetAutoMode(false);
		}
#endif

#ifdef USE_HTU
		if ((thermState.sensor == 0) && thermConfig.autoAdjMode && (thermRoomTemp[0]>-100) ) {
			float t = 0;
			if (thermConfig.autoAdjMode == 1) {
				t = tahGetTemperature();
			} else if (thermConfig.autoAdjMode == 2) {
				t = tahGetHeatIndex();
			}
			if ((t > -10) && (abs(t - thermRoomTemp[0]) > 0.85)) {
				thermState.adjTemp = round( (t - thermRoomTemp[0] + thermState.adjTemp) * 2.0 ) / 2.0;
				thermSendAdvancedParams();
			}
		}
#endif
		lastMaintenance = t;
	}

#ifdef USE_HTU
	static unsigned long tmTempAdj = 0;
	// Maintain temperature adjastment sensor
	if (thermLastStatus && timedOut(t, tmTempAdj, 10000)) {
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
		tmTempAdj = t;
	}
#endif



	static char _wifiState = 99;
	ThermWiFiState wifiState =
		!commsEnabled() ? ThermWiFiState::Off :
		!wifiConnected() ? ThermWiFiState::BlinkFast :
		!mqttConnected() ? ThermWiFiState::Blink :
		ThermWiFiState::On;

	if (_wifiState != (char)wifiState) {
		thermSetWiFiSign(wifiState);
		_wifiState = (char)wifiState;
	}

	thermPublish(false);
}

void thermInit() {
	for (int i = 0; i < ROOM_TEMP_SIZE; i++) {
		thermRoomTemp[i] = -100;
	}
	storageRegisterBlock('T', &thermConfig, sizeof(thermConfig));
	thermActivityLocked = millis();
	therm.begin(9600);
	mqttRegisterCallbacks(thermCallback, thermConnect);
	aeRegisterLoop(thermLoop);
}

#pragma endregion
