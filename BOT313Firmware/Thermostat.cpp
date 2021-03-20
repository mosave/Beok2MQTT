#include <Arduino.h>
#include <errno.h>
#include "Config.h"
#include "Thermostat.h"
#include "Comms.h"

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



enum ThermPacketType {
  Ignore,
  Status
};

char thermData[128];
int thermLen = 0;
// Current thermostat state
ThermState thermState;
// Published thermostat state
ThermState _thermState;
ThermPacketType thermPacketType;
unsigned long thermPacketRequested = 0;
unsigned long thermLastRead = 0;
unsigned long thermLastStatus = 0;
unsigned long thermLastPublished = 0;
uint16 thermCRC = 0;

#ifdef USE_SOFT_SERIAL
	#include <SoftwareSerial.h>
	SoftwareSerial therm(THERM_RX, THERM_TX);
#else
	#define therm Serial
#endif

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
void thermSendMessage( ThermPacketType packetType, const char* data) {
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
  therm.write(thermCRC & 0x00FF);
  therm.write(thermCRC >> 8 );
  thermPacketType = packetType;
  thermPacketRequested = millis();

#ifdef THERM_DEBUG
  char s[256];
  sprintf(s, "> %s:%02x%02x\n", data, (thermCRC & 0x00FF), (thermCRC >> 8) );
  mqttPublish("Log",s,false);
  aePrintf(s);
#endif
}

// void thermSendMessage( ThermPacketType packetType, const __FlashStringHelper* data) {
//   PGM_P p = reinterpret_cast<PGM_P>(data);
//   char s[255] __attribute__ ((aligned(4)));
//   size_t n = std::min(sizeof(s)-1, strlen_P(p) );
//   memcpy_P(s, p, n);
//   s[n] = 0;
//   thermSendMessage( packetType, s );
// }

void thermSendMessage( const char* data ) {
  thermSendMessage( ThermPacketType::Ignore, data );
}

#pragma endregion

#pragma region ProcessMessage
bool thermProcessMessage() {
  if( thermLen < 3 ) {
    thermLen = 0;
    return false;
  }

  #ifdef THERM_DEBUG
    if( thermLen==0 ) return false;
    char s[255];
    char hex[4];
    strcpy( s, "< ");

    for(int i=0; i<thermLen; i++ ) {
      sprintf( hex, "%02x ", thermData[i]);
      if( i == thermLen-2 ) strcat(s, ": ");
      strcat( s, hex);
    }

    aePrintln(s);
    mqttPublish("Log",s,false);
  #endif
  if( ((unsigned long)(millis() - thermPacketRequested) > (unsigned long)2000) || (thermPacketType == ThermPacketType::Ignore) ) {
    thermPacketType = ThermPacketType::Ignore;
    thermPacketRequested = 0;
  }


  thermCRCStart(); 
  for(int i=0; i<thermLen-2; i++ ) {
    thermCRCNext( thermData[i] );
  }

  if( (thermData[thermLen-2] != (thermCRC & 0xFF)) && (thermData[thermLen-2] != (thermCRC >> 8)) ) {
    aePrintln("Bad CRC");
    thermLen = 0;
    return false;
  }
  switch( thermPacketType ) {
    case ThermPacketType::Status: {
      // Just in case - check data validity:
      if( (thermLen > 22) // have at least 23 bytes
          && (thermData[11]>thermData[12]) // temperature range is valid
          && (thermData[22]>0) && (thermData[22]<8) // Week day is in range
          && (thermData[19]<24) && (thermData[20]<60) // Hours and minutes are in range
        ) {
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
      }
      return true;
    }
  }
  thermLen = 0;
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

}

