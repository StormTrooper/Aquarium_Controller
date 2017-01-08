/*
   Arduino/NodeMCU based Aquarium Controller.
   Temperature and pH values are read and transmitted to Domoticz every 5 mins.
   The controller also sets the RGB LED lighting depending on the time of day/night.
   T5 lights and CO2 controlled via time and switched on/off via relays.

   Greg McCarthy
   07/01/2017

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


*/


#include <pgmspace.h>
#include <Wire.h>                         // must be included here so that Arduino library object file references work
#include <RtcDS1307.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "WifiClient.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <U8x8lib.h>
#include "wifi.h"

String VERS = "Vers: 1.1";
const char http_site[] = "controller.home";
const int http_port = 8080;                           // Domoticz port
const int NTP_PACKET_SIZE = 48;                       // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE];                  // buffer to hold incoming and outgoing packets
unsigned int localPort = 2390;                        // local port to listen for UDP packets
const char* ntpServerName = "0.uk.pool.ntp.org";      // NTP Server
float temp;                                           // Temperature from sensor
unsigned long setSecond = 0;                          // stores current time value
boolean NTPSync = false;                              //Has NTP Synced with time server?
int NTP_Day, NTP_Month, NTP_Year, NTP_Hour, NTP_Minute, NTP_Second;
const unsigned long fiveMinutes = 5 * 60 * 1000UL;
static unsigned long lastSampleTime = 0 - fiveMinutes;// initialize such that a reading is due the first time through loop()
unsigned long int avgValue;                           //Store the average value of the sensor feedback


#define ONE_WIRE_BUS D0
#define RED             D6                             // pin for red LED
#define GREEN           D7                             // pin for green - never explicitly referenced
#define BLUE            D8                             // pin for blue - never explicitly referenced
#define pH_Reading      A0                             // Analog pin for reading pH value
#define T5_Lights       D5                             // Pin connected to relay to turn T5 lights on and off
#define CO2             D9

U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(/* D0 on OLED clock=*/ D4,   /* D1 on OLED data=*/ D3, /* reset=*/ U8X8_PIN_NONE);   // OLEDs without Reset of the Display

IPAddress timeServerIP;                                // time.nist.gov NTP server address

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

WiFiClient client;

RtcDS1307<TwoWire> Rtc(Wire);

//==============================================================================================================================================
// Setup
//==============================================================================================================================================
void setup ()
{
  Serial.begin(9600);

  //Setup pins
  pinMode(RED, OUTPUT);
  pinMode(GREEN, OUTPUT);
  pinMode(BLUE, OUTPUT);
  pinMode(pH_Reading, INPUT);
  pinMode(T5_Lights, OUTPUT);
  pinMode(CO2, OUTPUT);

  //Set CO2 and T5 Lights on. Will be controller by the time.
  digitalWrite(T5_Lights, HIGH);
  digitalWrite(CO2, HIGH);

  u8x8.begin();

  u8x8.setPowerSave(0);
  //u8x8.setFont(u8x8_font_victoriabold8_r);
  u8x8.setFont(u8x8_font_pxplusibmcgathin_f);
  u8x8.drawString(0, 0, "Vers: 1.0");
  delay(1000);
  u8x8.clearDisplay();

  //Connect to Wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");


  u8x8.drawString(0, 0, "WiFi connected");
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  // Get NTP Time
  getNTP();

  //--------RTC SETUP ------------
  Rtc.Begin();

  RtcDateTime dateTime(NTP_Year - 30, NTP_Month,  NTP_Day, NTP_Hour, NTP_Minute, NTP_Second);
  Rtc.SetDateTime(dateTime);
  u8x8.drawString(0, 3, "NTP Setup");

  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__); //Incase we need it

  if (!Rtc.IsDateTimeValid())
  {
    // Common Causes:
    //    1) first time you ran and the device wasn't running yet
    //    2) the battery on the device is low or even missing

    Serial.println("RTC lost confidence in the DateTime!");
    Rtc.SetDateTime(compiled);
  }

  if (!Rtc.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    Rtc.SetIsRunning(true);
  }

  // never assume the Rtc was last configured by you, so
  // just clear them to your needed state
  Rtc.SetSquareWavePin(DS1307SquareWaveOut_Low);

  sensors.begin();    //Setup DS18B20

  delay(1000);

  Serial.println("Setup complete");
  u8x8.drawString(0, 5, "Setup complete");
  delay(1000);
  u8x8.clearDisplay();

}

