#define WEB_DEBUG_MODE

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

/** Real example **/
int myIntegers[] = {
  953, 952, 952, 951, 952, 952, 951, 952, 952, 953, 951, 952, 953, 951, 952, 953, 952, 952, 953, 952, 953,
  953, 952, 953, 953, 951, 951, 952, 950, 928, 871, 760, 744, 745, 766, 820, 856, 904, 928, 942, 942, 944,
  945, 950, 949, 951, 950, 951, 950, 950, 951, 953, 950, 951, 948, 938, 922, 905, 889, 872, 856, 838, 821, 796,
  775, 757, 740, 736, 736, 738, 738, 742, 739, 747, 756, 765, 784, 809, 832, 854, 873, 891, 913, 932, 940, 943,
  946, 944, 948, 948, 950, 950, 950, 949, 949, 949, 951, 950, 950, 952, 952, 951, 952, 952, 952, 950
};
int numberOfElements = sizeof(myIntegers) / sizeof(myIntegers[0]);
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
}

void readData()
{
    reset();
    //WebSerial.println("Start...");

    currentMethod = READ_PREVIOUS_FILE;
}

void printData()
{
    reset();
    //WebSerial.println("Print data...");

    currentMethod = PRINT_DATA;
}


#ifdef WEB_DEBUG_MODE
//WebSerial **/
AsyncWebServer server(80);

void recvMsg(uint8_t *data, size_t len){
  String command = "";
  for(size_t i=0; i < len; i++){
    command += char(data[i]);
  }
  WebSerial.println(">>> "+command);

  command.trim();
  ldrValue = analogRead(ldrPin);

  if (command == "r") {
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

  readData();   
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

int getValueFromArray()
{
  return myIntegers[currentReadIndex];
}

int getValueFromSensor()
{
  return analogRead(ldrPin);
}

void mainControl()
{
  if(currentMethod == READ_PREVIOUS_FILE)
  {
    int currentValue = getValueFromSensor();

    if(isReady)
    {
      int previousMaxValue = getPreviousMaxValue();
      if(currentValue < (previousMaxValue * ((100-percentage)*0.01)) && !doorOpen)
      {
        #ifdef WEB_DEBUG_MODE
          WebSerial.println("Door open: "+String(currentValue)+" compared with: "+String(previousMaxValue));
        #endif
        startFrame = millis();
        doorOpen = true;
      }
      
      if(currentValue >= previousMaxValue * ((100-percentage)*0.01) && doorOpen)
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

    // if(currentReadIndex >= numberOfElements)
    // {
    //   reset();      
    //   //WebSerial.println("End");
    // }
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