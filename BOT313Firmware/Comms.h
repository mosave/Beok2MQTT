#ifndef comms_h
#define comms_h

#define MQTT_CALLBACK std::function<bool(char*, uint8_t*, unsigned int)> callback
#define MQTT_CONNECT std::function<void()> connect

// Exported functions:
// WiFi
char* wifiHostName();
bool wifiConnected();
bool wifiEnabled();
void wifiEnable();
void wifiDisable();

// MQTT
bool mqttConnected();

// All these functions treat TOPIC_Name as template and complete it with MQTT_Root, mqttClientId and optional variables (if passed)
// mqttTopic(...) function will be used to transform TOPIC_Name

char* mqttTopic( char* buffer, char* TOPIC_Name );
char* mqttTopic( char* buffer, char* TOPIC_Name, char* topicVar );
char* mqttTopic( char* buffer, char* TOPIC_Name, char* topicVar1, char* topicVar2 );

bool mqttIsTopic( char* topic, char* TOPIC_Name );
bool mqttIsTopic( char* topic, char* TOPIC_Name, char* topicVar );
bool mqttIsTopic( char* topic, char* TOPIC_Name, char* topicVar1, char* topicVar2 );

void mqttSubscribeTopic( char* TOPIC_Name );
void mqttSubscribeTopic( char* TOPIC_Name, char* topicVar );
void mqttSubscribeTopic( char* TOPIC_Name, char* topicVar1, char* topicVar2 );

bool mqttPublish( char* TOPIC_Name, long value, bool retained );
bool mqttPublish( char* TOPIC_Name, char* topicVar, long value, bool retained );
bool mqttPublish( char* TOPIC_Name, char* topicVar1, char* topicVar2, long value, bool retained );

bool mqttPublish( char* TOPIC_Name, char* value, bool retained );
bool mqttPublish( char* TOPIC_Name, char* topicVar, char* value, bool retained );
bool mqttPublish( char* TOPIC_Name, char* topicVar1, char* topicVar2, char* value, bool retained );

// RAW topic names (no templating)
void mqttSubscribeTopicRaw( char* topic );
bool mqttPublishRaw( char* topic, long value, bool retained );
bool mqttPublishRaw( char* topic, char* value, bool retained );

// Human activity
void triggerActivity();

void mqttRegisterCallbacks( MQTT_CALLBACK, MQTT_CONNECT );

bool commsOTAEnabled();

void commsEnableOTA();
void commsRestart();
void commsClearTopicAndRestart( char* topic );
void commsClearTopicAndRestart( char* topic, char* topicVar1 );
void commsClearTopicAndRestart( char* topic, char* topicVar1, char* topicVar2 );

// Comms engine
void commsInit();

#endif
