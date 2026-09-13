#ifndef aelib_h
#define aelib_h
#include <Arduino.h>
#include "Config.h"

#define AELIB_VERSION "2.1.1"

#pragma GCC diagnostic ignored "-Wwrite-strings"

#ifndef aePrintf
    #define aePrintf( ... ) Serial.printf( __VA_ARGS__ )
#endif
#ifndef aePrint
    #define aePrint( ... ) Serial.print( __VA_ARGS__ )
#endif

#ifndef aePrintln
    #define aePrintln( ... ) Serial.println( __VA_ARGS__ )
#endif


#define timedOut( tNow, t, timeout ) ((unsigned long)((unsigned long)(tNow) - (unsigned long)(t)) > (unsigned long)(timeout))


typedef void (*LOOP)();


// Initialize library
void aeInit();


void aeRegisterLoop( LOOP loop );
void aeLoop();


// Clear storage blocks
void storageReset();

// Register new memory block with storage library
void storageRegisterBlock(char id, void* data, unsigned short size);

// Force storage to save changes immediately (if any)
void storageSave();

#endif
