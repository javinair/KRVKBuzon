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

#if defined(ESP8266)
#include <WiFiClientSecure.h>
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <AsyncTCP.h>
#endif

/** LDR sensor **/
int ldrPin = A0;
int readValue = 0;


/** Loop **/
unsigned long previousMillis = 0;
const long interval = 180000;


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


/** WebSerial **/
AsyncWebServer server(80);

void recvMsg(uint8_t *data, size_t len){
  String command = "";
  for(size_t i=0; i < len; i++){
    command += char(data[i]);
  }
  WebSerial.println(">>> "+command);

  command.trim();
  readValue = analogRead(ldrPin);

  if (command == "r1") {
    readFile(LittleFS, "/sensor.txt");
  } else if (command == "a1") {
    appendFile(LittleFS, "/sensor.txt",(getLocalTime()+","+readValue+",\r\n").c_str());
  } else if (command == "d1") {
    deleteFile(LittleFS, "/sensor.txt");
  } else if (command == "r2") {
    readFile(LittleFS, "/button.txt");
  } else if (command == "a2") {
    appendFile(LittleFS, "/button.txt",(getLocalTime()+",,"+readValue+"\r\n").c_str());
  } else if (command == "d2") {
    deleteFile(LittleFS, "/button.txt");
  }
  else {
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

void loop()
{
  unsigned long currentMillis = millis();

  // OTA
  ArduinoOTA.handle();

  if (currentMillis - previousMillis >= interval)
  {
    readValue = analogRead(ldrPin);
    appendFile(LittleFS, "/sensor.txt",(getLocalTime()+","+readValue+",\r\n").c_str());

    previousMillis = currentMillis;
  }  
}