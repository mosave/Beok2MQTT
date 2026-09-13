#ifndef thermostat_h
#define thermostat_h

#define UNKNOWN ((char)128)

struct ThermConfig {
    int autoAdjMode = 0;
    // Компенсация нагрева корпуса термостата в выключенном и во включенном состоянии
    float sensorAdjTempOff = -1.5;
    float sensorAdjTempOn = -3.5;
};

extern ThermConfig thermConfig;

struct ThermState {
    char brightness;
    bool inverted;
    bool locked;
    bool power;
    bool heating;
    bool sound;

    float roomTemp;
    float targetTemp;
    float targetTempMax;

    float floorTemp;
    float floorTempMax;

    char sensor;
    float hysteresis;
    float adjTemp;
    bool antiFroze;
};

extern ThermState thermState;

//MCU_DEBUG only!!!
void thermSendMessage( const char* data, bool blockActivityDetection );

void thermInit();
#endif
