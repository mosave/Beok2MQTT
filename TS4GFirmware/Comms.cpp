#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#else
#include <WiFi.h>
#include <ESPmDNS.h>
#endif

#include <errno.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#include "AELib.h"
#include "Comms.h"

#ifdef MQTT_Address
#elif MQTT_Port
#else
#define MQTT_MDNS
#define mqttMdnsSize 4

struct mqttMdnsRecord {
    char address[16];
    uint16_t port;
};
mqttMdnsRecord mqttMdns[mqttMdnsSize];
#endif

#ifdef TIMEZONE
#include "TZ.h"
#include <time.h>

#ifndef NTP_SERVER1
static char* NTP_SERVER1 PROGMEM = "time.google.com";
#endif

#ifndef NTP_SERVER2
static char* NTP_SERVER2 PROGMEM = "time.nist.gov";
#endif

#endif

//#define Debug

// Time to wait between connection attempts
#define COMMS_ConnectTimeout ((unsigned long)(60 * 1000))
#define COMMS_ConnectNextTimeout ((unsigned long)(3 * 1000))
// Number of connection attempts before resetting controller
#define COMMS_ConnectAttempts 1000000
// Time to wait between RSSI reports
#define COMMS_RSSITimeout ((unsigned long)(60 * 1000))

#define MQTT_ActivityTimeout ((unsigned long)(10 * 1000))
#define MQTT_CbsSize 10
#define MQTT_ClientId 16
#define MQTT_RootSize 32

static char* TOPIC_Online PROGMEM = "Online";
static char* TOPIC_DeviceInfo PROGMEM = "DeviceInfo";
static char* TOPIC_Activity PROGMEM = "Activity";
static char* TOPIC_Reset PROGMEM = "Reset";
static char* TOPIC_FactoryReset PROGMEM = "FactoryReset";
static char* TOPIC_EnableOTA PROGMEM = "EnableOTA";

#ifndef WIFI_HostName
static char* TOPIC_SetName PROGMEM = "SetName";
#endif

#ifndef MQTT_Root
static char* TOPIC_SetRoot PROGMEM = "SetRoot";
#endif

#ifndef TOPIC_HA_Status
static char* TOPIC_HA_Status PROGMEM = "homeassistant/status";
#endif


struct CommsConfig {
    // *********** Device configuration
    // Both device host name and MQTT client id  
    // Default is "ESP_<MAC_ADDR>" 
    // Use SetName MQTT command to change
    char hostName[32];

    // Top level of MQTT topic tree, trailing "/" is optional
    // An %s is to be replaced with device name.
    // Default is "new/%s".
    // Use SetRoot MQTT command to change
    char mqttRoot[MQTT_MAX_TOPIC_LEN];
    // *********** End of device configuration
    // Disable wifi/mqtt 
    bool disabled;
} commsConfig;

#define CONFIG_Timeout ((unsigned long)(15*60*1000))

// Templrary disable wifi/mqtt
bool commsOffline = false;
bool wifiTimeCritical = false;
unsigned long commsConnecting;
unsigned long commsConnectAttempt = 0;
unsigned long commsPaused;

int mqttMdnsIndex = 0;
int mqttMdnsCnt = 0;

unsigned long otaEnabled;
bool otaShouldInit;
unsigned int otaProgress;

unsigned long mqttActivity;
bool mqttDisableCallback = false;
char mqttServerAddress[32] = "";
bool commsHAConnected = false;
int commsRSSI = 0;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

struct MQTTCallbacks {
    MQTT_CALLBACK;
    MQTT_CONNECT;
};

unsigned int mqttCbsCount = 0;
MQTTCallbacks mqttCbs[MQTT_CbsSize];

//**************************************************************************
//                          WIFI helper functions
//**************************************************************************
#pragma region WiFi / Comm helpers
char* wifiHostName() {
    return commsConfig.hostName;
}

bool wifiConnected() {
    return
        !commsConfig.disabled &&
        !commsOffline &&
        (WiFi.status() == WL_CONNECTED);
}

bool mqttConnected() {
    return wifiConnected() && mqttClient.connected();
}

bool commsEnabled() {
    return !commsConfig.disabled;
}

void commsEnable() {
    aePrintln(F("WIFI: Enabling"));
    commsConfig.disabled = false;
    commsConnect();
}

