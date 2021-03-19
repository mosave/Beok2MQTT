#ifndef storage_h
#define storage_h
#include "Config.h"

// Initialize library
void storageInit();

// Initialize library and crear storage block
void storageInit( bool reset );

// Initialize library and crear storage block
void storageReset();

// Register new memory block with storage library
void storageRegisterBlock( char id, void* data, unsigned short size );

// Force storage to save changes immediately (if any)
void storageSave();

#endif
