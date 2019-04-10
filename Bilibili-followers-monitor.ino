//硬件连接说明：
//MAX7219 --- ESP8266
//  VCC   --- 3V(3.3V)
//  GND   --- G (GND)
//  DIN   --- D7(GPIO13)
//  CS    --- D1(GPIO5)
//  CLK   --- D5(GPIO14)
#include <FS.h>
#include <SPI.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#define CS_PIN 5  //D1
#define KEY_PIN 4 //D2

String AP_SSID  = "BiliBili monitor";
String AP_PWD   = "HiGeekStudio";

DynamicJsonDocument jsonBuffer(400);
WiFiClient client;
IPAddress apIP(192, 168, 131, 1);
ESP8266WebServer webServer(80);
String ssid, password, biliuid;

void setup()
{
  Serial.begin(115200);
  SPI.begin();
  EEPROM.begin(128);
  SPIFFS.begin();

  pinMode(CS_PIN, OUTPUT);
  pinMode(KEY_PIN, INPUT_PULLUP);

  digitalWrite(CS_PIN, LOW);
  sendCommand(12, 1);               //显示控制 1使能 0失能
  sendCommand(15, 1);               //全亮测试
  delay(500);
  sendCommand(15, 0);
  sendCommand(10, 15);              //亮度 15是最大
  sendCommand(11, 7);               //扫描位数，八个数
  sendCommand(9, 0xff);             //解码模式，详情请百度MAX7219
  digitalWrite(CS_PIN, HIGH);

  ssid = eepromReadStr(0x00);
  password = eepromReadStr(0x20);
  biliuid = eepromReadStr(0x40);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PWD);
  WiFi.begin(ssid, password);

  webServer.on("/", handleRoot);
  webServer.on("/setup", HTTP_POST, handleUpdate);
  webServer.on("/status",handleStatus);
  webServer.begin();
}

void loop()
{
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("http://api.bilibili.com/x/relation/stat?vmid=" + biliuid);
    int httpCode = http.GET();

    Serial.print("HttpCode:");
    Serial.print(httpCode);

    if (httpCode == 200) {
      String resBuff = http.getString();
      DeserializationError error = deserializeJson(jsonBuffer, resBuff);
      if (error) { //这段异常处理没有必要，因为B站的API基本上不会变。所以接收到的数据不会有变化，也就不会出错。
        Serial.println("json error");
        errorCode(3);  //JSON反序列化错误，强行中止运行
        while (1);
      }
      JsonObject root = jsonBuffer.as<JsonObject>();
      long code = root["code"];
      if (code != 0) {
        errorCode(1); //UID错误，强行中止运行
        while(1);
      }
      long fans = root["data"]["follower"];
      Serial.print("     Fans:");
      Serial.println(fans);
      displayNumber(fans);
      delay(1000);
    }
    else {
      errorCode(2); //API无法连接，请检查网络是否通常。或者有可能是API地址更新。
      while(1);
    }
  }
}

void connectWiFi(){
  webServer.handleClient();
  while(WiFi.status() != WL_CONNECTED) {
    sendCommand(9, 0x00);
    for(int i = 2; i < 0x80; i = i << 1) {
      for(int x = 1; x < 9; x++) {
        sendCommand(x, i);
      }
      delay(100);
    }
  }
  sendCommand(9, 0xff);
}

void sendCommand(int command, int value) {
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(command);
  SPI.transfer(value);
  digitalWrite(CS_PIN, HIGH);
}

void displayNumber(int number) {
  int x;
  int tmp = number;
  for (x = 1; tmp /= 10; x++);
  for(int i = 1; i < 9; i++) sendCommand(i, 0x0f);
  int y = (8 - x) / 2;
  for(int i = 0; i < x; i++) {
    sendCommand(i+y+1, number%10);
    number /= 10;
  }
}
/* *
 * Web服务器事件
 * */
void handleRoot()
{
  Serial.println("[Debug]->Web访问: /");
  File f = SPIFFS.open("/index.html", "r");
  String httpBuff = f.readString();
  f.close();
  webServer.send(200, "text/html", httpBuff);
}

void handleStatus(){
    Serial.println("[Debug]->Web访问: /status");
    String httpBuff;//建立http缓存
    httpBuff += ssid;
    httpBuff += ";";
    httpBuff += password;
    httpBuff += ";";
    httpBuff += biliuid;
    webServer.send(200, "text/html", httpBuff);
}

void handleUpdate()
{
    Serial.println("[Debug]->Web提交: /post");//只是Debug
  String arg_ssid = webServer.arg("ssid");
  String arg_passwd = webServer.arg("passwd");
  String arg_uid = webServer.arg("uid");
  Serial.println(arg_ssid + " " + arg_passwd + " " + arg_uid);
  if (arg_ssid == "" || arg_passwd == "" || arg_uid == "")
  {
    webServer.send(400, "text/plain", "Incomplete");//不成功，缺少参数
  }
  else
  {
    eepromWriteStr(0x00, arg_ssid);
    eepromWriteStr(0x20, arg_passwd);
    eepromWriteStr(0x40, arg_uid);
    webServer.send(200, "text/plain", "OK");//成功
  }
}
/* *
 * 显示ERROR CODE
 * */
void errorCode(int e_code){
  sendCommand(9, 0x01);
  sendCommand(8, 0x4f);
  sendCommand(7, 0x05);
  sendCommand(6, 0x05);
  sendCommand(5, 0x1d);
  sendCommand(4, 0x05);
  sendCommand(3, 0x01);
  sendCommand(2, 0x01);
  sendCommand(1, e_code);
}
/* *
 * 读取字符串
 * */
String eepromReadStr(u16 addr) {
  String str;
  char buff;
  for(u16 i = 0; i < 16; i++) {
    buff = EEPROM.read(addr+i);
    if (buff =='\0') {
      return str;
    }
    str += buff;
  }
  return "ERROR";
}
/* *
 * 写入字符串
 * */
void eepromWriteStr(u16 addr, String str) {
  u16 numToWrite = str.length()+1;
  for(u16 i = 0; i < numToWrite; i++) {
    EEPROM.write(addr+i,str[i]);
  }
  EEPROM.commit();
}
