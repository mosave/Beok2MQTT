#include <Arduino.h>
#include <errno.h>
#include "Config.h"
#include "Thermostat.h"
#include "Comms.h"

#pragma region Constants
static char* TOPIC_Locked PROGMEM = "Locked";
static char* TOPIC_On PROGMEM = "Power";
static char* TOPIC_Heating PROGMEM = "Heating";
static char* TOPIC_TargetSetManually PROGMEM = "TargetSetManually";
static char* TOPIC_RoomTemp PROGMEM = "RoomTemp";
static char* TOPIC_FloorTemp PROGMEM = "FloorTemp";
static char* TOPIC_FloorTempMax PROGMEM = "FloorTemp";
static char* TOPIC_TargetTemp PROGMEM = "TargetTemp";

static char* TOPIC_TargetTempMax PROGMEM = "TargetTempMax";
static char* TOPIC_TargetTempMin PROGMEM = "TargetTempMin";

static char* TOPIC_AutoMode PROGMEM = "AutoMode";
static char* TOPIC_LoopMode PROGMEM = "LoopMode";
static char* TOPIC_Sensor PROGMEM = "Sensor";
static char* TOPIC_Hysteresis PROGMEM = "Hysteresis";
static char* TOPIC_AdjTemp PROGMEM = "AdjTemp";
static char* TOPIC_AntiFroze PROGMEM = "AntiFroze";
static char* TOPIC_PowerOnMemory PROGMEM = "PowerOnMemory";
static char* TOPIC_DayOfWeek PROGMEM = "DayOfWeek";
static char* TOPIC_Time PROGMEM = "Time";

static char* TOPIC_SetTargetTemp PROGMEM = "SetTargetTemp";
static char* TOPIC_SetOn PROGMEM = "SetPower";
static char* TOPIC_SetLocked PROGMEM = "SetLocked";
static char* TOPIC_SetAutoMode PROGMEM = "SetAutoMode";
static char* TOPIC_SetLoopMode PROGMEM = "SetLoopMode";
#pragma endregion Constants

#pragma region Types and Vars
enum ThermWiFiState {
  Off,
  BlinkFast,
  Blink,
  On
};

char thermData[128];
int thermDataLen = 0;
// Current thermostat state
ThermState thermState;
// Published thermostat state
ThermState _thermState;
unsigned long thermLastStatus = 0;
unsigned long thermLastPublished = 0;
unsigned long thermActivityLocked = 0;
uint16 thermCRC = 0;

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
void thermCRCNext( uint8 nextByte) {
  char i;
  thermCRC ^= nextByte;
  for (i = 0; i < 8; i++) {
    if( thermCRC & 0x0001 ) {
      thermCRC = (thermCRC >> 1)  ^ 0xA001;
    } else {
      thermCRC >>= 1;
    }
  }
}
#pragma endregion

#pragma region Message Sending
void thermSendMessage( const char* data, bool appendCRC) {
  char hex[3] = {0,0,0};
  char* p = (char*)data;
  thermCRCStart();
  while ( *p>'\0' ) {
    hex[0] = *p; p++;
    hex[1] = *p; p++;
    if( *p == ' ') p++;
    uint8_t d = strtoul( hex, NULL, 16);
    thermCRCNext(d);
    therm.write(d);
  }
  if( appendCRC ) {
    therm.write(thermCRC & 0x00FF);
    therm.write(thermCRC >> 8 );
  }

#ifdef THERM_DEBUG
  char s[256];
  if( appendCRC ) {
    sprintf(s, "> %s:%02x%02x\n", data, (thermCRC & 0x00FF), (thermCRC >> 8) );
  } else {
    sprintf(s, "> %s\n", data );
  }
  mqttPublish("Log",s,false);
  aePrintf(s);
#endif
}

void thermSendMessage( const char* data) {
  thermSendMessage( data, true );
}

// 0: off
// 1: fast blink
// 2: slow blink
// 3: on
void thermSetWiFiSign(ThermWiFiState wifiState ) {
  char data[64];
  if( wifiState == ThermWiFiState::Off ) {
    strcpy(data, "a5a55a5a99c1e90300000000");
  } else {
//   0: BlinkFast
//   1: Blink
//   2: On
    uint8 mode = (wifiState == ThermWiFiState::BlinkFast) ? 0 : (wifiState == ThermWiFiState::Blink) ? 1 : 2;
    sprintf( data, "a5a55a5aa1c1ec0304000000 %02x 000000", mode );
  }
  thermSendMessage( data, false);
}

