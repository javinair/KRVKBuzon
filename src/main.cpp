#include <Arduino.h>
#include <ArduinoOTA.h>
#include <UniversalTelegramBot.h>
#include <NTPClient.h>
#include <Timezone.h>
#include <TimeLib.h>
#include <LittleFS.h>
#include "FS.h"
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
#include <WifiCredentials.hpp>
#include <LoopMethods.hpp>

#if defined(ESP8266)
#include <WiFiClientSecure.h>
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <AsyncTCP.h>
#endif

/** LDR sensor **/
int ldrPin = A0;
int ldrValue = 0;


/** Loop **/
unsigned long previousMillis;
const long interval = 200;

int currentMethod = NONE;
const int previousSensorValuesSize = 5;
int previousSensorValues[previousSensorValuesSize];
int previousValuesIndex;

bool isReady = false;
bool doorOpen = false;
int startFrame, endFrame;

/** Read previous file **/
File file;
int myIntegers[] = {
  953, 952, 952, 951, 952, 952, 951, 952, 952, 953, 951, 952, 953, 951, 952, 953, 952, 952, 953, 952, 953,
  953, 952, 953, 953, 951, 951, 952, 950, 928, 871, 814, 760, 744, 745, 766, 820, 856, 904, 928, 942, 942, 944,
  945, 950, 949, 951, 950, 951, 950, 950, 951, 953, 950, 951, 948, 938, 922, 905, 889, 872, 856, 838, 821, 796,
  775, 757, 740, 736, 736, 738, 738, 742, 739, 747, 756, 765, 784, 809, 832, 854, 873, 891, 913, 932, 940, 943,
  946, 944, 948, 948, 950, 950, 950, 949, 949, 949, 951, 950, 950, 952, 952, 951, 952, 952, 952, 950
};
int numberOfElements = sizeof(myIntegers) / sizeof(myIntegers[0]);

int currentReadIndex;
int percentage = 10;


/** NTP and local time **/
WiFiUDP ntpUDP;
TimeChangeRule myDST = {"CEST", Last, Sun, Mar, 2, 120}; // Spain summer time (Central European Summer Time)
TimeChangeRule mySTD = {"CET", Last, Sun, Oct, 3, 60};  // Spain winter time (Central European Time)
Timezone myTZ(myDST, mySTD);
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org");

String getLocalTime()
{
  // UTC time
  time_t utcTime = timeClient.getEpochTime();
  time_t localTime = myTZ.toLocal(utcTime);
  int hours = hour(localTime);
  int minutes = minute(localTime);
  int seconds = second(localTime);

  char formattedTime[9]; // 8 character plus null to finish the string
  sprintf(formattedTime, "%02d:%02d:%02d", hours, minutes, seconds);
  return formattedTime;
}

/** LittleFS **/
void appendFile(fs::FS &fs, const char * path, const char * message){
    File file = fs.open(path, "a+");
    if(!file)
    {
        WebSerial.println("- failed to open file for appending");
        return;
    }
    file.print(message);
    file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message){
    File file = fs.open(path, "w+");
    if(!file)
    {
        WebSerial.println("- failed to open file for writing");
        return;
    }
    file.print(message);
    file.close();
}

void readFile(fs::FS &fs, const char * path){
    File file = fs.open(path,"r+");
    if(!file || file.isDirectory())
    {
        WebSerial.println("- failed to open file for reading");
        return;
    }

    while(file.available())
    {
        WebSerial.println(file.readString());
    }
    file.close();
}

void deleteFile(fs::FS &fs, const char * path){
    if(fs.remove(path))
    {
        WebSerial.println("- file deleted");
    } else{
        WebSerial.println("- delete failed");
    }
}

void splitStringToInteger(const String &input, char separator, LinkedList<int> &output) {
  int startIndex = 0;
  int endIndex = 0;
  while (endIndex < input.length()) {
    if (input[endIndex] == separator) {
      String subString = input.substring(startIndex, endIndex);
      output.add(subString.toInt());
      startIndex = endIndex + 1;
    }
    endIndex++;
  }
  if (startIndex < endIndex) {
    String subString = input.substring(startIndex, endIndex);
    output.add(subString.toInt());
  }
}

int countOccurrences(const String &text, char target) {
  int count = 0;
  for (int i = 0; i < text.length(); i++) {
    if (text[i] == target) {
      count++;
    }
  }
  return count;
}

void readPreviousFile()
{
    WebSerial.println("Start...");

    currentMethod = READ_PREVIOUS_FILE;
    currentReadIndex = 0;
}


/** WebSerial **/
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
    readPreviousFile();
  } else {
    WebSerial.println("Unknown command");
  }
}

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
  Serial.print("Ready on ");
  Serial.println(WiFi.localIP());

  // OTA
  ArduinoOTA.begin();

  // Telegram
  clientTCP.setInsecure();

  // NTP
  timeClient.begin();
  timeClient.update();

  // WebSerial is accessible at "<IP Address>/webserial" in browser
  WebSerial.begin(&server);
  WebSerial.msgCallback(recvMsg);
  server.begin();

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

void mainControl()
{
  if(currentMethod == READ_PREVIOUS_FILE)
  {
    if(isReady)
    {
      int previousMaxValue = getPreviousMaxValue();
      if(myIntegers[currentReadIndex] < (previousMaxValue * ((100-percentage)*0.01)) && !doorOpen)
      {
        WebSerial.println("Door open on myIntegers["+String(currentReadIndex)+"] = "+String(myIntegers[currentReadIndex])+" compared with: "+String(previousMaxValue));
        startFrame = currentReadIndex;
        doorOpen = true;
      }
      
      if(myIntegers[currentReadIndex] >= previousMaxValue * ((100-percentage)*0.01) && doorOpen)
      {
        endFrame = currentReadIndex;
        WebSerial.println("Door open for "+String(endFrame-startFrame)+" frames");
        endFrame = 0;
        startFrame = 0;
        doorOpen = false;
      }
    }

    previousSensorValues[previousValuesIndex] = myIntegers[currentReadIndex];
    previousValuesIndex = (previousValuesIndex + 1) % previousSensorValuesSize;
    currentReadIndex++;

    if (previousValuesIndex == 0)
    {
      isReady = true;
    }

    if(currentReadIndex >= numberOfElements)
    {
      reset();      
      WebSerial.println("End");
    }
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
  //   WebSerial.println("["+String(currentReadIndex)+"]  "+myIntegers[currentReadIndex]);

  //   currentReadIndex++;
  //   if(currentReadIndex >= elements)
  //   {
  //     currentReadIndex = 0;
  //     currentMethod = NONE;
  //   }
  // }
}