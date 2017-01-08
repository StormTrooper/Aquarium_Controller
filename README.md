Arduino/NodeMCU based Aquarium Controller.

Temperature and pH values are read and transmitted to Domoticz every 5 mins.
The controller also sets the RGB LED lighting depending on the time of day/night.
T5 lights and CO2 controlled via time and switched on/off via relays.

Greg McCarthy

Hardware:
NodeMCU

Libaries Used:

DS1307 - RTC
https://github.com/Makuna/Rtc

DS18B20 Temp Sensor
http://www.hobbytronics.co.uk/ds18b20-arduino

OLED - connected in I2C
http://electronics.stackexchange.com/questions/164680/ssd1306-display-spi-connection-or-i2c-according-to-resistors/269790#269790?newreg=4f0894a69fdf4c3eb3e6e61d90f3e744
https://github.com/olikraus/u8b8/wiki/u8x8reference


PH Meter - Amplifier
http://www.electro-tech-online.com/threads/ph-amplifier-for-micro.41430/

IRF540 N-Channel MOSFETS
http://www.infineon.com/dgdl/irf540n.pdf?fileId=5546d462533600a4015355e396cb199f

5050 RGB LED String
http://www.ebay.co.uk/itm/322248491740?_trksid=p2057872.m2749.l2649&var=511148771996&ssPageName=STRK%3AMEBIDX%3AIT


