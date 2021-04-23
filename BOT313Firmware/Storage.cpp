#include <Arduino.h>
#include <EEPROM.h>
#include "Config.h"
#include "Storage.h"

//#define Debug

#define STORAGE_MaxBlocks 8
#define STORAGE_SaveDelay ((unsigned long)60*60*1000)
#define STORAGE_Size 4096

unsigned int storageBlockCount;
char storageIds[STORAGE_MaxBlocks];
unsigned short storageSizes[STORAGE_MaxBlocks];
void* storageBlocks[STORAGE_MaxBlocks];


struct StorageSnapshotHeader {
  char id;
  unsigned short size;
} __attribute__ ((packed));

byte storageSnapshot[STORAGE_Size];

unsigned long changedOn = 0;

// Search block of data in snapshot by blockId
// Returns byte index in storageSnapshot array or -1 if not found
void* storageSnapshotFind(char id ) {
  if( (storageSnapshot[0] == 0x41) && (storageSnapshot[1] == 0x45) ) {
    void* p = storageSnapshot + 2;
    while( p < (storageSnapshot + STORAGE_Size) ) {
      StorageSnapshotHeader* header = (StorageSnapshotHeader*)p;
      p += sizeof(StorageSnapshotHeader);
      
      if( header->id == 0 ) return NULL;
      if( header->id == id ) return p;
      
      p += header->size;
    }
  }
  return NULL;
}

// Pack Storage blocks into storageSnapshot array to write to EMMC;
void storageMakeSnapshot(){
  memset( storageSnapshot, 0, STORAGE_Size );
  storageSnapshot[0] = 0x41; storageSnapshot[1] = 0x45;

  void* p = &storageSnapshot[2];
  for( int i = 0; (i<storageBlockCount) && (p<(storageSnapshot+STORAGE_Size)); i++) {
    StorageSnapshotHeader* header = (StorageSnapshotHeader*)p;
    p+=sizeof(StorageSnapshotHeader);
    header->id = storageIds[i];
    header->size = storageSizes[i];
    // Should implement range checking here
    //if( (p+header->size) >= STORAGE_Size ) header->size = STORAGE_Size - p - 1;
    
    memcpy( p, storageBlocks[i], header->size );
    p += header->size;
  }
}

bool isChanged() {
  bool changed = false;
  for( int i = 0; (i<storageBlockCount); i++) {
    void* p = storageSnapshotFind( storageIds[i] );
    if( (p != NULL) && (memcmp( storageBlocks[i], p, storageSizes[i] ) != 0 ) ) changed = true;
  }
  if( changed ) {
#ifdef Debug
  aePrintln(F("Storage: changes detected"));
#endif
    storageMakeSnapshot();
    changedOn = millis();
  }
  return (changedOn>0);
}

// Read storage from non-volatile memory
void storageRead() {
  
  EEPROM.get( 0, storageSnapshot);
  if( (storageSnapshot[0] != 0x41) || (storageSnapshot[1] != 0x45) ) {
    memset( storageSnapshot, 0, STORAGE_Size );
    storageSnapshot[0] = 0x41;
    storageSnapshot[1] = 0x45;
    changedOn = millis();
  }
    
  for( int i = 0; (i<storageBlockCount); i++) {
    void* p = storageSnapshotFind( storageIds[i] );
    if( p != NULL ) {
      memcpy( storageBlocks[i], p, storageSizes[i] );
    } else {
      memset( storageBlocks[i], 0, storageSizes[i] );
    }
  }
}

// Register new memory block with storage library
void storageRegisterBlock(char id, void* data, unsigned short size ) {
  storageInit();
  storageIds[storageBlockCount] = id;
  storageBlocks[storageBlockCount] = data;
  storageSizes[storageBlockCount] = size;
  storageBlockCount++;
  void* p = storageSnapshotFind(id);
  
  if( p != NULL ) {
    memcpy( data, p, size );
  } else {
    changedOn = millis();
    storageMakeSnapshot();
  }
}

void storageSave() {
  storageInit();
  if( isChanged() ) {
    aePrintln(F("Writing Storage"));
    EEPROM.put( 0, storageSnapshot);
    EEPROM.commit();
    changedOn = 0;
  }
}

// Every minute checks if storage blocks changed.
// If more then STORAGE_SaveDelay passed since last change then save storage to EMMC
void storageLoop() {
  static unsigned long checkedOn = 0;
  unsigned long t = millis();
  if( ((unsigned long)(t - checkedOn)) > ((unsigned long)60000) ) {
    checkedOn = t;
    if( isChanged() ) {
      if( ((unsigned long)(t - changedOn)) > STORAGE_SaveDelay ) {
        storageSave();
      }
    }
  }
}

// Initialize library and crear storage block
void storageInit( bool reset ) {
  static bool initialized = false;
  if( !initialized ) {
    EEPROM.begin( STORAGE_Size );
    registerLoop(storageLoop);
  }

  if( reset ) {
    memset( storageSnapshot, 0, sizeof(storageSnapshot) );
    EEPROM.put( 0, storageSnapshot);
    EEPROM.commit();
    changedOn = 0;
  }
  
  if( !initialized ) {
    initialized = true;
    storageRead();
  }
}

void storageInit() {
  storageInit( false );
}

void storageReset() {
  aePrintln(F("Clearing Storage"));
  storageInit();
  memset( storageSnapshot, 0, sizeof(storageSnapshot) );
  EEPROM.put( 0, storageSnapshot);
  EEPROM.commit();
  delay(1000);
  ESP.restart();
}
