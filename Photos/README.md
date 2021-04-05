# Фотографии

### Внешний вид термостатов
![Термостаты, подсветка выключена](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p01%20Thermostats.jpg)
![Термостаты, подсветка включена](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p02%20Backlight.jpg)

### Плата контроллера BOT-313 WiFi со стороны экрана
![Плата термостата, экран](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p03%20Control%20Board%20Face.jpg)

### Модуль Broadlink BL335-P
![Broadlink BL335-P](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p04%20Broadlink%20BL335-P.jpg)

### Плата контроллера BOT-313 WiFi (WH) с подключенным UART сканером
![BOT-313 WiFi (WH) с UART сканером](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p05%20Control%20Board.jpg)

### Плата контроллера TGP-51 WiFi (WH)
*Термостаты для бойлера поддерживают только один (встроенный) датчик температуры. Провода и дополнительный разъем на фотографии - вход внешнего датчика температуры, выбираемого переключателем*
![TGP-51 WiFi (WH)](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p05%20Control%20Board%202.jpg)

### Тестовая конфигурация: WeMos D1, для управления термостатом задействаован SoftSerial
![Тестовая конфигурация](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p06%20Test%20setup.jpg)

### Модуль Broadlink заменен на ESP-01
![Модуль Broadlink заменен на ESP-01](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p07%20ESP-01%20installed.jpg)

### Модуль ESP-01S с дополнительным цифровым датчиком влажности GY-213/HTDU21D
GY 213 пока путешествует где-то в недрах российской почты. Фотография будет добавлена позднее.
![Датчик влажности](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p08%20ESP+GY213V.jpg)

### Дерево MQTT топиков термостата
![MQTT Tree](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p09%20MQTT%20Tree.jpg)


### "Принципиальная схема" :sunglasses:

 * VCC <-> VCC
 * GN D<-> GND
 * RX <-> TX
 * TX <-> RX
 
 При подключении к ESP дополнительного i2c сенсора выходы GPIO0 и GPIO2 можно использовать как SDA и SCL:
 * GPIO2 <-> SDA
 * GPIO0 <-> SCL

![Wiring](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p10%20Wiring.jpg)


### Что такое хаос
![Chaos](https://github.com/mosave/Beok2MQTT/raw/main/Photos/p11%20Workplace%20Chaos.jpg)
