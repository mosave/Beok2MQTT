#include <Arduino.h>
#include <errno.h>
#include "Config.h"
#include "Thermostat.h"
#include "Comms.h"
#include "Storage.h"

#ifdef USE_HTU21D
  #include "TAH_HTU21D.h"
#endif
#pragma region Constants

#ifdef TIMEZONE
  #include <time.h>
#endif


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

static char* TOPIC_SetAutoMode PROGMEM = "SetAutoMode";
static char* TOPIC_SetLoopMode PROGMEM = "SetLoopMode";
static char* TOPIC_SetSensor PROGMEM = "SetSensor";
static char* TOPIC_Hysteresis PROGMEM = "Hysteresis";
static char* TOPIC_SetAdjTemp PROGMEM = "SetAdjTemp";

#ifdef USE_HTU21D
static char* TOPIC_SetAutoAdjMode PROGMEM = "SetAutoAdjMode";
#endif
static char* TOPIC_SetAntiFroze PROGMEM = "SetAntiFroze";
static char* TOPIC_PowerOnMemory PROGMEM = "PowerOnMemory";
static char* TOPIC_SetWeekday PROGMEM = "SetWeekday";
static char* TOPIC_SetTime PROGMEM = "SetTime";

static char* TOPIC_SetSchedule PROGMEM = "SetSchedule";
static char* TOPIC_SetSchedule2 PROGMEM = "SetSchedule2";



#define P3(str) ((char*)(((uint)str)+3))
#pragma endregion Constants

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
// Current thermostat state
ThermState thermState;
// Published thermostat state
ThermState _thermState;
unsigned long thermLastStatusRequest = 0;
unsigned long thermLastStatus = 0;
unsigned long thermLastPublished = 0;
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