bool thermCallback(char* topic, byte* payload, unsigned int length) {
  char s[64];
  if( mqttIsTopic( topic, TOPIC_SetTargetTemp ) ) {
    if( (payload != NULL) && (length > 0) && (length<31) ) {
      char s[31];
      memset( s, 0, sizeof(s) );
      strncpy( s, ((char*)payload), length );
      errno = 0;
      float temp = strtof(s,NULL);
      if ( (errno == 0) && (temp>=thermState.targetTempMin) && (temp<=thermState.targetTempMax) ) {
        sprintf(s, "01060002%02x", 0, (int)(temp*2) );
        thermSendMessage( ThermPacketType::Ignore, s );
        thermState.targetTemp = temp;
      }
      mqttPublish(TOPIC_SetTargetTemp,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetOn ) ) {
    if( (payload != NULL) && (length==1) ) {
      char v = ( (char)*payload =='1' ) ? 1 : ( (char)*payload == '0' ) ? 0 : 99;
      if( v<99) {
        char s[31];
        sprintf(s, "01060000%02x%02x", thermState.locked, v );
        thermSendMessage( ThermPacketType::Ignore, s );
        thermState.on = (bool)v;
        mqttPublish(TOPIC_SetOn,(char*)NULL, false);
      }
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetLocked ) ) {
    if( (payload != NULL) && (length==1) ) {
      char v = ( (char)*payload =='1' ) ? 1 : ( (char)*payload == '0' ) ? 0 : 99;
      if( v<99) {
        char s[31];
        sprintf(s, "01060000%02x%02x", v, thermState.on );
        thermSendMessage( ThermPacketType::Ignore, s );
        thermState.locked = (bool)v;
        mqttPublish(TOPIC_SetLocked,(char*)NULL, false);
      }
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetAutoMode ) ) {
    if( (payload != NULL) && (length==1) ) {
      uint8 v = ( (char)*payload == '1' ) ? 1 : ( (char)*payload == '0' ) ? 0 : 99;
      if( v<99) {
        char s[31];
        thermState.autoMode = (bool)(v&1);
        sprintf(s, "01060002%02x%02x", ((thermState.loopMode << 4) | thermState.autoMode), thermState.sensor );
        thermSendMessage( ThermPacketType::Ignore, s );
        mqttPublish(TOPIC_SetAutoMode,(char*)NULL, false);
      }
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetLoopMode ) ) {
    if( (payload != NULL) && (length>0) && (length<31) ) {
      char s[31];
      memset( s, 0, sizeof(s) );
      strncpy( s, ((char*)payload), length );
      errno = 0;
      uint8 v = (uint8)atoi(s);
      if ( (errno == 0) && (v>=0) && (v<=0x0F) ) {
        char s[31];
        thermState.loopMode = (v&0x0F);
        sprintf(s, "01060002%02x%02x", ((thermState.loopMode << 4) | thermState.autoMode), thermState.sensor );
        thermSendMessage( ThermPacketType::Ignore, s );
        mqttPublish(TOPIC_SetLoopMode,(char*)NULL, false);
      }
    }
    return true;
  }
  return false;
}
#pragma endregion

#pragma region State publishing

bool thermPublish( char* topic, float value, float* _value, bool retained ) {
  if( (value != *_value ) && mqttPublish( topic, value, retained) ) {
    *_value = value;
    thermLastPublished = millis();
  }
}
bool thermPublish( char* topic, bool value, bool* _value, bool retained ) {
  if( ((int)value != (int)(*_value) ) && mqttPublish( topic, value, retained) ) {
    *_value = value;
    thermLastPublished = millis();
  }
}

bool thermPublish( char* topic, int value, int* _value, bool retained ) {
  if( (value != (*_value) ) && mqttPublish( topic, value, retained) ) {
    *_value = value;
    thermLastPublished = millis();
  }
}


void thermPublish() {
    unsigned long t = millis();
    // To avoid MQTT spam
    if( (thermLastStatus>0) && (unsigned long)(t - thermLastPublished) < (unsigned long)500 ) return;

    //thermPublish( TOPIC_, thermState., &_thermState., true );
    thermPublish( TOPIC_Locked, thermState.locked, &_thermState.locked, true );
    thermPublish( TOPIC_On, thermState.on, &_thermState.on, true );
    thermPublish( TOPIC_Heating, thermState.heating, &_thermState.heating, true );
    thermPublish( TOPIC_TargetSetManually, thermState.targetSetManually, &_thermState.targetSetManually, true );
    thermPublish( TOPIC_RoomTemp, thermState.roomTemp, &_thermState.roomTemp, true );
    thermPublish( TOPIC_TargetTemp, thermState.targetTemp, &_thermState.targetTemp, true );
    thermPublish( TOPIC_TargetTempMax, thermState.targetTempMax, &_thermState.targetTempMax, true );
    thermPublish( TOPIC_TargetTempMin, thermState.targetTempMin, &_thermState.targetTempMin, true );
    thermPublish( TOPIC_FloorTemp, thermState.floorTemp, &_thermState.floorTemp, true );
    thermPublish( TOPIC_FloorTempMax, thermState.floorTempMax, &_thermState.floorTempMax, true );
    thermPublish( TOPIC_AutoMode, thermState.autoMode, &_thermState.autoMode, true );
    thermPublish( TOPIC_LoopMode, thermState.loopMode, &_thermState.loopMode, true );
    thermPublish( TOPIC_Sensor, thermState.sensor, &_thermState.sensor, true );
    thermPublish( TOPIC_Hysteresis, thermState.hysteresis, &_thermState.hysteresis, true );
    thermPublish( TOPIC_AdjTemp, thermState.adjTemp, &_thermState.adjTemp, true );

    thermPublish( TOPIC_AntiFroze, thermState.antiFroze, &_thermState.antiFroze, true );
    thermPublish( TOPIC_PowerOnMemory, thermState.powerOnMemory, &_thermState.powerOnMemory, true );
    thermPublish( TOPIC_DayOfWeek, thermState.dayOfWeek, &_thermState.dayOfWeek, true );
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
	unsigned long t = millis();

  // Read from the Bluetooth module and send to the Arduino Serial Monitor
  while (therm.available()>0) {
    // Packet read timeout
    if( (unsigned long)(t - thermLastRead) > (unsigned long)200 ) {
      if( thermLen>1 ) thermProcessMessage();
      thermLen = 0;
    }
    // Packet is too long
    if( thermLen >= sizeof(thermData)-1 ) {
      thermProcessMessage();
      thermLen=0;
    }

    thermData[thermLen++] = therm.read(); 
    thermLastRead = t;
  }

  if( (unsigned long)(t - thermLastRead) > (unsigned long)1000 ) {
    if( (unsigned long)(t - thermLastStatus) > (unsigned long)5000 ) {
      // Get Status (a'la heartbeat)
      thermSendMessage( ThermPacketType::Status, "010300000016");
      thermLastStatus = t;
    } else {
      //thermLastRead = t;
    }
  }
  thermPublish();
}

void thermInit() {
	therm.begin(9600);
  mqttRegisterCallbacks( thermCallback, thermConnect );
	registerLoop(thermLoop);
}

#pragma endregion