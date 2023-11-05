// #define WEB_DEBUG_MODE

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <UniversalTelegramBot.h>
#include <NTPClient.h>
#include <Timezone.h>
#include <TimeLib.h>
#include <LittleFS.h>
#include "FS.h"
#include <ESPAsyncWebServer.h>
#include <WifiCredentials.hpp>
#include <LoopMethods.hpp>

#if defined(ESP8266)
#include <WiFiClientSecure.h>
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <AsyncTCP.h>
#endif

#ifdef WEB_DEBUG_MODE
  #include <WebSerial.h>
#endif

/** LDR sensor **/
int ldrPin = A0;
int ldrValue = 0;


/** Loop **/
unsigned long previousMillis;
const long interval = 200;

/** Main method **/
int currentMethod = NONE;
const int previousSensorValuesSize = 5;
int previousSensorValues[previousSensorValuesSize];
int previousValuesIndex;
bool isReady = false;
bool doorOpen = false;
unsigned long startFrame, endFrame;
int percentage = 10;
int flagValue;

/** Real example **/
int night_values[] = {1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,921,893,883,877,881,992,1014,1022,1024,1024,1024,1024,900,876,870,868,864,864,861,859,840,917,998,1014,1022,1024,1024,1024,1024,1024,1024,1024,1024};
int example_values[] = {900,900,900,903,900,900,900,900,675,484,487,859,886,893,895,896,897,897,898,481,483,810,883,892,896,897,900,900,900,480,483,482,481,481,481,481,482,481,481,469,843,886,894,897,898,897,894,888,856,763,640,555,501,476,451,446,446,449,459,464,469,473,481,481,481,483,483,483,483,483,481,483,483,484,484,484,486,484,486,483,486,481,476,471,462,449,463,507,561,622,753,884,896,897,900,900,901,902,902,902,902,901,902,902};
int sunny_values[] = {360,360,360,360,360,360,360,361,361,361,361,100,45,46,41,222,353,358,360,360,361,361,361,48,46,48,46,46,46,41,105,355,360,362,363,362,292,133,66,51,45,41,41,43,45,45,46,46,46,46,46,46,46,46,46,46,45,43,41,43,56,84,362,363,352,326,214,43,45,46,46,46,48,46,46,46,46,46,46,45,45,43,41,45,53,74,153,354,339,355,357,357,357,320,43,49,342,349,354,358,361,363,365,364,364,364,364,364,364,365,364};
int numberOfElements_night_values = sizeof(night_values) / sizeof(night_values[0]);
int numberOfElements_example_values = sizeof(example_values) / sizeof(example_values[0]);
int numberOfElements_sunny_values = sizeof(sunny_values) / sizeof(sunny_values[0]);
int currentReadIndex;


/** NTP and local time **/
WiFiUDP ntpUDP;
TimeChangeRule myDST = {"CEST", Last, Sun, Mar, 2, 120}; // Spain summer time (Central European Summer Time)
TimeChangeRule mySTD = {"CET", Last, Sun, Oct, 3, 60};  // Spain winter time (Central European Time)
Timezone myTZ(myDST, mySTD);
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org");


void reset()
{
  currentMethod = NONE;
  previousValuesIndex = 0;
  currentReadIndex = 0;
  startFrame = 0;
  endFrame = 0;
  doorOpen = false;
  isReady = false;
  flagValue = 1024;
}

void readData()
{
    reset();
    currentMethod = READ_PREVIOUS_FILE;
}

void printData()
{
    reset();

    currentMethod = PRINT_DATA;
}

/** WebSerial **/
#ifdef WEB_DEBUG_MODE
  AsyncWebServer server(80);

  void recvMsg(uint8_t *data, size_t len){
    String command = "";
    for(size_t i=0; i < len; i++){
      command += char(data[i]);
    }
    WebSerial.println(">>> "+command);

    command.trim();
    ldrValue = analogRead(ldrPin);

    if (command == "re") {
      readData();
    } else if (command == "rn") {
      readData();
    } else if (command == "rs") {
      readData();    
    } else if (command == "s") {
      reset();
    } else if (command == "p") {
      printData();
    } else {
      WebSerial.println("Unknown command");
    }
  }