#pragma endregion

#pragma region ProcessMessage
bool thermProcessMessage() {
  if( thermDataLen < 3 ) return false;

  #ifdef THERM_DEBUG
    char s[255];
    char hex[4];
    strcpy( s, "< ");

    for(int i=0; i<thermDataLen; i++ ) {
      sprintf( hex, "%02x ", thermData[i]);
      if( i == thermDataLen-2 ) strcat(s, ": ");
      strcat( s, hex);
    }

    aePrintln(s);
    mqttPublish("Log",s,false);
  #endif
  

  thermCRCStart(); 
  for(int i=0; i<thermDataLen-2; i++ ) {
    thermCRCNext( thermData[i] );
  }

  if( (thermData[thermDataLen-2] != (thermCRC & 0xFF)) && (thermData[thermDataLen-2] != (thermCRC >> 8)) ) {
    aePrintln("Bad CRC");
    return false;
  }
  // Test if it is valid "Status" packet
  if( (thermDataLen > 22) // have at least 23 bytes
      && (thermData[0]==0x01) && (thermData[1]==0x03) // signature is for "Status" packet type
      && (thermData[11]>thermData[12]) // temperature range is valid
      && (thermData[22]>0) && (thermData[22]<8) // Week day is in range
      && (thermData[19]<24) && (thermData[20]<60) // Hours and minutes are in range
    ) {
    thermLastStatus = millis();

    thermState.locked = thermData[3] & 1;
    thermState.on = thermData[4] & 1;
    thermState.heating =  (thermData[4] >> 4) & 1;
    thermState.targetSetManually =  (thermData[4] >> 6) & 1;

    thermState.roomTemp =  (thermData[5] & 255) / 2.0;

    thermState.targetTemp =  (thermData[6] & 255)/2.0;
    thermState.targetTempMax = thermData[11];
    thermState.targetTempMin = thermData[12];

    thermState.floorTemp = (thermData[18] & 0xFF)/2.0;
    thermState.floorTempMax = thermData[9];

    thermState.autoMode =  thermData[7] & 0x01;
    thermState.loopMode =  (thermData[7] >> 4) & 0x0F;
    thermState.sensor = thermData[8];
    thermState.hysteresis = thermData[10] / 2.0;
    thermState.adjTemp = ((thermData[13] << 8) + thermData[14])/2.0;
    if( thermState.adjTemp > 32767 ) thermState.adjTemp = 32767 - thermState.adjTemp;
    thermState.antiFroze = (thermData[15] & 1);
    thermState.powerOnMemory = (thermData[16] & 1);

    thermState.hours =  thermData[19];
    thermState.minutes =  thermData[20];
    thermState.seconds =  thermData[21];
    thermState.dayOfWeek =  thermData[22];

    // //*** Thermostat time validation
    // $timeH = (int)date("G", time());
    // $timeM = (int)date("i", time());
    // $timeS = (int)date("s", time());
    // $timeD = (int)date("N", time());

    //                               // Get current time in week seconds
    // $timeCurrent = ( (($timeD-1)*24 + $timeH) * 60 + $timeM ) * 60 + $timeS;

    //                               // Get thermostat time in week seconds
    // $timeTherm = ( (($data['dayofweek']-1)*24 + $data['hour']) * 60 + $data['min'] ) * 60 + $payload[21];

    // // Delta time, seconds
    // $timeDelta = abs( $timeCurrent - $timeTherm);

    // // Compensate overflow with 1 minute confidence
    // if( $timeDelta >= 7*24*60*60 - 60 ) $timeDelta = abs(7*24*60*60 - $timeDelta);

    // // Check if thermostat time differs more than 30seconds
    // if ( $timeDelta > 30 ) {
    //   self::set_time($timeH,$timeM,$timeS,$timeD);
    // }
    return true;
  }
  return false;
}
#pragma endregion

#pragma region MQTT subscribtion handling
void thermConnect() {
  memset( &_thermState, 0xFF, sizeof(_thermState) );
  mqttSubscribeTopic( TOPIC_SetTargetTemp );
  mqttSubscribeTopic( TOPIC_SetOn );
  mqttSubscribeTopic( TOPIC_SetLocked );
  mqttSubscribeTopic( TOPIC_SetAutoMode );
  mqttSubscribeTopic( TOPIC_SetLoopMode );
  thermActivityLocked = millis();
}

