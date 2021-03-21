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
static char* TOPIC_Weekday PROGMEM = "Weekday";
static char* TOPIC_Time PROGMEM = "Time";

static char* TOPIC_Schedule PROGMEM = "Schedule";
static char* TOPIC_Schedule2 PROGMEM = "Schedule2";

static char* TOPIC_SetTargetTemp PROGMEM = "SetTargetTemp";
static char* TOPIC_SetOn PROGMEM = "SetPower";
static char* TOPIC_SetLocked PROGMEM = "SetLocked";
static char* TOPIC_SetAutoMode PROGMEM = "SetAutoMode";
static char* TOPIC_SetLoopMode PROGMEM = "SetLoopMode";
static char* TOPIC_SetSchedule PROGMEM = "SetSchedule";
static char* TOPIC_SetSchedule2 PROGMEM = "SetSchedule2";

static char* TOPIC_SetTime PROGMEM = "SetTime";
static char* TOPIC_SetWeekday PROGMEM = "SetWeekday";
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
    thermState.weekday =  thermData[22];

    // //*** Thermostat time validation
    // $timeH = (int)date("G", time());
    // $timeM = (int)date("i", time());
    // $timeS = (int)date("s", time());
    // $timeD = (int)date("N", time());

    //                               // Get current time in week seconds
    // $timeCurrent = ( (($timeD-1)*24 + $timeH) * 60 + $timeM ) * 60 + $timeS;

    //                               // Get thermostat time in week seconds
    // $timeTherm = ( (($data['weekday']-1)*24 + $data['hour']) * 60 + $data['min'] ) * 60 + $payload[21];

    // // Delta time, seconds
    // $timeDelta = abs( $timeCurrent - $timeTherm);

    // // Compensate overflow with 1 minute confidence
    // if( $timeDelta >= 7*24*60*60 - 60 ) $timeDelta = abs(7*24*60*60 - $timeDelta);

    // // Check if thermostat time differs more than 30seconds
    // if ( $timeDelta > 30 ) {
    //   self::set_time($timeH,$timeM,$timeS,$timeD);
    // }

    // If status packet have schedule data
    if( thermDataLen > 46 ) {
      for (int i = 0; i < 6; i++) {
        thermState.schedule[i].h = thermData[2*i + 23];
        thermState.schedule[i].m = thermData[2*i + 24];
        thermState.schedule[i].t = (float)(thermData[i + 39]/2.0);
        //aePrintf("%d: %d %d %f\n", i, thermState.schedule[i].h, thermState.schedule[i].m, thermState.schedule[i].t );
        if( i<2 ) {
          thermState.schedule2[i].h = thermData[2*(i+6) + 23];
          thermState.schedule2[i].m = thermData[2*(i+6) + 24];
          thermState.schedule2[i].t = (float)   (thermData[ i + 6  + 39]/2.0);
        }
      }
    }
    return true;
  }
  return false;
}
#pragma endregion

#pragma region Schedule helpers
char* thermPrintSchedule(char* s, ThermScheduleRecord schedule[], int recordCount ) {
  *s=0;
  char sr[16];
  char sf[8];
  if( (schedule[0].h>23) || (schedule[0].m>30) ) return s;

  for(int i=0; i<recordCount; i++ ){
    dtostrf( schedule[i].t, 4,1, sf );
    char* p = sf;
    while( *p==' ') p++;

    sprintf(sr,"%02d:%02d %s",schedule[i].h, schedule[i].m, p );
    strcat(s,sr);
    if(i+1<recordCount) strcat(s, ";");
  }
  return s;
}

