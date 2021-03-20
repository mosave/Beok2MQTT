#ifndef thermostat_h
#define thermostat_h

struct ThermState {
    bool isLocked;
    bool isOn;
    bool isActive;
    bool isManual;

    float roomTemp;
    float targetTemp;
    float targetTempMax;
    float targetTempMin;

    float floorTemp;
    float floorTempMax;

    bool autoMode;
    int loopMode;
    int sensor;
    float hysteresis;
    float adjTemp;

    bool antiFroze;
    bool powerOnMemory;

    int hours;
    int minutes;
    int seconds;
    int dayOfWeek;
};

extern ThermState thermState;

//MCU_DEBUG only!!!
void thermSendMessage( const char* data);

bool thermIsAlive();

void thermInit();
#endif