//==============================================================================================================================================
// Main
//==============================================================================================================================================

void loop ()
{
  if (!Rtc.IsDateTimeValid())
  {
    // Common Cuases:
    //    1) the battery on the device is low or even missing and the power line was disconnected
    Serial.println("RTC lost confidence in the DateTime!");
  }

  RtcDateTime currentTime = Rtc.GetDateTime();

  displayTime(currentTime);


  unsigned long now = millis();
  if (now - lastSampleTime >= fiveMinutes)                  // Get Temp and pH every 5 min and send to Domoticz
  {
    lastSampleTime += fiveMinutes;

    // Get current temperature
    temp = (sensors.getTempCByIndex(0));
    temp = 22.2;

    // Get pH Value from A0
    float pH_Value = GetpH();

    const int BUF_MAX = 64;
    char buf[BUF_MAX]; 
    const int VAL_MAX = 16;
    char val[VAL_MAX];

    //Convert float to string and display temp
    strcpy_P(buf, (const char*) F("Temp: "));
    dtostrf(temp, 4, 1, val);
    strcat(buf, val);
    u8x8.drawString(0, 5, buf);

    //Convert float to string and display pH
    // float pH_Value = 7.2;

    strcpy_P(buf, (const char*) F("pH: "));
    dtostrf(pH_Value, 5, 1, val);
    strcat(buf, val);
    u8x8.drawString(0, 7, buf);

    if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
      client.connect(http_site, http_port);

      //Send temp
      String url = "/json.htm?type=command&param=udevice&idx=36&nvalue=0&svalue=";
      client.connect(http_site, http_port);
      client.print(String("GET ") + url + temp + " HTTP/1.1\r\n" +
                   "Host: " + http_site + "\r\n" +
                   "Connection: close\r\n\r\n");

      //Send pH
      url = "/json.htm?type=command&param=udevice&idx=37&nvalue=0&svalue=";
      client.connect(http_site, http_port);
      client.print(String("GET ") + url + pH_Value + " HTTP/1.1\r\n" +
                   "Host: " + http_site + "\r\n" +
                   "Connection: close\r\n\r\n");

    } else {
      Serial.println("Error in WiFi connection");
    }
  }

  ControlLEDs();

  //Sync time with NTP Server once a day.
  if ((NTP_Hour = 3) && (NTP_Minute = 3) && (NTPSync = false)) {
    getNTP();
    NTPSync = true;
  }

  // Reset NTP Flag 2 mins later
  if ((NTP_Hour = 3) && (NTP_Minute = 5) && (NTPSync = true)) {
    NTPSync = false;
  }

  delay(250);

  setSecond = setSecond + 1;
}

//==============================================================================================================================================
// Functions
//==============================================================================================================================================

//==============================================================================================================================================
// print Date and Time and fill H, M, S, D, M, Y variables
//==============================================================================================================================================

#define countof(a) (sizeof(a) / sizeof(a[0]))

void printDateTime(const RtcDateTime& dt)
{
  char datestring[20];

  snprintf_P(datestring,
             countof(datestring),
             PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
             dt.Month(),
             dt.Day(),
             dt.Year(),
             dt.Hour(),
             dt.Minute(),
             dt.Second() );
  Serial.println(datestring);

  NTP_Month = dt.Month();
  NTP_Day = dt.Day();
  NTP_Year = dt.Year();
  NTP_Hour = dt.Hour();
  NTP_Minute = dt.Minute();
  NTP_Second = dt.Second();
}

//==============================================================================================================================================
// Display date and time on OLED
//==============================================================================================================================================

void displayTime(const RtcDateTime& dt)
{
  char datestring[20];

  snprintf_P(datestring,
             countof(datestring),
             PSTR("%02u:%02u:%02u"),
             dt.Hour(),
             dt.Minute(),
             dt.Second() );
  u8x8.drawString(4, 0, datestring);  // Display in middle of screen

  NTP_Hour = dt.Hour();
  NTP_Minute = dt.Minute();
  NTP_Second = dt.Second();

}

//==============================================================================================================================================
// Get Time from NTP Server
//==============================================================================================================================================

