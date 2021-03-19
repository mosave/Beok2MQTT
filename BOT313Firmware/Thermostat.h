#ifndef thermostat_h
#define thermostat_h

struct ThermState {
    bool isOn;
    bool autoMode;
    char sensor;
    float targetTemp;
    float roomTemp;
    float externTemp;
    int hours;
    int minutes;
    int dayofweek;
};

extern ThermState thermState;

//MCU_DEBUG only!!!
void thermSendMessage( const char* data);

bool thermIsAlive();

void thermInit();
#endif