bool thermCallback(char* topic, byte* payload, unsigned int length) {
  char s[64];
  if( mqttIsTopic( topic, TOPIC_SetTargetTemp ) ) {
    if( (payload != NULL) && (length > 0) && (length<31) ) {
      char s[31];
      memset( s, 0, sizeof(s) );
      strncpy( s, ((char*)payload), length );
      errno = 0;
      float temp = ((int)(strtof(s,NULL) * 2)) / 2.0 ;
      if ( (errno == 0) && (temp>=thermState.targetTempMin) && (temp<=thermState.targetTempMax) ) {
        if( temp != thermState.targetTemp ) {
          thermActivityLocked = millis();
          sprintf(s, "0106000100%02x", (int)(temp*2) );
          thermSendMessage( s );
          thermState.targetTemp = temp;
        }
      }
      mqttPublish(TOPIC_SetTargetTemp,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetOn ) ) {
    if( (payload != NULL) && (length==1) ) {
      char v = ( (char)*payload =='1' ) ? 1 : ( (char)*payload == '0' ) ? 0 : 99;
      if( (v<99) && (thermState.on != (bool)v) ) {
        thermActivityLocked = millis();
        thermState.on = (bool)v;
        char s[31];
        sprintf(s, "01060000%02x%02x", thermState.locked, v );
        thermSendMessage( s );
      }
      mqttPublish(TOPIC_SetOn,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetLocked ) ) {
    if( (payload != NULL) && (length==1) ) {
      char v = ( (char)*payload =='1' ) ? 1 : ( (char)*payload == '0' ) ? 0 : 99;
      if( (v<99) && (thermState.locked != (bool)v) ) {
        thermActivityLocked = millis();
        thermState.locked = (bool)v;
        char s[31];
        sprintf(s, "01060000%02x%02x", v, thermState.on );
        thermSendMessage( s );
      }
      mqttPublish(TOPIC_SetLocked,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetAutoMode ) ) {
    if( (payload != NULL) && (length==1) ) {
      uint8 v = ( (char)*payload == '1' ) ? 1 : ( (char)*payload == '0' ) ? 0 : 99;
      if( (v<99) && (thermState.autoMode != (bool)(v&1)) ){
        thermActivityLocked = millis();
        thermState.autoMode = (bool)(v&1);
        char s[31];
        sprintf(s, "01060002%02x%02x", ((thermState.loopMode << 4) | thermState.autoMode), thermState.sensor );
        thermSendMessage( s );
      }
      mqttPublish(TOPIC_SetAutoMode,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetLoopMode ) ) {
    if( (payload != NULL) && (length>0) && (length<31) ) {
      char s[31];
      memset( s, 0, sizeof(s) );
      strncpy( s, ((char*)payload), length );
      errno = 0;
      uint8 v = (uint8)atoi(s);
      if ( (errno == 0) && (v>=0) && (v<=0x0F) && (thermState.loopMode != (v&0x0F)) ) {
        thermActivityLocked = millis();
        thermState.loopMode = (v&0x0F);
        char s[31];
        sprintf(s, "01060002%02x%02x", ((thermState.loopMode << 4) | thermState.autoMode), thermState.sensor );
        thermSendMessage( s );
      }
      mqttPublish(TOPIC_SetLoopMode,(char*)NULL, false);
    }
    return true;
  }
  return false;
}
#pragma endregion

#pragma region State publishing
void thermTriggerActivity() {
  if( (unsigned long)(millis() - thermActivityLocked) > (unsigned long)5000 ) {
    triggerActivity();
  }
}

bool thermPublish( char* topic, float value, float* _value, bool retained, bool activity ) {
  if( (value != *_value ) && mqttPublish( topic, value, retained) ) {
    *_value = value;
    thermLastPublished = millis();
    if( activity ) thermTriggerActivity();
  }
}
bool thermPublish( char* topic, bool value, bool* _value, bool retained, bool activity ) {
  if( ((int)value != (int)(*_value) ) && mqttPublish( topic, value, retained) ) {
    *_value = value;
    thermLastPublished = millis();
    if( activity ) thermTriggerActivity();
  }
}

bool thermPublish( char* topic, int value, int* _value, bool retained, bool activity ) {
  if( (value != (*_value) ) && mqttPublish( topic, value, retained) ) {
    *_value = value;
    thermLastPublished = millis();
    if( activity ) thermTriggerActivity();
  }
}


void thermPublish() {
    unsigned long t = millis();
    // To avoid MQTT spam
    if( (thermLastStatus>0) && (unsigned long)(t - thermLastPublished) < (unsigned long)500 ) return;

    thermPublish( TOPIC_Locked, thermState.locked, &_thermState.locked, true, true );
    thermPublish( TOPIC_On, thermState.on, &_thermState.on, true, true );
    thermPublish( TOPIC_Heating, thermState.heating, &_thermState.heating, true, false );
    thermPublish( TOPIC_TargetSetManually, thermState.targetSetManually, &_thermState.targetSetManually, true, false );
    thermPublish( TOPIC_RoomTemp, thermState.roomTemp, &_thermState.roomTemp, true, false );
    thermPublish( TOPIC_TargetTemp, thermState.targetTemp, &_thermState.targetTemp, true, true );
    thermPublish( TOPIC_TargetTempMax, thermState.targetTempMax, &_thermState.targetTempMax, true, false );
    thermPublish( TOPIC_TargetTempMin, thermState.targetTempMin, &_thermState.targetTempMin, true, false );
    thermPublish( TOPIC_FloorTemp, thermState.floorTemp, &_thermState.floorTemp, true, false );
    thermPublish( TOPIC_FloorTempMax, thermState.floorTempMax, &_thermState.floorTempMax, true, false );
    thermPublish( TOPIC_AutoMode, thermState.autoMode, &_thermState.autoMode, true, true );
    thermPublish( TOPIC_LoopMode, thermState.loopMode, &_thermState.loopMode, true, false );
    thermPublish( TOPIC_Sensor, thermState.sensor, &_thermState.sensor, true, false );
    thermPublish( TOPIC_Hysteresis, thermState.hysteresis, &_thermState.hysteresis, true, false );
    thermPublish( TOPIC_AdjTemp, thermState.adjTemp, &_thermState.adjTemp, true, false );

    thermPublish( TOPIC_AntiFroze, thermState.antiFroze, &_thermState.antiFroze, true, false );
    thermPublish( TOPIC_PowerOnMemory, thermState.powerOnMemory, &_thermState.powerOnMemory, true, false );
    thermPublish( TOPIC_DayOfWeek, thermState.dayOfWeek, &_thermState.dayOfWeek, true, false );
    //thermPublish( TOPIC_Hours, thermState.hours, &_thermState.hours, true );
    //thermPublish( TOPIC_Minutes, thermState.minutes, &_thermState.minutes, true );
    if( (thermState.hours != _thermState.hours) || (thermState.minutes != _thermState.minutes) ) {
      char s[16];
      sprintf(s,"%02d:%02d", thermState.hours, thermState.minutes );
      if( mqttPublish( TOPIC_Time, s, true)) {
        _thermState.hours = thermState.hours;
        _thermState.minutes = thermState.minutes;
        thermLastPublished = millis();
      }
    }
}
#pragma endregion

#pragma region Init & Loop
void thermLoop() {
  static unsigned long lastRead = 0;
  static unsigned long lastMaintenance = 0;
  
	unsigned long t = millis();

  if( (thermDataLen>0) && (unsigned long)(t - lastRead) > (unsigned long)200 ) {
    thermProcessMessage();
    thermDataLen = 0;
  }

  // Read Thermostat MCU uart
  while (therm.available()>0) {
    // Check buffer overflow 
    if( thermDataLen >= sizeof(thermData)-1 ) {
      thermProcessMessage();
      thermDataLen=0;
    }

    thermData[thermDataLen++] = therm.read(); 
    lastRead = t;
    lastMaintenance = t;
  }

  // Periodical maintenance tasks
  if( (unsigned long)(t - lastMaintenance) > (unsigned long)500 ) {
    // Get MCU status every few seconds
    if( (unsigned long)(t - thermLastStatus) > (unsigned long)5000 ) {
      thermSendMessage( "010300000016");
    }
    static char _wifiState = 99;
    ThermWiFiState wifiState = 
        !wifiEnabled() ? ThermWiFiState::Off :
        !wifiConnected() ? ThermWiFiState::BlinkFast :
        !mqttConnected() ? ThermWiFiState::Blink : 
        ThermWiFiState::On;

    if( _wifiState != (char)wifiState) {
      thermSetWiFiSign( wifiState );
      _wifiState = (char)wifiState;
    }

    lastMaintenance = t;
  } else {
    thermPublish();
  }
}

void thermInit() {
  thermActivityLocked = millis();
	therm.begin(9600);
  mqttRegisterCallbacks( thermCallback, thermConnect );
	registerLoop(thermLoop);
}

#pragma endregion