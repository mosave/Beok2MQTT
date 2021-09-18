#ifndef thermostat_h
#define thermostat_h

struct ThermConfig {
    int autoAdjMode = 0;
};

extern ThermConfig thermConfig;

struct ThermScheduleRecord {
    int8 h;
    int8 m;
    float t;
};

struct ThermState {
    bool locked;
    bool power;
    bool heating;
    bool targetSetManually;

    float roomTemp;
    float targetTemp;
    float targetTempMax;
    float targetTempMin;

    float floorTemp;
    int floorTempMax;

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
    int weekday;

    ThermScheduleRecord schedule[6];
    ThermScheduleRecord schedule2[2];
};

extern ThermState thermState;

//MCU_DEBUG only!!!
void thermSendMessage( const char* data);

void thermInit();
#endif