#endif

/** Telegram **/
String botToken = TELEGRAM_TOKEN;
String chatID = TELEGRAM_CHAT_ID;
WiFiClientSecure clientTCP;
UniversalTelegramBot bot(botToken, clientTCP);

/** Main code **/
void setup()
{
  Serial.begin(9600);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    ESP.restart();
  }

  // OTA
  ArduinoOTA.begin();

  // Telegram
  clientTCP.setInsecure();

  // NTP
  timeClient.begin();
  timeClient.update();

#ifdef WEB_DEBUG_MODE
  //WebSerial is accessible at "<IP Address>///WebSerial" in browser
  WebSerial.begin(&server);
  WebSerial.msgCallback(recvMsg);
  server.begin();
#endif

  if(!LittleFS.begin()){
    Serial.println("LittleFS Mount Failed");
    return;
  }

}

int getPreviousMaxValue()
{
  int max = 0;

  for(int i = 0; i < previousSensorValuesSize; i++) {
    if (previousSensorValues[i] > max) {
      max = previousSensorValues[i];
    }
  }

  return max;
}

int getValueFromArray_example_values()
{
  return example_values[currentReadIndex];
}

int getValueFromArray_night_values()
{
  return night_values[currentReadIndex];
}

int getValueFromArray_sunny_values()
{
  return sunny_values[currentReadIndex];
}

int getValueFromSensor()
{
  return analogRead(ldrPin);
}

void mainControl()
{
  if(currentMethod == READ_PREVIOUS_FILE)
  {
    int currentValue = getValueFromArray_sunny_values();
    #ifdef DEBUG_MODE
      // WebSerial.println(String(currentValue));
    #endif
    if(isReady)
    {
      int previousMaxValue = getPreviousMaxValue();
      if(currentValue < (previousMaxValue * ((100-percentage)*0.01)) && !doorOpen)
      {
        flagValue = previousMaxValue * ((100-percentage)*0.01);
        #ifdef WEB_DEBUG_MODE
          WebSerial.println("Door open with value: "+String(currentValue)+" compared with: "+String(previousMaxValue));
        #endif
        startFrame = millis();
        doorOpen = true;
      }
      if(currentValue >= flagValue && doorOpen)
      {
        endFrame = millis();
        #ifdef WEB_DEBUG_MODE
          WebSerial.println("Door open for "+String(endFrame-startFrame)+" millis");
        #endif
        endFrame = 0;
        startFrame = 0;
        doorOpen = false;
        bot.sendMessage(TELEGRAM_CHAT_ID, String("\xF0\x9F\x93\xAC"), ""); //📬
      }
    }

    previousSensorValues[previousValuesIndex] = currentValue;
    previousValuesIndex = (previousValuesIndex + 1) % previousSensorValuesSize;
    currentReadIndex++;

    if (previousValuesIndex == 0)
    {
      isReady = true;
    }

    if(currentReadIndex >= numberOfElements_sunny_values)
    {
      reset();      
    }
  }

  if(currentMethod == PRINT_DATA)
  {
    #ifdef WEB_DEBUG_MODE
      WebSerial.println(getValueFromSensor());
    #endif
  }
}

void loop()
{
  unsigned long currentMillis = millis();

  // OTA
  ArduinoOTA.handle(); 

  if (currentMillis - previousMillis >= interval)
  {
    mainControl();    

    previousMillis = currentMillis;
  }

  // // Read whole array and print it
  // if(currentMethod == READ_PREVIOUS_FILE)
  // {
  //   //WebSerial.println("["+String(currentReadIndex)+"]  "+myIntegers[currentReadIndex]);

  //   currentReadIndex++;
  //   if(currentReadIndex >= elements)
  //   {
  //     currentReadIndex = 0;
  //     currentMethod = NONE;
  //   }
  // }
}