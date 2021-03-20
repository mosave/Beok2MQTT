#include <Arduino.h>
#include "Config.h"
#include "Thermostat.h"
#include "Comms.h"

static char* TOPIC_Locked PROGMEM = "Locked";
static char* TOPIC_On PROGMEM = "On";
static char* TOPIC_Active PROGMEM = "Active";
static char* TOPIC_Manual PROGMEM = "Manual";
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
static char* TOPIC_Hours PROGMEM = "Hours";
static char* TOPIC_Minutes PROGMEM = "Minutes";
static char* TOPIC_DayOfWeek PROGMEM = "DayOfWeek";

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

#pragma region Curtains commands
bool thermIsAlive() {
  return true;
  //return ( (unsigned long)(millis() - thermLastAlive) < (unsigned long)90000 );
}

#pragma endregion
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
        thermState.isLocked = thermData[3] & 1;
        thermState.isOn = thermData[4] & 1;
        thermState.isActive =  (thermData[4] >> 4) & 1;
        thermState.isManual =  (thermData[4] >> 6) & 1;

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

void thermConnect() {
  memset( &_thermState, 0xFF, sizeof(_thermState) );
  mqttSubscribeTopic( TOPIC_Locked );
  mqttSubscribeTopic( TOPIC_On );
  mqttSubscribeTopic( TOPIC_Active );
  mqttSubscribeTopic( TOPIC_Manual );
  mqttSubscribeTopic( TOPIC_TargetTemp );
  mqttSubscribeTopic( TOPIC_AutoMode );
  mqttSubscribeTopic( TOPIC_Sensor );
  mqttSubscribeTopic( TOPIC_Hours );
  mqttSubscribeTopic( TOPIC_Minutes );
  mqttSubscribeTopic( TOPIC_DayOfWeek );
}

bool thermCallback(char* topic, byte* payload, unsigned int length) {
  char s[64];
  if( mqttIsTopic( topic, TOPIC_Locked ) ) {
    return true;
  } else if( mqttIsTopic( topic, TOPIC_On ) ) {
    if( (payload != NULL) && (length == 1) ) {
      bool onOff = (*payload == '1') || (*payload == 1);
      sprintf(s, "0106000000 %2x", onOff ? 0 : 1 );
      thermSendMessage( ThermPacketType::Ignore, s );
    }
    return true;
  } else if( mqttIsTopic( topic, TOPIC_Active ) ) {
    return true;
  } else if( mqttIsTopic( topic, TOPIC_Manual ) ) {
    return true;
  } else if( mqttIsTopic( topic, TOPIC_TargetTemp ) ) {


/*
void thermGetSchedule(){
  thermSendMessage( "010300000016" );
}
void thermGetTemp(){
  thermSendMessage( "010300000008" );
}
void thermSetMode(){
  thermSendMessage( "" );
}
void thermSetTemp(){
  thermSendMessage( "" );
}

  public function set_power($remote_lock,$power){
    $payload = self::prepare_request(array(0x01,0x06,0x00,0x00,$remote_lock,$power));
    $response=$this->send_packet(0x6a, $payload);
  }

  public function set_mode($mode_byte,$sensor){
    $payload = self::prepare_request(array(0x01,0x06,0x00,0x02,$mode_byte,$sensor));
    $response=$this->send_packet(0x6a, $payload);
  }

  public function set_temp($param){
    $payload = self::prepare_request(array(0x01,0x06,0x00,0x01,0x00,(int)($param * 2)));
    $response=$this->send_packet(0x6a, $payload);
  }

  public function set_time($hour,$minute,$second,$day){
    $payload = self::prepare_request(array(0x01,0x10,0x00,0x08,0x00,0x02,0x04,$hour,$minute,$second,$day));
    $response=$this->send_packet(0x6a, $payload);
  }

  public function set_advanced($loop_mode,$sensor,$osv,$dif,$svh,$svl,$adj1,$adj2,$fre,$poweron){
    $payload = self::prepare_request(array(0x01,0x10,0x00,0x02,0x00,0x05,0x0a,$loop_mode,$sensor,$osv,$dif,$svh,$svl,$adj1,$adj2,$fre,$poweron));
    $response=$this->send_packet(0x6a, $payload);
  }

  public function set_schedule($param){
    $pararr = json_decode($param,true);
    $input_payload = array(0x01,0x10,0x00,0x0a,0x00,0x0c,0x18);
    for ($i = 0; $i < 6; $i++){
      $input_payload = array_push($input_payload,$pararr[0][$i]['start_hour'],$pararr[0][$i]['start_minute']);
    }
    for ($i = 0; $i < 2; $i++){
      $input_payload = array_push($input_payload,$pararr[1][$i]['start_hour'],$pararr[1][$i]['start_minute']);
    }
    for ($i = 0; $i < 6; $i++){
      $input_payload = array_push($input_payload,((int)$pararr[0][$i]['temp'] * 2));
    }
    for ($i = 0; $i < 2; $i++){
      $input_payload = array_push($input_payload,((int)$pararr[1][$i]['temp'] * 2));
    }
    $input_payload = array_merge(array(0x01,0x10,0x00,0x0a,0x00,0x0c,0x18),$input_payload);
    $payload = self::prepare_request($input_payload);
    $this->send_packet(0x6a, $payload);
  }
*/
    return true;
  } else if( mqttIsTopic( topic, TOPIC_AutoMode ) ) {
    return true;
  } else if( mqttIsTopic( topic, TOPIC_Sensor ) ) {
    return true;
  } else if( mqttIsTopic( topic, TOPIC_Hours ) ) {
    return true;
  } else if( mqttIsTopic( topic, TOPIC_Minutes ) ) {
    return true;
  } else if( mqttIsTopic( topic, TOPIC_DayOfWeek ) ) {
    return true;
  }
  return false;
}

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
    if( (unsigned long)(t - thermLastPublished) < (unsigned long)500 ) return;

    //thermPublish( TOPIC_, thermState., &_thermState., true );
    thermPublish( TOPIC_Locked, thermState.isLocked, &_thermState.isLocked, true );
    thermPublish( TOPIC_On, thermState.isOn, &_thermState.isOn, true );
    thermPublish( TOPIC_Active, thermState.isActive, &_thermState.isActive, true );
    thermPublish( TOPIC_Manual, thermState.isManual, &_thermState.isManual, true );
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
    thermPublish( TOPIC_Hours, thermState.hours, &_thermState.hours, true );
    thermPublish( TOPIC_Minutes, thermState.minutes, &_thermState.minutes, true );
    thermPublish( TOPIC_DayOfWeek, thermState.dayOfWeek, &_thermState.dayOfWeek, true );
}

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