void getNTP() {
  boolean GotTime = false;
  int count = 0;  //Track count.

  while (GotTime == false) {    //Loop until we get the time from NTP. If after 10 tries still no connection then exit. We will try again the next NTP Sync.
    WiFi.hostByName(ntpServerName, timeServerIP);

    sendNTPpacket(timeServerIP); // send an NTP packet to a time server
    // wait to see if a reply is available
    delay(1000);

    int cb = udp.parsePacket();
    if (!cb) {
      Serial.println("no packet yet");
      count++;
      if (count > 10) {
        GotTime = true;   //Fake flag as we haven't got sync in 10 tries.
      }
    }
    else {
      Serial.print("packet received, length=");
      Serial.println(cb);
      // We've received a packet, read the data from it
      udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

      //the timestamp starts at byte 40 of the received packet and is four bytes,
      // or two words, long. First, esxtract the two words:

      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      unsigned long secsSince1900 = highWord << 16 | lowWord;
      const unsigned long seventyYears = 2208988800UL;
      unsigned long epoch = secsSince1900 - seventyYears;
      printDateTime(epoch);
      GotTime = true;
    }
    delay(3000);
  }
}

//==============================================================================================================================================
// send an NTP request to the time server at the given address
//==============================================================================================================================================

unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

//==============================================================================================================================================
// Fade lights out
//==============================================================================================================================================

void lightsOut () {

  float decrementGreen = (float) 5 / (30 * 60);
  float decrementRed = (float) 5 / (30 * 60);
  float decrementBlue = (float) 80 / (30 * 60);
  //float incrementWhite = (float) 0/(30*60);

  float greenVal = (float)(5 - (setSecond * decrementGreen));
  float redVal = (float)(5 - (setSecond * decrementRed));
  float blueVal = (float)(80 - (setSecond * decrementBlue));


  if (setSecond < 1800)   {

    analogWrite(RED, (int) redVal);
    analogWrite(GREEN, (int) greenVal);
    analogWrite(BLUE, (int) blueVal);
    u8x8.drawString(5, 2, "Lights out");

  }
}

//==============================================================================================================================================
// Fade lights on
//==============================================================================================================================================

void lightsOn () {

  float incrementGreen = (float) 130 / (30 * 60);
  float incrementRed = (float) 255 / (30 * 60);
  float incrementBlue = (float) 65 / (30 * 60);
  float incrementWhite = (float) 255 / (30 * 60);

  //sunrise begins!
  float greenVal = (float)(setSecond * incrementGreen);
  float redVal = (float)(setSecond * incrementRed);
  float blueVal = (float)(setSecond * incrementBlue);
  float whiteVal = (float)(setSecond * incrementWhite);

  if (setSecond < 1800) {

    analogWrite(RED, (int) redVal);
    analogWrite(GREEN, (int) greenVal);
    analogWrite(BLUE, (int) blueVal);
    //    analogWrite(WHITEPIN, (int) whiteVal);

  }
}

//==============================================================================================================================================
// SunSet
//==============================================================================================================================================

void sunSet () {

  float decrementGreen = (float) 125 / (30 * 60); //5
  float decrementRed = (float) 250 / (30 * 60); //5
  float incrementBlue = (float) 15 / (30 * 60); //80
  float incrementWhite = (float) 255 / (30 * 60); //0

  float greenVal = (float)(130 - (setSecond * decrementGreen));
  float redVal = (float)(255 - (setSecond * decrementRed));
  float blueVal = (float)(65 + (setSecond * incrementBlue));
  float whiteVal = (float)(255 - (setSecond * incrementWhite));

  if (setSecond < 1800) {

    analogWrite(RED, (int) redVal);
    analogWrite(GREEN, (int) greenVal);
    analogWrite(BLUE, (int) blueVal);
    printRGB((int) redVal, (int) greenVal, (int) blueVal);

  }
}

//==============================================================================================================================================
// Control RGB LEDS
//==============================================================================================================================================

