#ifndef thermostat_h
#define thermostat_h

struct ThermState {
    bool locked;
    bool on;
    bool heating;
    bool targetSetManually;

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

void thermInit();
#endif
