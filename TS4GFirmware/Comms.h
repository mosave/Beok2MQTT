#ifndef comms_h
#define comms_h

#include <time.h>

#define COMMS_StorageId 'C'

#define MQTT_CALLBACK std::function<bool(char*, uint8_t*, unsigned int)> callback
#define MQTT_CONNECT std::function<void()> connect
#define MQTT_MAX_TOPIC_LEN 128

// Exported functions:
// WiFi
char* wifiHostName();

bool wifiConnected();
bool mqttConnected();

void commsConnect();
void commsDisconnect();

bool commsEnabled();
void commsEnable();
void commsDisable();

// Home Assistant
bool haConnected();

// All these functions treat TOPIC_Name as template and complete it with MQTT_Root, mqttClientId and optional variables (if passed)
// mqttTopic(...) function will be used to transform TOPIC_Name

char* mqttTopic(char* buffer, char* TOPIC_Name);
char* mqttTopic(char* buffer, char* TOPIC_Name, char* topicVar);
char* mqttTopic(char* buffer, char* TOPIC_Name, char* topicVar1, char* topicVar2);

bool mqttIsTopic(char* topic, char* TOPIC_Name);
bool mqttIsTopic(char* topic, char* TOPIC_Name, char* topicVar);
bool mqttIsTopic(char* topic, char* TOPIC_Name, char* topicVar1, char* topicVar2);

void mqttSubscribeTopic(char* TOPIC_Name);
void mqttSubscribeTopic(char* TOPIC_Name, char* topicVar);
void mqttSubscribeTopic(char* TOPIC_Name, char* topicVar1, char* topicVar2);

bool mqttPublish(char* TOPIC_Name, long value, bool retained);
bool mqttPublish(char* TOPIC_Name, char* topicVar, long value, bool retained);
bool mqttPublish(char* TOPIC_Name, char* topicVar1, char* topicVar2, long value, bool retained);

bool mqttPublish(char* TOPIC_Name, char* value, bool retained);
bool mqttPublish(char* TOPIC_Name, char* topicVar, char* value, bool retained);
bool mqttPublish(char* TOPIC_Name, char* topicVar1, char* topicVar2, char* value, bool retained);

// RAW topic names (no templating)
void mqttSubscribeTopicRaw(char* topic);
bool mqttPublishRaw(char* topic, long value, bool retained);
bool mqttPublishRaw(char* topic, char* value, bool retained);

// Parsers
/// <summary>Check if string is "boolean": 
/// "1"/"0", "on"/"off", "true"/"false","yes"/"no"
/// </summary>
/// <param name="str">String to parse</param>
/// <param name="value">Variable to return either true or false</param>
/// <returns>True if string passed contain one of the "boolean" values</returns>
bool parseBool(char* str, bool* value);

/// <summary>
/// Parse string expecting floating point number validating it with range specified
/// </summary>
/// <param name="str">String to parse</param>
/// <param name="min">Minimum allowed number</param>
/// <param name="max">Maximum allowed number</param>
/// <param name="max">Parse result (if successful</param>
/// <returns>True if string passed contain integer value within [min..max] range</returns>
bool parseInt(char* str, int min, int max, int* value);


/// <summary>
/// Parse string expecting floating point number validating it with range specified
/// </summary>
/// <param name="str">String to be parsed</param>
/// <param name="min">Minimum allowed number</param>
/// <param name="max">Maximum allowed number</param>
/// <param name="max">Parse result (if successful</param>
/// <returns>True if string passed contain floating value within [min..max] range</returns>
bool parseFloat(char* str, float min, float max, float* value);

/// <summary>
/// Announce human activity in ".../Activity" topic (button pressed / motion detected /etc)
/// </summary>
void triggerActivity();

/// <summary>
/// Subscribe to MQTT message received and MQTT connected events
/// </summary>
/// <param name="">MQTT message received callback function</param>
/// <param name="">MQTT broker connected callback function</param>
void mqttRegisterCallbacks(MQTT_CALLBACK, MQTT_CONNECT);

/// <summary>
/// Check if On The Air updates enabled
/// </summary>
/// <returns>True if OTA was enabled</returns>
bool commsOTAEnabled();

/// <summary>
/// Enable On The Air updates
/// </summary>
void commsEnableOTA();

// Be sure TIMEZONE is defined in config.h for time functions to work
// TRUE if local time is synchronized 
bool commsTimeIsValid();

// Returns pointer to structure containing local time or NULL if local time is not yet synchronized.
tm* commsGetTime();

/// <summary>
/// Check if:
/// * deviceId is a MAC address in format "01234567890A" (ignoring case):
/// * deviceId is equal to wifi host name (ignoring case):
/// * mqtt root ends with deviceId (ignoring case)
/// Examples
///   "SmartRelay14" will match "smartrelay14" host name
///   "14/sr1" will match "House/14/SR1" device root
/// </summary>
/// <param name="deviceId">Device ID string to test</param>
/// <returns>True if deviceId mach either host name or end of the device mqtt root path</returns>
bool commsDeviceIdIs( char* deviceId);

bool commsDeviceIdIs(char* deviceId, int len);

/// <summary>
/// Restart device
/// </summary>
void commsRestart();
void commsClearTopicAndRestart(char* topic);
void commsClearTopicAndRestart(char* topic, char* topicVar1);
void commsClearTopicAndRestart(char* topic, char* topicVar1, char* topicVar2);

/// <summary>
/// Initialize Comms engine 
/// </summary>
/// <param name="isTimeCritical">Set to true to make wifi more responsive by disabling wifi modem power save mode</param>
void commsInit(bool isTimeCritical);

/// <summary>
/// Initialize Comms engine with wifi modem power save mode allowed
/// </summary>
void commsInit();
#endif
