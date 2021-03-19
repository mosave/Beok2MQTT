#include <Arduino.h>
#include "Config.h"
#include "Thermostat.h"
#include "Comms.h"

char thermData[128];
int thermLen = 0;

extern ThermState thermState;

unsigned long thermLastRead = 0;

#ifdef USE_SOFT_SERIAL
	#include <SoftwareSerial.h>
	SoftwareSerial therm(MCU_RX, MCU_TX);
#else
	#define therm Serial
#endif


void thermLogData( const char* prefix ) {
#ifdef THERM_DEBUG
  if( thermLen==0 ) return;
  char s[255];
  char hex[4];
  strcpy( s, "< ");
  if( (prefix != NULL) && (strlen(prefix)>0) ) strcat(strcat( s, prefix)," ");

  for(int i=0; i<thermLen; i++ ) {
    sprintf( hex, "%02x", thermData[i]);
    if( i == thermLen-1 ) strcat(s, ":");
    strcat( s, hex);
  }
  aePrintln(s);
  mqttPublish("Log",s,false);
#endif
}


#pragma region Message Sending
void thermSendMessage( const char* data) {
  char hex[3] = {0,0,0};
  char* p = (char*)data;
  uint8_t sum = 0;
  while ( *p>'\0' ) {
    hex[0] = *p; p++;
    hex[1] = *p; p++;
    uint8_t d = strtoul( hex, NULL, 16);
    therm.write(d);
    sum += d;
  }
  therm.write(sum);

#ifdef THERM_DEBUG
  char s[256];
  sprintf(s, "> %s:%02x", data,sum);
  mqttPublish("Log",s,false);
  aePrintln(s);
#endif
}

void thermSendMessage( const __FlashStringHelper* data) {
  PGM_P p = reinterpret_cast<PGM_P>(data);
  char s[255] __attribute__ ((aligned(4)));
  size_t n = std::min(sizeof(s)-1, strlen_P(p) );
  memcpy_P(s, p, n);
  s[n] = 0;
  thermSendMessage( s );
}
#pragma endregion

#pragma region Curtains commands
bool thermIsAlive() {
  return true;
  //return ( (unsigned long)(millis() - thermLastAlive) < (unsigned long)90000 );
}

#pragma endregion
void thermProcessData() {
}

void thermLoop() {
	unsigned long t = millis();

  // Read from the Bluetooth module and send to the Arduino Serial Monitor
  while (therm.available()>0) {
    // Packet read timeout
    if( (unsigned long)(t - thermLastRead) > (unsigned long)500 ) {
      if( thermLen>1 ) thermLogData("Timed Out");
      thermLen = 0;
    }
    // Packet is too long
    if( thermLen >= sizeof(thermData)-1 ) {
      thermLogData("Too long");
      thermLen=0;
    }

    thermLastRead = t;
    int d = therm.read();
    //thermData[thermLen++] = therm.read(); 
    thermData[thermLen++] = d; 
    // Invalid packet header (0x0103)
    if( (thermData[0]!=0x01) || (thermLen>1) && (thermData[1]!=0x03) ) {
      thermLogData("Invalid header");
      thermLen = 0;
    } else {
      // Check if packet received correctly.
      // 1. Packet length is received already:
      if( thermLen > 6) {
          // if( sum == thermData[ 6 + len ]) {
          //   // 5. Process buffer...
          //   thermLastAlive = t;
          //   thermHeartBeat = t;
          //   thermLogData("");
          //   thermProcessData();
          // } else {
          //   thermLogData("Bad CRC");
          // }
          //thermLen = 0;
      }
    }
  }

  if( (unsigned long)(t - thermLastRead) > (unsigned long)1000 ) {
    (unsigned long)(t + (unsigned long)300);
    if( (unsigned long)(t - thermLastRead) > (unsigned long)15000 ) {
      // Send heartbeat
      thermLastRead = t;
    } else {
      thermLastRead = t;
    }
  }

}

void thermInit() {
	therm.begin(9600);
	registerLoop(thermLoop);
}