void thermSendAdvancedParams() {
  char data[64];
  int16_t a = (int16_t)(thermState.adjTemp*2.0);
  thermActivityLocked = millis();
  
//  sprintf(data, "%d %02x %02x ", (int16_t)(thermState.adjTemp*2.0), (a>>8) & 0xFF, a & 0xFF  );
//  mqttPublish("Log",data,false);
  
  sprintf( data, "0110000200050a %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
    thermState.loopMode,
    thermState.sensor,
    (int)thermState.floorTempMax,
    (int)(thermState.hysteresis*2.0),
    (int)thermState.targetTempMax,
    (int)thermState.targetTempMin,
    (a>>8) & 0xFF, a & 0xFF,
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
      //&& (thermData[22]>0) && (thermData[22]<8) // Week day is in range
      //&& (thermData[19]<24) && (thermData[20]<60) // Hours and minutes are in range
    ) {
      
    thermLastStatus = millis();
    thermLastStatusRequest = thermLastStatus;

    thermState.locked = thermData[3] & 1;
    thermState.power = thermData[4] & 1;
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
    
    thermState.adjTemp = ((int16_t)((thermData[13] << 8) + thermData[14]))/2.0;
    
    thermState.antiFroze = (thermData[15] & 1);
    thermState.powerOnMemory = (thermData[16] & 1);

    thermState.hours =  thermData[19];
    thermState.minutes =  thermData[20];
    thermState.seconds =  thermData[21];
    thermState.weekday =  thermData[22];

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
#ifdef USE_HTU21D    
    if( (thermState.sensor==0) && thermConfig.autoAdjMode ) {
      float t = 0;
      if( thermConfig.autoAdjMode==1 ) {
        t = tahGetTemperature();
      } else if( thermConfig.autoAdjMode==2 ) {
        t = tahGetHeatIndex();
      }
      float delta = thermState.roomTemp-t;
      if( delta <0 ) delta = -delta;
      if( (t != 0) && (delta>0.75) ) {
        delta = t - (thermState.roomTemp - thermState.adjTemp);
        thermState.adjTemp = (float)((int)(delta * 2)) / 2.0;
        thermSendAdvancedParams();
      }
    }
#endif
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
  mqttSubscribeTopic( TOPIC_SetPower );
  mqttSubscribeTopic( TOPIC_SetLocked );
  mqttSubscribeTopic( TOPIC_SetAutoMode );
  mqttSubscribeTopic( TOPIC_SetLoopMode );
  mqttSubscribeTopic( TOPIC_SetSchedule );
  mqttSubscribeTopic( TOPIC_SetSchedule2 );
  mqttSubscribeTopic( TOPIC_SetTime );
  mqttSubscribeTopic( TOPIC_SetWeekday );
  mqttSubscribeTopic( TOPIC_SetSensor );
  mqttSubscribeTopic( TOPIC_SetAdjTemp );
  mqttSubscribeTopic( TOPIC_SetAntiFroze );
  mqttSubscribeTopic( TOPIC_SetFloorTempMax );
  
#ifdef USE_HTU21D
  mqttSubscribeTopic( TOPIC_SetAutoAdjMode );
#endif
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
  } else if( mqttIsTopic( topic, TOPIC_SetAdjTemp ) ) {
    if( (payload != NULL) && (length > 0) && (length<31) ) {
      char s[31];
      memset( s, 0, sizeof(s) );
      strncpy( s, ((char*)payload), length );
      errno = 0;
      float adjTemp = ((int)(strtof(s,NULL) * 2)) / 2.0 ;
      if ( (errno == 0) && (adjTemp>=-10) && (adjTemp<=10) ) {
        thermState.adjTemp = adjTemp;
        thermSendAdvancedParams();
      }
      mqttPublish(TOPIC_SetAdjTemp,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetFloorTempMax ) ) {
    if( (payload != NULL) && (length > 0) && (length<31) ) {
      char s[31];
      memset( s, 0, sizeof(s) );
      strncpy( s, ((char*)payload), length );
      errno = 0;
      int ftMax = (int)strtof(s,NULL);
      if ( (errno == 0) && (ftMax>=20) && (ftMax<=45) ) {
        thermState.floorTempMax = ftMax;
        thermSendAdvancedParams();
      }
      mqttPublish(TOPIC_SetFloorTempMax,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetAntiFroze ) ) {
    if( (payload != NULL) && (length==1) ) {
      uint8 v = ( (char)*payload == '1' ) ? 1 : ( (char)*payload == '0' ) ? 0 : 99;
      if( (v<99) && (thermState.antiFroze != (bool)(v&1)) ){
        thermState.antiFroze = (bool)(v&1);
        thermSendAdvancedParams();
      }
      mqttPublish(TOPIC_SetAntiFroze,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetPower ) ) {
    if( (payload != NULL) && (length==1) ) {
      char v = ( (char)*payload =='1' ) ? 1 : ( (char)*payload == '0' ) ? 0 : 99;
      if( (v<99) && (thermState.power != (bool)v) ) {
        thermActivityLocked = millis();
        thermState.power = (bool)v;
        char s[31];
        sprintf(s, "01060000%02x%02x", thermState.locked?1:0, v );
        thermSendMessage( s );
      }
      mqttPublish(TOPIC_SetPower,(char*)NULL, false);
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_SetLocked ) ) {
    if( (payload != NULL) && (length==1) ) {
      char v = ( (char)*payload =='1' ) ? 1 : ( (char)*payload == '0' ) ? 0 : 99;
      if( (v<99) && (thermState.locked != (bool)v) ) {
        thermActivityLocked = millis();
        thermState.locked = (bool)v;
        char s[31];
        sprintf(s, "01060000%02x%02x", v, thermState.power?1:0 );
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
  } else if( mqttIsTopic( topic, TOPIC_SetSensor ) ) {
    if( (payload != NULL) && (length>0) && (length<31) ) {
      char s[31];
      memset( s, 0, sizeof(s) );
      strncpy( s, ((char*)payload), length );
      errno = 0;
      uint8 v = (uint8)atoi(s);
      if ( (errno == 0) && (v>=0) && (v<=0x0F) && (thermState.sensor != (v&0x0F)) ) {
        thermActivityLocked = millis();
        thermState.sensor = (v&0x0F);
        char s[31];
        sprintf(s, "01060002%02x%02x", ((thermState.loopMode << 4) | thermState.autoMode), thermState.sensor );
        thermSendMessage( s );
      }
      mqttPublish(TOPIC_SetSensor,(char*)NULL, false);
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
      if( (errno == 0) && ( *p != 0 ) && (h>=0) && (h<=23) ) {
        while( (*p != 0) && ( (*p<'0') || (*p>'9') ) ) p++;
        int m = (int8)strtol( p, NULL, 10 );
        if( (errno == 0) && (m>=0) && (m<=59) && ((thermState.hours != h) || (thermState.minutes != m)) ) {
          while( (*p != 0) && ( (*p<'0') || (*p>'9') ) ) p++;
          thermState.seconds = (int8)strtol( p, NULL, 10 );
          if( (errno != 0) || (thermState.seconds<0) || (thermState.seconds>59) ) thermState.seconds = 0;

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
#ifdef USE_HTU21D
  } else if( mqttIsTopic( topic, TOPIC_SetAutoAdjMode ) ) {
    if( (payload != NULL) && (length==1) && (thermState.sensor == 0) ) {
      int m = ( (char)(*payload) - '1' + 1 );
      if ( (errno == 0) && (m>=0) && (m<=2) ) {
        thermActivityLocked = millis();
        thermConfig.autoAdjMode = m;
        storageSave();
      }
      mqttPublish(TOPIC_SetAutoAdjMode,(char*)NULL, false);
    }
    return true;
#endif    
  } else if( mqttIsTopic( topic, "EnableOTA" ) ) {
    thermSetWiFiSign( ThermWiFiState::BlinkFast );
    thermDisabled = true;
    delay(100);
    commsEnableOTA();
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

void thermPublish( char* topic, float value, float* _value, bool retained, bool activity ) {
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
void thermPublish( char* topic, bool value, bool* _value, bool retained, bool activity ) {
  if( ((int)value != (int)(*_value) ) && mqttPublish( topic, value?1:0, retained) ) {
    *_value = value;
    thermLastPublished = millis();
    if( activity ) thermTriggerActivity();
  }
}

void thermPublish( char* topic, int value, int* _value, bool retained, bool activity ) {
  if( (value != (*_value) ) && mqttPublish( topic, value, retained) ) {
    *_value = value;
    thermLastPublished = millis();
    if( activity ) thermTriggerActivity();
  }
}

void thermPublish() {
    unsigned long t = millis();
    // To avoid MQTT spam
    if( (thermLastStatus == 0) || (unsigned long)(t - thermLastPublished) < (unsigned long)500 ) return;
    char s[128];
    char s1[128];
    thermPublish( P3(TOPIC_SetLocked), thermState.locked, &_thermState.locked, true, true );
    thermPublish( P3(TOPIC_SetPower), thermState.power, &_thermState.power, true, true );
    thermPublish( TOPIC_Heating, thermState.heating, &_thermState.heating, true, false );
    thermPublish( TOPIC_TargetSetManually, thermState.targetSetManually, &_thermState.targetSetManually, true, false );
    thermPublish( TOPIC_RoomTemp, thermState.roomTemp, &_thermState.roomTemp, true, false );
    thermPublish( P3(TOPIC_SetTargetTemp), thermState.targetTemp, &_thermState.targetTemp, true, true );
    thermPublish( TOPIC_TargetTempMax, thermState.targetTempMax, &_thermState.targetTempMax, true, false );
    thermPublish( TOPIC_TargetTempMin, thermState.targetTempMin, &_thermState.targetTempMin, true, false );
    thermPublish( TOPIC_FloorTemp, thermState.floorTemp, &_thermState.floorTemp, true, false );
    thermPublish( P3(TOPIC_SetFloorTempMax), thermState.floorTempMax, &_thermState.floorTempMax, true, false );
    thermPublish( P3(TOPIC_SetAutoMode), thermState.autoMode, &_thermState.autoMode, true, true );
    thermPublish( P3(TOPIC_SetLoopMode), thermState.loopMode, &_thermState.loopMode, true, false );
    thermPublish( P3(TOPIC_SetSensor), thermState.sensor, &_thermState.sensor, true, false );
    thermPublish( TOPIC_Hysteresis, thermState.hysteresis, &_thermState.hysteresis, true, false );
    thermPublish( P3(TOPIC_SetAdjTemp), thermState.adjTemp, &_thermState.adjTemp, true, false );

    thermPublish( P3(TOPIC_SetAntiFroze), thermState.antiFroze, &_thermState.antiFroze, true, false );
    thermPublish( TOPIC_PowerOnMemory, thermState.powerOnMemory, &_thermState.powerOnMemory, true, false );
    thermPublish( P3(TOPIC_SetWeekday), thermState.weekday, &_thermState.weekday, true, false );
    if( (thermState.hours != _thermState.hours) || (thermState.minutes != _thermState.minutes) ) {
      sprintf(s,"%02d:%02d", thermState.hours, thermState.minutes );
      if( mqttPublish( P3(TOPIC_SetTime), s, true)) {
        _thermState.hours = thermState.hours;
        _thermState.minutes = thermState.minutes;
        thermLastPublished = millis();
      }
    }
    thermPrintSchedule(s,thermState.schedule, 6);
    thermPrintSchedule(s1,_thermState.schedule, 6);
    if( (strlen(s)>0) && (strcmp(s, s1)!=0) ) {
      if( mqttPublish( P3(TOPIC_SetSchedule), s, true)) {
        memcpy(_thermState.schedule, thermState.schedule, sizeof(_thermState.schedule));
        thermLastPublished = millis();
      }
    }

    thermPrintSchedule(s,thermState.schedule2, 2);
    thermPrintSchedule(s1,_thermState.schedule2, 2);
    if( (strlen(s)>0) && (strcmp(s, s1)!=0) ) {
      if( mqttPublish( P3(TOPIC_SetSchedule2), s, true)) {
        memcpy(_thermState.schedule2, thermState.schedule2, sizeof(_thermState.schedule2));
        thermLastPublished = millis();
      }
    }

#ifdef USE_HTU21D
    static int _autoAdjMode = 99;
    if( _autoAdjMode != thermConfig.autoAdjMode ) {
      if( mqttPublish( P3(TOPIC_SetAutoAdjMode), thermConfig.autoAdjMode, true)) {
        _autoAdjMode = thermConfig.autoAdjMode;
        thermLastPublished = millis();
      }
    }
#endif

}
#pragma endregion

#pragma region Init & Loop
void thermLoop() {
  if(thermDisabled) return;
  
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
    //*** Thermostat time validation
    tm* lt = commsGetTime();
    
    if( mqttConnected() && (lt!=NULL) ) {
      int weekday = (lt->tm_wday>0) ? lt->tm_wday : 7;
      
      unsigned long tc =  ((( weekday * 24 ) + lt->tm_hour) * 60 + lt->tm_min) * 60 + lt->tm_sec;
      unsigned long tt = ((( thermState.weekday * 24 ) + thermState.hours) * 60 + thermState.minutes) * 60 + thermState.seconds;
      // If more than 20 seconds difference:
      if( ((tc<tt)?(tt-tc):(tc-tt)) > 20 ) {
          thermActivityLocked = millis();
          thermState.hours = lt->tm_hour;
          thermState.minutes = lt->tm_min;
          thermState.seconds = lt->tm_sec;
          thermState.weekday = weekday;
          char s[31];
          sprintf(s, "01100008000204%02x%02x%02x%02x", thermState.hours, thermState.minutes, thermState.seconds, thermState.weekday );
          thermSendMessage( s );
      }
    }

    // Get MCU status every few seconds
    if( (unsigned long)(t - thermLastStatusRequest) > (unsigned long)4000 ) {
      thermSendMessage( "010300000016");
      thermLastStatusRequest = t;
    } else {
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
    }
    lastMaintenance = t;
  } else {
    thermPublish();
  }
}

void thermInit() {
  storageRegisterBlock('T', &thermConfig, sizeof(thermConfig));
  thermActivityLocked = millis();
  therm.begin(9600);
  mqttRegisterCallbacks( thermCallback, thermConnect );
  registerLoop(thermLoop);
}

#pragma endregion
