#ifndef tah_h
#define tah_h

extern float tahTemperatureAdj;
extern float tahHumidityAdj;

bool tahAvailable();
void tahInit();

float tahGetTemperature();
float tahGetHumidity();
float tahGetHeatIndex();
float tahGetAbsHumidity();
int tahGetPressure();
int tahGetCO2();
int tahGetVOC();

#ifndef TAH_DHT
#ifndef TAH_HTU21D
#ifndef TAH_SCD4x
#ifndef TAH_BME68x
   Please define TAH_DHT, TAH_HTU21D or TAH_SCD4x should be defined in Config.h
#endif
#endif
#endif
#endif

#endif
