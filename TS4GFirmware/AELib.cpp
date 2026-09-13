#include <Arduino.h>
#include <EEPROM.h>
#include "AELib.h"

//#define Debug

#define AELIB_MaxLoops 16

#ifndef STORAGE_Version
#define STORAGE_Version 0x01
#endif

#define STORAGE_MaxBlocks 8
#define STORAGE_SaveDelay ((unsigned long)60*60*1000)
#define STORAGE_MaxSaveDelay ((unsigned long)6*60*60*1000)
#define STORAGE_Size 4096


unsigned int storageBlockCount;
char storageIds[STORAGE_MaxBlocks];
unsigned short storageSizes[STORAGE_MaxBlocks];
void* storageBlocks[STORAGE_MaxBlocks];

unsigned int aelibLoopCount = 0;
LOOP aelibLoops[AELIB_MaxLoops];


struct StorageSnapshotHeader {
    char id;
    unsigned short size;
} __attribute__((packed));

byte storageSnapshot[STORAGE_Size];

unsigned long changedOn = 0;
// The oldest unsaved modification. Unlike changedOn, it is not reset by
// continuing changes, so a steady stream cannot postpone a save forever.
unsigned long storageDirtySince = 0;

#pragma region Storage functions
// Search block of data in snapshot by blockId
// Returns byte index in storageSnapshot array or -1 if not found
void* storageFindBlock(char id) {
    if ((storageSnapshot[0] == 0x41) && (storageSnapshot[1] == STORAGE_Version)) {
        byte* p = storageSnapshot + 2;
        while (p < (storageSnapshot + STORAGE_Size)) {
            StorageSnapshotHeader* header = (StorageSnapshotHeader*)p;
            p += sizeof(StorageSnapshotHeader);

            if (header->id == 0) return NULL;
            if (header->id == id) return p;

            p += header->size;
        }
    }
    return NULL;
}

// Pack Storage blocks into storageSnapshot array to write to EMMC;
void storageMakeSnapshot() {
    memset(storageSnapshot, 0, STORAGE_Size);
    storageSnapshot[0] = 0x41; storageSnapshot[1] = STORAGE_Version;

    byte* p = &storageSnapshot[2];
    for (int i = 0; (i < storageBlockCount) && (p < (storageSnapshot + STORAGE_Size)); i++) {
        StorageSnapshotHeader* header = (StorageSnapshotHeader*)p;
        p += sizeof(StorageSnapshotHeader);
        header->id = storageIds[i];
        header->size = storageSizes[i];
        // Should implement range checking here
        //if( (p+header->size) >= STORAGE_Size ) header->size = STORAGE_Size - p - 1;

        memcpy(p, storageBlocks[i], header->size);
        p += header->size;
    }
}

bool storageIsModified() {
    bool changed = false;
    for (int i = 0; (i < storageBlockCount); i++) {
        void* p = storageFindBlock(storageIds[i]);
        if ((p != NULL) && (memcmp(storageBlocks[i], p, storageSizes[i]) != 0)) changed = true;
    }
    if (changed) {
#ifdef Debug
        aePrintln(F("Storage: changes detected"));
#endif
        storageMakeSnapshot();
        changedOn = millis();
        if (storageDirtySince == 0) {
            storageDirtySince = changedOn;
        }
    }
    return (changedOn > 0);
}

// Read storage from non-volatile memory
void storageRead() {

    EEPROM.get(0, storageSnapshot);
    if ((storageSnapshot[0] != 0x41) || (storageSnapshot[1] != STORAGE_Version)) {
        memset(storageSnapshot, 0, STORAGE_Size);
        storageSnapshot[0] = 0x41;
        storageSnapshot[1] = STORAGE_Version;
        changedOn = millis();
    }

    for (int i = 0; (i < storageBlockCount); i++) {
        void* p = storageFindBlock(storageIds[i]);
        if (p != NULL) {
            memcpy(storageBlocks[i], p, storageSizes[i]);
        } else {
            memset(storageBlocks[i], 0, storageSizes[i]);
        }
    }
}

void storageInit(bool reset) {
    static bool initialized = false;
    if (!initialized) {
        EEPROM.begin(STORAGE_Size);
    }

    if (reset) {
        memset(storageSnapshot, 0, sizeof(storageSnapshot));
        EEPROM.put(0, storageSnapshot);
        EEPROM.commit();
        changedOn = 0;
        storageDirtySince = 0;
    }

    if (!initialized) {
        initialized = true;
        storageRead();
    }
}

// Register new memory block with storage library
void storageRegisterBlock(char id, void* data, unsigned short size) {
    storageInit(false);
    storageIds[storageBlockCount] = id;
    storageBlocks[storageBlockCount] = data;
    storageSizes[storageBlockCount] = size;
    storageBlockCount++;
    void* p = storageFindBlock(id);

    if (p != NULL) {
        memcpy(data, p, size);
    } else {
        changedOn = millis();
        storageMakeSnapshot();
    }
}

void storageSave() {
    storageInit(false);
    if (storageIsModified()) {
        aePrintln(F("Writing Storage"));
        EEPROM.put(0, storageSnapshot);
        EEPROM.commit();
        changedOn = 0;
        storageDirtySince = 0;
    }
}

void storageReset() {
    aePrintln(F("Clearing Storage"));
    storageInit(true);
    delay(1000);
    ESP.restart();
}
#pragma endregion

#pragma region Loop callback support
void aeRegisterLoop(LOOP loop) {
    if (aelibLoopCount < AELIB_MaxLoops) {
        aelibLoops[aelibLoopCount] = loop;
        aelibLoopCount++;
    }
}

void aeInit() {
    storageInit(false);
}
#pragma endregion

void aeLoop() {
    // Check if storage blocks changed
    static unsigned long checkedOn = 0;
    unsigned long t = millis();
    if (timedOut(t, checkedOn, 60000)) {
        checkedOn = t;
        if (storageIsModified()) {
            // Save after a quiet period, or cap the dirty interval during steady use.
            if (timedOut(t, changedOn, STORAGE_SaveDelay)
                || timedOut(t, storageDirtySince, STORAGE_MaxSaveDelay)) {
                storageSave();
            }
        }
        yield();
    }

    for (int i = 0; i < aelibLoopCount; i++) {
        if (aelibLoops[i] != NULL) {
            aelibLoops[i]();
            yield();
        }
    }
}