void commsDisable() {
    commsDisconnect();
    aePrintln(F("WIFI: Disabling"));
    commsConfig.disabled = true;
}

void commsConnect() {
    if (commsConfig.disabled) return;
    commsOffline = false;

    if (commsConnectAttempt > 1) {
        aePrint(F("WIFI: Connecting, attempt "));
        aePrintln(commsConnectAttempt);
    } else {
        aePrintln(F("WIFI: Connecting"));
    }

    commsConnecting = millis();
    commsPaused = 0;

    WiFi.forceSleepWake();
    WiFi.setSleepMode(wifiTimeCritical ? WIFI_NONE_SLEEP : WIFI_LIGHT_SLEEP);

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    //if (commsConnectAttempt > 1) {
    //    WiFiPhyMode phyMode = WiFi.getPhyMode();
    //    switch (phyMode) {
    //    case WIFI_PHY_MODE_11N: phyMode = WIFI_PHY_MODE_11G; break;
    //    case WIFI_PHY_MODE_11G: phyMode = WIFI_PHY_MODE_11B; break;
    //    default: phyMode = WIFI_PHY_MODE_11N; break;
    //    }
    //    aePrint(F("WIFI: PhyMode=")); aePrintln(phyMode);
    //    WiFi.setPhyMode(phyMode);
    //}

    if (strlen(commsConfig.hostName) <= 0) {
        uint8_t macAddr[6];
        WiFi.macAddress(macAddr);
        sprintf(commsConfig.hostName, "ESP_%02X%02X%02X%02X%02X%02X", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
    }

    if (strlen(commsConfig.mqttRoot) <= 0) {
        strcpy(commsConfig.mqttRoot, "new/%s/");
    }
    storageSave();

    WiFi.hostname(commsConfig.hostName);
    WiFi.begin(WIFI_SSID, WIFI_Password);
}

void commsDisconnect() {
    if (mqttClient.connected()) {
        aePrintln(F("MQTT: Disconnecting"));
        mqttPublish(TOPIC_Online, (long)0, true);
        mqttClient.disconnect();
    }
    if (wifiConnected()) {
        aePrintln(F("WIFI: Disconnecting"));
    }
    MDNS.end();
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin(); // Wifi off
    commsOffline = true;
}

void commsReconnect() {
    if (commsConfig.disabled || commsOffline) return;
    commsDisconnect();

    commsConnectAttempt++;
    if (commsConnectAttempt >= COMMS_ConnectAttempts) {
        commsRestart();
    } else {
        commsConnect();
    }
}
#pragma endregion

//**************************************************************************
//                      MQTT helper functions
//**************************************************************************
#pragma region mqttTopic / mqttSubscribe / mqttPublish / mqttDeviceIdIs
char* mqttServer() {
    return mqttServerAddress;
}
char* mqttTopic(char* buffer, char* TOPIC_Name) {
    return mqttTopic(buffer, TOPIC_Name, NULL, NULL);
}
char* mqttTopic(char* buffer, char* TOPIC_Name, char* topicVar) {
    return mqttTopic(buffer, TOPIC_Name, topicVar, NULL);
}
char* mqttTopic(char* buffer, char* TOPIC_Name, char* topicVar1, char* topicVar2) {
    char fstr[MQTT_MAX_TOPIC_LEN + sizeof(commsConfig.hostName)]; // format string
    char empty[2] = ""; // to replace NULL variables

    sprintf(fstr, commsConfig.mqttRoot, commsConfig.hostName);
    // Append "/" to the end of path
    if (fstr[strlen(fstr) - 1] != '/') strcat(fstr, "/");
    // Delete "/" from the TOPIC_Name beginning
    while ((*TOPIC_Name) == '/') TOPIC_Name++;

    strcat(fstr, TOPIC_Name);
    sprintf(buffer, fstr, (topicVar1 != NULL) ? topicVar1 : empty, (topicVar2 != NULL) ? topicVar2 : empty);
    return(buffer);
}

// Check if "topic" string conforms TOPIC_Name template
bool mqttIsTopic(char* topic, char* TOPIC_Name) {
    char topicName[MQTT_MAX_TOPIC_LEN];
    return (strcmp(topic, mqttTopic(topicName, TOPIC_Name)) == 0);
}
bool mqttIsTopic(char* topic, char* TOPIC_Name, char* topicVar) {
    char topicName[MQTT_MAX_TOPIC_LEN];
    return (strcmp(topic, mqttTopic(topicName, TOPIC_Name, topicVar)) == 0);
}
bool mqttIsTopic(char* topic, char* TOPIC_Name, char* topicVar1, char* topicVar2) {
    char topicName[MQTT_MAX_TOPIC_LEN];
    return (strcmp(topic, mqttTopic(topicName, TOPIC_Name, topicVar1, topicVar2)) == 0);
}

// Wrappers to mqtt subscribtion
void mqttSubscribeTopic(char* TOPIC_Name) {
    mqttSubscribeTopic(TOPIC_Name, NULL, NULL);
}
void mqttSubscribeTopic(char* TOPIC_Name, char* topicVar) {
    mqttSubscribeTopic(TOPIC_Name, topicVar, NULL);
}
void mqttSubscribeTopic(char* TOPIC_Name, char* topicVar1, char* topicVar2) {
    char topic[MQTT_MAX_TOPIC_LEN];
    mqttSubscribeTopicRaw(mqttTopic(topic, TOPIC_Name, topicVar1, topicVar2));
}
void mqttSubscribeTopicRaw(char* topic) {
    mqttClient.subscribe(topic);
}

// Wrappers to mqtt publish function
bool mqttPublish(char* TOPIC_Name, long value, bool retained) {
    return mqttPublish(TOPIC_Name, NULL, NULL, value, retained);
}
bool mqttPublish(char* TOPIC_Name, char* topicVar, long value, bool retained) {
    return mqttPublish(TOPIC_Name, topicVar, NULL, value, retained);
}
bool mqttPublish(char* TOPIC_Name, char* topicVar1, char* topicVar2, long value, bool retained) {
    char topic[MQTT_MAX_TOPIC_LEN];
    return mqttPublishRaw(mqttTopic(topic, TOPIC_Name, topicVar1, topicVar2), value, retained);
}

bool mqttPublishRaw(char* topic, long value, bool retained) {
    char sValue[32];
    sprintf(sValue, "%ld", value);
    return mqttPublishRaw(topic, sValue, retained);
}

bool mqttPublish(char* TOPIC_Name, char* value, bool retained) {
    return mqttPublish(TOPIC_Name, NULL, NULL, value, retained);
}
bool mqttPublish(char* TOPIC_Name, char* topicVar, char* value, bool retained) {
    return mqttPublish(TOPIC_Name, topicVar, NULL, value, retained);
}
bool mqttPublish(char* TOPIC_Name, char* topicVar1, char* topicVar2, char* value, bool retained) {
    char topic[MQTT_MAX_TOPIC_LEN];
    return mqttPublishRaw(mqttTopic(topic, TOPIC_Name, topicVar1, topicVar2), value, retained);
}

bool mqttPublishRaw(char* topic, char* value, bool retained) {
    if (!mqttConnected()) return false;
#ifdef Debug  
    aePrint(F("Publish ")); aePrint(topic); aePrint(F("="));  aePrintln(value);
#endif  
    return mqttClient.publish(topic, value, retained);
}

// TRUE if:
// * deviceId is equals to wifi host name (ignoring case):
// * mqtt root ends with deviceId (ignoring case)
bool commsDeviceIdIs( char* deviceId, int len) {
    if (strlen(wifiHostName()) == len && strncasecmp(deviceId, wifiHostName(), len) == 0) {
        return true;
    }

    char s[64];

    if (len == 12) {
        uint8_t macAddr[6];
        WiFi.macAddress(macAddr);
        sprintf(s, "%02X%02X%02X%02X%02X%02X", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
        if (strncasecmp(deviceId, s, 12) == 0) {
            return true;
        }
    }

    sprintf(s, commsConfig.mqttRoot, commsConfig.hostName);

    // remove "/" at the end if any
    int rLen = strlen(s);
    if (s[rLen - 1] == '/') rLen--;

    return (len <= rLen && strncasecmp(deviceId, &s[rLen - len], len) == 0);
}

bool commsDeviceIdIs( char* deviceId) {
    int len = strlen(deviceId);
    return commsDeviceIdIs(deviceId, len);
}

void triggerActivity() {
    mqttActivity = millis();
}
#pragma endregion

#pragma region MQTT calbacks
// Regiater Callback and Connect functions to call on MQTT events
void mqttRegisterCallbacks(MQTT_CALLBACK, MQTT_CONNECT) {
    if (mqttCbsCount >= MQTT_CbsSize) return;
    mqttCbs[mqttCbsCount].callback = callback;
    mqttCbs[mqttCbsCount].connect = connect;
    mqttCbsCount++;
}

// Internal proxy function to process "default" topics
void mqttCallbackProxy(char* topic, byte* payload, unsigned int length) {
    if (mqttDisableCallback) return;

    for (int i = 0; i < mqttCbsCount; i++) {
        if (mqttCbs[i].callback != NULL) {
            if (mqttCbs[i].callback(topic, payload, length)) return;
        }
    }

    if (mqttIsTopic(topic, TOPIC_Reset)) {
        aePrintln(F("MQTT: Resetting by request"));
        commsClearTopicAndRestart(TOPIC_Reset);
    } else if (strcmp(topic, TOPIC_HA_Status) == 0) {
        if ((payload != NULL) && (length > 3) && (length < 63)) {
            commsHAConnected = (strncmp((const char*)payload, "online", 6) == 0);
        }
        aePrintf("HA: Connected=%d\r\n", commsHAConnected);
    } else if (mqttIsTopic(topic, TOPIC_FactoryReset)) {
        aePrintln(F("MQTT: Resetting settings"));
        storageReset();
        commsClearTopicAndRestart(TOPIC_FactoryReset);

#ifndef WIFI_HostName
    } else if (mqttIsTopic(topic, TOPIC_SetName)) {
        if ((payload != NULL) && (length > 1) && (length < 32)
            && ((strlen(commsConfig.hostName) != length) || strncmp(commsConfig.hostName, (char*)payload, length) != 0)) {
            memset(commsConfig.hostName, 0, sizeof(commsConfig.hostName));
            strncpy(commsConfig.hostName, ((char*)payload), length);
            commsConfig.hostName[length] = 0;
            aePrint(F("MQTT: Device name set to ")); aePrintln(commsConfig.hostName);
            commsRestart();
        }
#endif
#ifndef MQTT_Root
    } else if (mqttIsTopic(topic, TOPIC_SetRoot)) {
        if ((payload != NULL) && (length > 3) && (length < 63)
            && ((strlen(commsConfig.mqttRoot) != length) || strncmp(commsConfig.mqttRoot, (char*)payload, length) != 0)) {
            memset(commsConfig.mqttRoot, 0, sizeof(commsConfig.mqttRoot));
            strncpy(commsConfig.mqttRoot, ((char*)payload), length);
            aePrint(F("MQTT: Device root set to ")); aePrintln(commsConfig.mqttRoot);
            commsRestart();
        }
#endif
    } else if (mqttIsTopic(topic, TOPIC_EnableOTA)) {
        commsEnableOTA();
    }
}

void mqttPublishDeviceInfo() {
    char deviceInfo[256];
    IPAddress ip = WiFi.localIP();
    uint8_t macAddr[6];
    WiFi.macAddress(macAddr);
    commsRSSI = WiFi.RSSI();

#ifdef ESP32
    int espModelNo = 32;
#else
    int espModelNo = esp_is_8285() ? 8285 : 8266;
#endif

    static char* deviceInfoFormatString PROGMEM = "%s\nESP%d/%dMB/%uMHz\nMAC: %02X %02X %02X %02X %02X %02X\nSSID: %s/%ddB\nIP: %d.%d.%d.%d\nBroker: %s, %s\nAELib v%s";
#ifdef MQTT_User
    static char* deviceInfoAuthorization PROGMEM = "authorized";
#else
    static char* deviceInfoAuthorization PROGMEM = "anonymous";
#endif

    sprintf(deviceInfo, deviceInfoFormatString,
        commsConfig.hostName,
        espModelNo,
        (int)(ESP.getFlashChipRealSize() / 1024 / 1024),
        ESP.getCpuFreqMHz(),
        macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5],
        WIFI_SSID, commsRSSI,
        ip[0], ip[1], ip[2], ip[3],
        mqttServerAddress,
        deviceInfoAuthorization,
        AELIB_VERSION
    );

#ifdef VERSION
    static char* deviceInfoFirmwareString PROGMEM = "\nFirmware: ";
    strcat(strcat(deviceInfo, deviceInfoFirmwareString), VERSION);
#endif
    mqttPublish(TOPIC_DeviceInfo, deviceInfo, true);
}
#pragma endregion

//**************************************************************************
//                             Parsers
//**************************************************************************
#pragma region parsers implementation
bool parseBool(char* s, bool* b) {
#define strIs( str ) (strcasecmp(s, str) == 0)

    if (strIs("true") || strIs("on") || strIs("yes") || strIs("1")) {
        *b = true;
        return true;
    }
    if (strIs("false") || strIs("off") || strIs("no") || strIs("0")) {
        *b = false;
        return true;
    }
    return false;
}

bool parseInt(char* str, int min, int max, int* value) {
    float f = 0;
    if (parseFloat(str, (float)min, (float)max, &f)) {
        *value = round(f);
        return true;
    }
    return false;
}

bool parseFloat(char* str, float min, float max, float* value) {
    errno = 0;
    char* p;
    float t = strtof(str, &p);
    if ((errno == 0) && ((unsigned int)p > (unsigned int)str) && (*p == 0) && (t >= min) && (t <= max)) {
        *value = t;
        return true;
    }
    return false;
}
#pragma endregion

//**************************************************************************
//                            Home Assistant support
//**************************************************************************
#pragma region Home Assistant
bool haConnected() {
    if (!mqttConnected()) return false;
    return commsHAConnected;
}
#pragma endregion

//**************************************************************************
//                            Comms engine
//**************************************************************************
#pragma region Commmx main loop & initialization
void commsLoop() {

    if (commsConfig.disabled || commsOffline) return;

    static bool wasConnected = false;
    static unsigned long onlineReported = 0;
    static unsigned long rssiChecked = 0;

    unsigned long t = millis();
    // Check if connection is not timed out
    if (WiFi.status() == WL_CONNECTED) {  // WiFi is already connected
        if (commsConnecting > 0) {
            commsConnecting = 0;
            aePrint(F("WIFI: Connected as "));
#ifdef ESP8266
            aePrint(WiFi.hostname());
#else
            aePrint(WiFi.getHostname());
#endif
            aePrint(F("/")); aePrintln(WiFi.localIP());
            if (!MDNS.begin(commsConfig.hostName)) {
                aePrintln(F("MDNQ: begin() failed"));
            }

            return; // Split activity to not overload loop
        }

        // Handle OTA things
        if (otaEnabled > 0) {
            if (!timedOut(t, otaEnabled, CONFIG_Timeout)) {
                if (otaShouldInit) {
                    otaShouldInit = false;
                    aePrintln(F("OTA: Enabling OTA"));
                    ArduinoOTA.setHostname(commsConfig.hostName);
                    ArduinoOTA.setPassword(WIFI_Password);
                    ArduinoOTA.onStart([]() {
                        mqttPublish(TOPIC_Online, (long)0, true);
                        storageSave();
#ifdef LittleFS
                        LittleFS.end();
#endif
                        });
                    ArduinoOTA.onEnd([]() {
                        aePrintln(F("\nOTA: Firmware updated. Restarting"));
                        });
                    ArduinoOTA.onError([](ota_error_t error) {
                        aePrint(F("OTA: Error #")); aePrint(error); aePrintln(F(" updating firmware. Restarting"));
                        commsRestart();
                        });
                    ArduinoOTA.begin();
                    return;
                } else {
                    ArduinoOTA.handle();
                }
            } else {
                mqttPublish(TOPIC_Online, (long)0, true);
                aePrintln(F("OTA: Timeout waiting for update. Restarting"));
                commsRestart();
            }
        }

        // Handle MQTT connection and loops
        if (mqttClient.loop()) {
            wasConnected = true;
            static bool activityReported = false;
            bool a = (mqttActivity != 0) && (!timedOut(t, mqttActivity, MQTT_ActivityTimeout));
            if ((a != activityReported) && mqttPublish(TOPIC_Activity, a ? 1 : 0, false)) {
                activityReported = a;
            }

            if (timedOut(t, rssiChecked, 5000)) {
                int rssi = WiFi.RSSI();
                int d = (rssi > commsRSSI) ? rssi - commsRSSI : commsRSSI - rssi;
                if ((timedOut(t, rssiChecked, COMMS_RSSITimeout) && (d > 5)) || (d > 20)) {
                    mqttPublishDeviceInfo();
                }
                rssiChecked = t;
            }

            // Report online status every 10 minutes
            if (timedOut(t, onlineReported, (unsigned long)600000)) {
                onlineReported = t;
                mqttPublish(TOPIC_Online, (long)1, true);
            }


        } else {
            commsHAConnected = false;
            if (wasConnected) {
                aePrintln(F("MQTT: Connection lost"));
                wasConnected = false;
            }
            if (commsPaused == 0) {
                bool tryConnect = true;
                char willTopic[MQTT_MAX_TOPIC_LEN];
#ifdef MQTT_MDNS
                if (mqttMdnsCnt <= 0) {
                    mqttMdnsIndex = 0;
                    memset(mqttMdns, 0, sizeof(mqttMdns));

                    aePrintln(F("MQTT: Querying MDNS for broker"));

                    WiFi.setSleepMode(WIFI_NONE_SLEEP);
                    delay(100);
                    int n = 0;
                    for (int i = 0; i < 10 && n == 0; i++) {
                        yield();
                        n = MDNS.queryService("mqtt", "tcp", 1000U);
                    }
                    if (!wifiTimeCritical) {
                        WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
                    }

                    aePrintf("MQTT: %d brokers advertised:\n", n);
                    mqttMdnsCnt = 0;
                    for (int i = 0; i < n; ++i) {
                        IPAddress ip = MDNS.IP(i);
                        char address[16];
                        sprintf(address, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
                        bool valid = (ip[0] || ip[1] || ip[2] || ip[3]);
                        if (valid && (mqttMdnsCnt < mqttMdnsSize)) {
                            strcpy(mqttMdns[mqttMdnsCnt].address, address);
                            mqttMdns[mqttMdnsCnt].port = MDNS.port(i);
                            aePrintf("MQTT: + %s:%d\n", mqttMdns[mqttMdnsCnt].address, mqttMdns[mqttMdnsCnt].port);
                            mqttMdnsCnt++;
                        } else {
                            aePrintf("MQTT: - %s:%d ignored\n", address, MDNS.port(i));
                        }
                    }
                }

                if (mqttMdnsCnt > 0) {
                    mqttTopic(willTopic, "*");
                    aePrintf("MQTT: Connecting broker #%d %s:%d as %s\r\n", mqttMdnsIndex, mqttMdns[mqttMdnsIndex].address, mqttMdns[mqttMdnsIndex].port, willTopic);
                    mqttClient.setServer(mqttMdns[mqttMdnsIndex].address, mqttMdns[mqttMdnsIndex].port);
                    strcpy(mqttServerAddress, mqttMdns[mqttMdnsIndex].address);
                    mqttMdnsIndex++;
                    if (mqttMdnsIndex >= mqttMdnsCnt) {
                        mqttMdnsCnt = 0;
                        mqttMdnsIndex = 0;
                    }
                } else {
                    tryConnect = false;
                }
#else
                mqttTopic(willTopic, "*");
                aePrintf("MQTT: Connecting broker %s:%d as %s\r\n", MQTT_Address, MQTT_Port, willTopic);
                mqttClient.setServer(MQTT_Address, MQTT_Port);
                strcpy(mqttServerAddress, MQTT_Address);
#endif

                mqttClient.setCallback(mqttCallbackProxy);

                mqttTopic(willTopic, TOPIC_Online);
#if defined(MQTT_User) && defined(MQTT_Password)
                if (tryConnect && mqttClient.connect(commsConfig.hostName, MQTT_User, MQTT_Password, willTopic, 0, true, "0")) {
#else
                if (tryConnect && mqttClient.connect(commsConfig.hostName, willTopic, 0, true, "0")) {
#endif
                    commsConnectAttempt = 0;
                    aePrintln(F("MQTT: Connected"));
#ifdef MQTT_MAX_PACKET_SIZE
                    mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);
#endif

#ifdef TIMEZONE
                    // adjust time zone
                    configTime(TIMEZONE, mqttServerAddress, NTP_SERVER1, NTP_SERVER2);
                    tzset();
#endif  

                    // Subscribe
                    mqttSubscribeTopic(TOPIC_Reset);
                    mqttSubscribeTopic(TOPIC_FactoryReset);
                    mqttSubscribeTopic(TOPIC_EnableOTA);
                    mqttSubscribeTopicRaw(TOPIC_HA_Status);

#ifndef WIFI_HostName
                    mqttSubscribeTopic(TOPIC_SetName);
#endif  

#ifndef MQTT_Root
                    mqttSubscribeTopic(TOPIC_SetRoot);
#endif  
                    mqttPublish(TOPIC_Online, (long)1, true);
                    onlineReported = t;
                    mqttPublishDeviceInfo();

                    for (int i = 0; i < mqttCbsCount; i++) {
                        if (mqttCbs[i].connect != NULL) mqttCbs[i].connect();
                    }

                    commsPaused = 0;
                    wasConnected = true;
                } else {
                    aePrint(F("MQTT: Connection error #")); aePrintln(mqttClient.state());
                    commsPaused = t;
                }
                return; // Split activity to not overload loop
            } else {
#ifdef MQTT_MDNS 
                if (timedOut(t, commsPaused, ((mqttMdnsCnt > 0) ? COMMS_ConnectNextTimeout : COMMS_ConnectTimeout))) {
#else
                if (timedOut(t, commsPaused, COMMS_ConnectTimeout)) {
#endif
                    commsReconnect();
                }
            }
        }

    } else {
        commsHAConnected = false;
        if (timedOut(t, commsConnecting, COMMS_ConnectTimeout)) {
            aePrint(F("WIFI: Connection error #")); aePrintln(WiFi.status());
            commsReconnect();
        }
    }
}

bool commsOTAEnabled() {
    return (otaEnabled > 0);
}
void commsEnableOTA() {
    aePrintln(F("OTA: Enabled"));
    if (otaEnabled == 0) {
        otaShouldInit = true;
    }
    otaEnabled = millis();
    //Serial.end();
}

// Returns pointer to structure containing local time or NULL if local time is not yet synchronized.
tm* commsGetTime() {
#ifdef TIMEZONE
    time_t currentTime = time(nullptr);
    if (currentTime > 10000) {
        return localtime(&currentTime);
    } else {
        return NULL;
    }
#else
    return NULL;
#endif  
}

// TRUE if local time is synchronized 
bool commsTimeIsValid() {
    return (commsGetTime() != NULL);
}

void commsRestart() {
    storageSave();
    aePrintln(F("Restarting device..."));
    mqttPublish(TOPIC_Online, (long)0, true);
    delay(1000);;
    ESP.restart();
}
void commsClearTopicAndRestart(char* topic) {
    commsClearTopicAndRestart(topic, NULL, NULL);
}
void commsClearTopicAndRestart(char* topic, char* topicVar1) {
    commsClearTopicAndRestart(topic, topicVar1, NULL);
}
void commsClearTopicAndRestart(char* topic, char* topicVar1, char* topicVar2) {
    mqttDisableCallback = true;
    mqttPublish(topic, topicVar1, topicVar2, (char*)"", false);
    commsRestart();
}


void commsInit(bool isTimeCritical) {
    otaEnabled = 0;
    otaShouldInit = false;
    commsPaused = 0;
    mqttActivity = 0;
    wifiTimeCritical = isTimeCritical;
    storageRegisterBlock(COMMS_StorageId, &commsConfig, sizeof(commsConfig));
#ifdef WIFI_HostName
    uint8_t macAddr[6];
    char macS[16];
    WiFi.macAddress(macAddr);
    sprintf(macS, "%02X%02X%02X%02X%02X%02X", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
    sprintf(commsConfig.hostName, WIFI_HostName, macS);
#endif  

#ifdef MQTT_Root
    strcpy(commsConfig.mqttRoot, MQTT_Root);
#endif  
    commsConnect();
    aeRegisterLoop(commsLoop);
}

void commsInit() {
    commsInit(false);
}
#pragma endregion