void thermParseSchedule( char* payload, unsigned int length, ThermScheduleRecord schedule[], int recordCount ) {
  if( (payload==NULL) || (length<10) ) return;
  char s[256];
  char s2[16];
  ThermScheduleRecord sch[6];
  char* p = s;
  bool changed = false;
  errno = 0;
  memset(s, 0, sizeof(s));
  strncpy(s, payload,length);

  for(int i=0; i<recordCount; i++) {
    if( *p == 0 ) return;

    sch[i].h = (int8)strtol( p, &p, 10 );
    if( (errno != 0) || (sch[i].h<0) || (sch[i].h>23) ) return;
    while( (*p != 0) && ( (*p<'0') || (*p>'9') ) ) p++;

    sch[i].m = (int8)strtol( p, &p, 10 );
    if( (errno != 0) || (sch[i].m<0) || (sch[i].m>60) ) return;
    while( (*p != 0) && ( (*p<'0') || (*p>'9') ) ) p++;

    sch[i].t = ((int)(strtof( p, &p )*2))/2.0;
    if( (errno != 0) || (sch[i].t < thermState.targetTempMin) || (sch[i].t > thermState.targetTempMax) ) return;
    while( (*p != 0) && ( (*p<'0') || (*p>'9') ) ) p++;

    if( (sch[i].h != schedule[i].h ) || (sch[i].m != schedule[i].m ) || ( (int)(sch[i].t*2) != (int)(schedule[i].t*2) ) ) changed = true;
  }
  
  if( !changed ) return;
  
  //aePrintln(thermPrintSchedule( s, sch, recordCount ));
  memcpy( schedule, sch, sizeof(ThermScheduleRecord)*recordCount);

  strcpy(s, "0110000a000c18" );
  for ( int i = 0; i < 6; i++) {
    sprintf(s2, "%02x%02x ", thermState.schedule[i].h, thermState.schedule[i].m );
    strcat(s,s2);
  }
  for ( int i = 0; i < 2; i++) {
    sprintf(s2, "%02x%02x ", thermState.schedule2[i].h, thermState.schedule2[i].m );
    strcat(s,s2);
  }
  for ( int i = 0; i < 6; i++) {
    sprintf(s2, "%02x ", (int)(thermState.schedule[i].t*2) );
    strcat(s,s2);
  }
  for ( int i = 0; i < 2; i++) {
    sprintf(s2, "%02x ", (int)(thermState.schedule2[i].t*2) );
    strcat(s,s2);
  }
  aePrintln(s);
  thermSendMessage(s);
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
  mqttSubscribeTopic( TOPIC_SetSchedule );
  mqttSubscribeTopic( TOPIC_SetSchedule2 );
  mqttSubscribeTopic( TOPIC_SetTime );
  mqttSubscribeTopic( TOPIC_SetWeekday );
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
  } else if( mqttIsTopic( topic, TOPIC_SetSchedule ) ) {
    if( (payload != NULL) && (length>10) && (length<255) ) {
      thermParseSchedule( (char*)payload, length, thermState.schedule, 6 );
      mqttPublish(TOPIC_SetSchedule,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetSchedule2 ) ) {
    if( (payload != NULL) && (length>10) && (length<255) ) {
      thermParseSchedule( (char*)payload, length, thermState.schedule2, 2 );
      mqttPublish(TOPIC_SetSchedule2,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetWeekday ) ) {
    if( (payload != NULL) && (length>0) ) {
      if ( (length==1) && (*payload >='1') && (*payload <='7') ) {
        int v = (*payload)-'0';
        if( thermState.weekday != v ) {
          thermActivityLocked = millis();
          thermState.weekday = v;
          char s[31];
          //0x01,0x10,0x00,0x08,0x00,0x02,0x04,$hour,$minute,$second,$day));
          sprintf(s, "01100008000204%02x%02x%02x%02x", thermState.hours, thermState.minutes, thermState.seconds, thermState.weekday );
          thermSendMessage( s );
        }
      }
      mqttPublish(TOPIC_SetWeekday,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetTime ) ) {
    if( (payload != NULL) && (length>0) && (length<=5) ) {
      char s[8];
      memset(s, 0, sizeof(s));
      memcpy(s, payload, length);
      char* p = s;
      errno = 0;
      int h = (int8)strtol( p, &p, 10 );
      aePrintln(h);
      if( (errno == 0) && ( *p != 0 ) && (h>=0) && (h<=23) ) {
        while( (*p != 0) && ( (*p<'0') || (*p>'9') ) ) p++;
        int m = (int8)strtol( p, NULL, 10 );
        aePrintln(m);
        if( (errno == 0) && (m>=0) && (m<=59) && ((thermState.hours != h) || (thermState.minutes != m)) ) {
          thermActivityLocked = millis();
          thermState.hours = h;
          thermState.minutes = m;
          thermState.seconds = 0;
          char s[31];
          sprintf(s, "01100008000204%02x%02x%02x%02x", thermState.hours, thermState.minutes, thermState.seconds, thermState.weekday );
          thermSendMessage( s );
        }
      }
      mqttPublish(TOPIC_SetTime,(char*)NULL, false);
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
  if( (value != *_value ) ) {
    char s[8];
    dtostrf( value, 4,1, s );
    char* p = s;
    while( *p==' ') p++;
    if( mqttPublish( topic, p, retained) ) {
      *_value = value;
      thermLastPublished = millis();
      if( activity ) thermTriggerActivity();
    }
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

    char s[128];
    char s1[128];

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
    thermPublish( TOPIC_Weekday, thermState.weekday, &_thermState.weekday, true, false );
    if( (thermState.hours != _thermState.hours) || (thermState.minutes != _thermState.minutes) ) {
      sprintf(s,"%02d:%02d", thermState.hours, thermState.minutes );
      if( mqttPublish( TOPIC_Time, s, true)) {
        _thermState.hours = thermState.hours;
        _thermState.minutes = thermState.minutes;
        thermLastPublished = millis();
      }
    }
    thermPrintSchedule(s,thermState.schedule, 6);
    thermPrintSchedule(s1,_thermState.schedule, 6);
    if( (strlen(s)>0) && (strcmp(s, s1)!=0) ) {
      if( mqttPublish( TOPIC_Schedule, s, true)) {
        memcpy(_thermState.schedule, thermState.schedule, sizeof(_thermState.schedule));
        thermLastPublished = millis();
      }
    }

    thermPrintSchedule(s,thermState.schedule2, 2);
    thermPrintSchedule(s1,_thermState.schedule2, 2);
    if( (strlen(s)>0) && (strcmp(s, s1)!=0) ) {
      if( mqttPublish( TOPIC_Schedule2, s, true)) {
        memcpy(_thermState.schedule2, thermState.schedule2, sizeof(_thermState.schedule2));
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