void ControlLEDs() {

  if (NTP_Hour == 23 && NTP_Minute == 59 && NTP_Second == 59) {
    setSecond = 0;
  }
  if (NTP_Hour == 7 && NTP_Minute == 29 && NTP_Second == 59) {
    setSecond = 0;
  }
  if (NTP_Hour == 18 && NTP_Minute == 29 && NTP_Second == 59) {
    setSecond = 0;
  }

  if (NTP_Hour >= 18) {

    analogWrite(GREEN, 5);
    analogWrite(RED, 5);
    analogWrite(BLUE, 80);
    printRGB(5, 5, 80);
    u8x8.drawString(4, 2, "Evening  ");
    u8x8.drawString(0, 3, "CO2:OFF  T5:OFF ");
    digitalWrite(CO2, HIGH);
    digitalWrite(T5_Lights, HIGH);


  } else if (NTP_Hour < 1 && NTP_Minute < 30) {

    lightsOut();
    u8x8.drawString(5, 2, "Lights out");
    u8x8.drawString(0, 3, "CO2:OFF  T5:OFF ");
    digitalWrite(CO2, HIGH);
    digitalWrite(T5_Lights, HIGH);

  } else if (NTP_Hour > 1 && NTP_Hour < 7) {

    analogWrite(GREEN, 0);
    analogWrite(RED, 0);
    analogWrite(BLUE, 240);
    printRGB(0, 0, 240);
    u8x8.drawString(6, 2, "Night    ");

    u8x8.drawString(0, 3, "CO2:OFF  T5:OFF ");
    digitalWrite(CO2, HIGH);
    digitalWrite(T5_Lights, HIGH);

  } else if (NTP_Hour == 7 && NTP_Minute < 30) {

    analogWrite(GREEN, 0);
    analogWrite(RED, 50);
    analogWrite(BLUE, 240);
    printRGB(0, 0, 240);
    u8x8.drawString(5, 2, "Morning  ");

    u8x8.drawString(0, 3, "CO2: OFF T5: OFF");
    digitalWrite(CO2, HIGH);
    digitalWrite(T5_Lights, HIGH);


  } else if (NTP_Hour == 7 && NTP_Minute >= 30) {

    lightsOn();
    u8x8.drawString(5, 2, "Wake up  ");
    u8x8.drawString(0, 3, "CO2: ON  T5: ON");
    digitalWrite(CO2, LOW);
    digitalWrite(T5_Lights, LOW);


  } else if (NTP_Hour == 18 && NTP_Minute >= 30) {

    sunSet();
    u8x8.drawString(5, 2, "Sunset  ");
    u8x8.drawString(0, 3, "CO2: OFF T5: OFF");
    digitalWrite(CO2, HIGH);
    digitalWrite(T5_Lights, HIGH);

  } else if (NTP_Hour >= 8 && NTP_Hour < 19) {

    analogWrite(GREEN, 0);
    analogWrite(RED, 0);
    analogWrite(BLUE, 0);
    printRGB(0, 0, 0);
    u8x8.drawString(6, 2, "Day1   ");
    u8x8.drawString(0, 3, "CO2: ON  T5: ON");
    digitalWrite(CO2, LOW);
    digitalWrite(T5_Lights, LOW);

  } else if (NTP_Hour == 18 && NTP_Minute < 30) {
    analogWrite(GREEN, 130);
    analogWrite(RED, 255);
    analogWrite(BLUE, 65);
    printRGB(255, 130, 65);
    u8x8.drawString(6, 2, "Day2   ");
    u8x8.drawString(0, 3, "CO2: ON  T5: ON ");
    digitalWrite(CO2, LOW);
    digitalWrite(T5_Lights, LOW);

  }
}

//==============================================================================================================================================
// Print RGB colours to OLED for debugging purposes
//==============================================================================================================================================

void printRGB(int R, int G, int B) {
  char buf[10];
  sprintf(buf, "%d:%d:%d", R, G, B);
  u8x8.drawString(1, 1, buf);
}

//==============================================================================================================================================
// Get pH values. Sample 10 reading, strip highest and lowest and take average
//==============================================================================================================================================

float GetpH() {

  int buf[10];

  for (int i = 0; i < 10; i++) //Get 10 sample value from the sensor for smooth the value
  {
    buf[i] = 1023; //analogRead(pH_Reading);
    delay(10);
  }
  for (int i = 0; i < 9; i++) //sort the analog from small to large
  {
    for (int j = i + 1; j < 10; j++)
    {
      if (buf[i] > buf[j])
      {
        temp = buf[i];
        buf[i] = buf[j];
        buf[j] = temp;
      }
    }
  }
  avgValue = 0;
  for (int i = 2; i < 8; i++)               //take the average value of 6 center sample
    avgValue += buf[i];
  float phValue = (float)avgValue * 5.0 / 1024 / 6; //convert the analog into millivolt
  phValue = 73.0 * phValue;                  //convert the millivolt into pH value (1023 = 14pH)
  Serial.print("    pH:");
  Serial.print(phValue, 2);
  Serial.println(" ");
  digitalWrite(13, HIGH);
  delay(800);
  digitalWrite(13, LOW);
}
