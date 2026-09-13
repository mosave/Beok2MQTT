# MQTT controller for Beok thermostats

### Замена Wi-Fi-модуля Broadlink или Tuya в термостате Beok на ESP8266

Wi-Fi-термостаты Beok вполне заслуженно ценятся «умнодомостроителями»: это функциональные, отработанные и надёжные устройства с удобным интерфейсом, несколькими вариантами дизайна и гибкой работой по расписанию.

Штатное приложение может управлять термостатом как в локальной сети, так и через проприетарные облачные серверы. Однако к самому приложению [Beok Home](https://play.google.com/store/apps/details?id=com.beok.heat) есть претензии. Рано или поздно также возникает задача интеграции термостата с MajorDoMo, Home Assistant или другой локальной системой управления.

Для термостатов с Broadlink эту задачу можно решить с помощью [модуля Broadlink для MajorDoMo](https://connect.smartliving.ru/addons/category1/32.html) и [SBeokThermostat](https://connect.smartliving.ru/addons/category6/245.html). Но такие интеграции периодически опрашивают все зарегистрированные устройства, создают дополнительную нагрузку на сервер и задерживают сценарии, особенно когда часть термостатов находится офлайн.

Эта прошивка предоставляет прямой доступ к термостату по MQTT. Благодаря этому можно отказаться от отдельного модуля интеграции, разгрузить сервер умного дома и строить сценарии непосредственно на уровне MQTT. Дополнительный бонус — возможность подключить к ESP8266 датчики температуры, влажности, CO₂ и качества воздуха.

Проект содержит две независимые прошивки. Они используют разные протоколы обмена с контроллером термостата и не взаимозаменяемы:

| Прошивка | Назначение |
| --- | --- |
| [`BOT313Firmware`](BOT313Firmware/BOT313Firmware.ino) | Beok BOT-313, TGP-5X и совместимые термостаты с модулем Broadlink |
| [`TS4GFirmware`](TS4GFirmware/TS4GFirmware.ino) | Термостаты, использующие UART-протокол Tuya/TS4G |

### Использованное железо

- Совместимый настенный термостат Beok; выбор прошивки определяется его протоколом, а не только названием бренда:
  - [Beok BOT-313 WiFi](http://www.beok-controls.com/pro_view.asp?id=66) ([проверено](https://aliexpress.ru/item/4000202232813.html));
  - [Beok TGP-5X WiFi](http://www.beok-controls.com/pro_view.asp?id=82) ([проверено](https://aliexpress.ru/item/4000695450163.html));
  - Многие другие модели, работающие по протоколу BroadLink либо Tuya.
- ESP-01S либо другой модуль ESP8266.
- Необязательный I²C-датчик: [GY-213V с HTU21D](https://www.aliexpress.com/item/32482508209.html) для TS4G; HTU21D или SCD4x для BOT-313. В BOT-313 также есть реализации для DHT и BME68x.

### Прошивка Wi-Fi-контроллера

В каталогах [`BOT313Firmware`](BOT313Firmware) и [`TS4GFirmware`](TS4GFirmware) находятся исходники двух рабочих прошивок, которые продолжают развиваться. В основе коммуникационной части лежит [IoT Framework AELib](https://github.com/mosave/AELib), поэтому доступны общие команды и сервисные топики фреймворка.

### Настройка и сборка
Все публичные параметры задаются в `Config.h` выбранной прошивки: тип датчика, режимы термостата, часовой пояс, пины и отладка. Задайте там `WIFI_SSID`, `WIFI_Password`, а также при необходимости `MQTT_Address`, `MQTT_Port`, `MQTT_User` и `MQTT_Password`. Если эти макросы не определены, `Config.h` необязательно подключает локальный приватный `Config.AE.h`; этот файл намеренно не входит в репозиторий и удобен для хранения секретов вне контроля версий.

Установите Arduino core для ESP8266, библиотеку [PubSubClient](https://github.com/knolleary/pubsubclient) и библиотеку только для выбранного датчика: [SparkFun HTU21D](https://github.com/sparkfun/SparkFun_HTU21D_Breakout_Arduino_Library) либо Sensirion SCD4x. Для DHT/BME68x используйте библиотеку, указанную в соответствующем `TAH_*.cpp`.

В `BOT313Firmware/Config.h` по умолчанию включён `MANUAL_MODE_ONLY`: автоматический режим, расписания и связанные MQTT-топики отключены. Уберите этот `#define`, чтобы включить их. В `TS4GFirmware` автоматический режим и расписания не реализованы.

### Список MQTT-топиков и соответствующих им команд для взаимодействия с термостатом

Все имена ниже относительны к корню устройства. По умолчанию он имеет вид `new/%s/`, где `%s` заменяется именем устройства; его можно изменить топиком `SetRoot`. Например, после настройки корня `Bedroom/Thermostat/` топик `RoomTemp` будет `Bedroom/Thermostat/RoomTemp`.

Значения состояния публикуются retained. Командные топики принимают текстовые значения; для булевых значений подходят `0`/`1`, `off`/`on`, `false`/`true`.

#### Термостат

| Состояние | Команда | Значение | Применимость к прошивке    |
| --- | --- | --- | --- |
| `Power` | `SetPower` | 0/1 — питание | обе |
| `Locked` | `SetLocked` | 0/1 — блокировка кнопок | обе |
| `Heating` | — | 0/1 — состояние реле | обе |
| `RoomTemp` | — | температура воздуха, °C | обе |
| `FloorTemp` | — | температура внешнего датчика пола, °C | обе, при наличии |
| `FloorTempMax` | `SetFloorTempMax` | ограничение температуры пола, °C | обе, при наличии |
| `TargetTemp` | `SetTargetTemp` | целевая температура, °C | обе |
| `TargetTempMax` | `SetTargetTempMax` | верхняя граница целевой температуры | состояние: обе; команда: только TS4G |
| `TargetTempMin` | — | нижняя граница целевой температуры | только BOT-313 |
| `Sensor` | `SetSensor` | конфигурация датчиков термостата | обе |
| `Hysteresis` | `SetHysteresis` | гистерезис | состояние: обе; команда: только TS4G |
| `AdjTemp` | `SetAdjTemp` | поправка температуры | обе |
| `AntiFroze` | `SetAntiFroze` | 0/1 — защита от замерзания | обе |
| `PowerOnMemory` | `SetPowerOnMemory` | 0/1 — восстановление состояния после питания | только BOT-313 |
| `AutoMode` | `SetAutoMode` | 0 — ручной, 1 — автоматический режим | только BOT-313, если отключён `MANUAL_MODE_ONLY` |
| `LoopMode` | `SetLoopMode` | 0 — 5+2, 1 — 6+1, 2 — 7 дней | только BOT-313, если включён автоматический режим |
| `Schedule`, `Schedule2` | `SetSchedule`, `SetSchedule2` | первое (5 интервалов) и второе (2 интервала) расписания | только BOT-313, если включён автоматический режим |
| `TargetSetManually` | — | 0/1 — уставка в авто-режиме изменена кнопками | только BOT-313, если включён автоматический режим |
| `Brightness` | `SetBrightness` | яркость дисплея | только TS4G |
| `Inverted` | `SetInverted` | 0/1 — инверсия дисплея | только TS4G |
| `Sound` | `SetSound` | 0/1 — звук кнопок | только TS4G |

#### Home Assistant и топики, доступные при подключении дополнительного датчика температуры

| Состояние | Команда | Значение | Применимость |
| --- | --- | --- | --- |
| `HAction` | — | `off`, `idle`, `heating` | обе |
| `HAMode` | `SetHAMode` | `off`, `heat`; также `auto` для BOT-313 с автоматическим режимом | обе; `auto` — только BOT-313 |
| `AutoAdjMode` | `SetAutoAdjMode` | 0 — ручная поправка, 1 — по `Sensors/Temperature`, 2 — по `Sensors/HeatIndex` | обе, при включённом дополнительном датчике |
| `Sensors/AdjTempOn` | `Sensors/SetAdjTempOn` | поправка датчика при включённом нагреве | обе, при включённом дополнительном датчике |
| `Sensors/AdjTempOff` | `Sensors/SetAdjTempOff` | поправка датчика при выключенном нагреве | обе, при включённом дополнительном датчике |
| `Sensors/TAHValid` | — | 0/1 — датчик доступен | обе, при включённом дополнительном датчике |
| `Sensors/Temperature` | — | температура дополнительного датчика, °C | обе, при включённом дополнительном датчике |
| `Sensors/Humidity` | — | влажность, % | обе, при включённом дополнительном датчике |
| `Sensors/HeatIndex` | — | индекс жары, °C | обе, при включённом дополнительном датчике |
| `Sensors/AbsHumidity` | — | абсолютная влажность | обе, при включённом дополнительном датчике |
| `Sensors/CO2` | — | CO₂, ppm | только BOT-313 с SCD4x |
| `Sensors/Pressure` | — | давление | только BOT-313 с BME68x |
| `Sensors/IAQ` | — | индекс качества воздуха | только BOT-313 с BME68x |

`Online`, `DeviceInfo`, `Activity`, `Reset`, `FactoryReset`, `EnableOTA`, `SetName`, `SetRoot` и другие общие топики не дублируются в этой таблице. Их описание и поведение определяет IoT-фреймворк [AELib](https://github.com/mosave/AELib); для выбранной прошивки смотрите также `AELib.*`, `Comms.*` и `TAH.*` в каталогах [`BOT313Firmware`](BOT313Firmware) и [`TS4GFirmware`](TS4GFirmware).

### Пример описания термостата в файле конфигурации Home Assistant

Добавьте MQTT-интеграцию в Home Assistant, затем укажите MQTT Climate в `configuration.yaml` в актуальной структуре. Подставьте собственный корень топиков вместо `Bedroom/Thermostat`.

```yaml
mqtt:
  - climate:
      unique_id: bedroom_thermostat
      name: Bedroom thermostat
      availability:
        - topic: Bedroom/Thermostat/Online
          payload_available: "1"
          payload_not_available: "0"
      temperature_unit: C
      temp_step: 0.5
      max_temp: 25
      min_temp: 15
      precision: 0.5
      modes:
        - "off"
        - "heat"
        - "auto" # Уберите для TS4G и BOT-313 с MANUAL_MODE_ONLY
      current_temperature_topic: Bedroom/Thermostat/RoomTemp
      temperature_state_topic: Bedroom/Thermostat/TargetTemp
      temperature_command_topic: Bedroom/Thermostat/SetTargetTemp
      action_topic: Bedroom/Thermostat/HAction
      mode_state_topic: Bedroom/Thermostat/HAMode
      mode_command_topic: Bedroom/Thermostat/SetHAMode
```

Параметры MQTT Climate описаны в [документации Home Assistant](https://www.home-assistant.io/integrations/climate.mqtt/). Прошивка не публикует MQTT Discovery-конфигурацию, поэтому YAML или ручное добавление MQTT-устройства остаются необходимыми.

### Дополнительные материалы

- [Фотографии](https://github.com/mosave/Beok2MQTT/tree/main/Photos)
- [Описание штатного Broadlink-модуля](https://github.com/mosave/Beok2MQTT/tree/main/Docs)
