/**********************************************************************
 * 项目：bilibili粉丝数监视器
 * 硬件：适用于NodeMCU ESP8266 + MAX7219
 * 功能：连接WiFi后获取指定用户的哔哩哔哩实时粉丝数并在8位数码管上居中显示
 * 作者：flyAkari 会飞的阿卡林 bilibili UID:751219
 * 源作者：HiGeek工作室 BiliBili UID:4893237
 * 日期：2018/09/18
 **********************************************************************/
/*2018/12/18 V1.1 更新说明：
  上电后数码管显示初始化为"--------", 直到获取到粉丝数.
  可从串口监视器输入数字测试显示连接是否正常, 波特率选择119200.*/

/*
  2019/4/6 V1.2 更新说明“
  原作者HiGeek来维护了当时瞎鸡儿乱写的代码，逻辑错乱，功能死板。居然还有while(1)的傻吊操作。
  在今天刷BiliBili的时候无意中发现自己当初瞎吉儿乱写的代码居然在流通，顿时感觉脸红耳赤，非常丢人。于是乎fork下来，重新维护位于2018/8/23写的粪代码。

  ***加了ERRORCODE显示，如果出现错误会在数码管上显示错误代码
  Error--1 UID（用户ID）填写错误
  Error--2 API无法连接，请检查当前使用的无线网能否正常浏览BiliBili。或者有可能是API地址更新。

  ***加了开机简单的自建，用于检查数码管是否有坏块。

  ***加了WiFi连接过程的动画

  ***加了WiFi断线重连
*/

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

//---------------修改此处""内的信息--------------------
// String ssid     = "HiGeek1";  //WiFi名
// String password = "789456123";  //WiFi密码
// String biliuid  = "4893237";  //bilibili UID 用户ID
String AP_SSID  = "BiliBili monitor";
String AP_PWD   = "HiGeek Studio";
//----------------------------------------------------

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
  WiFi.softAP(AP_SSID, AP_PWD, 6, 0, 1);
  WiFi.begin(ssid, password);
  // connectWiFi();

  webServer.on("/", handleRoot);
  webServer.on("/setup", HTTP_POST, handleUpdate);
  webServer.begin();
  Serial.println("DEBUG1");
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
  while(WiFi.status() != WL_CONNECTED) {
    sendCommand(9, 0x00);
    webServer.handleClient();
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
  httpBuff.replace("{{ssid}}", ssid);
  httpBuff.replace("{{passwd}}", password);
  httpBuff.replace("{{uid}}", biliuid);
  webServer.send(200, "text/html", httpBuff);
  Serial.println("DEBUG2");
}

void handleUpdate()
{
  String arg_ssid = webServer.arg("ssid");
  String arg_passwd = webServer.arg("passwd");
  String arg_uid = webServer.arg("uid");
  Serial.println(arg_ssid + " " + arg_passwd + " " + arg_uid);
  if (arg_ssid == "" || arg_passwd == "" || arg_uid == "")
  {
    webServer.send(400, "text/plain", "输入参数不完整");
  }
  else
  {
    eepromWriteStr(0x00, arg_ssid);
    eepromWriteStr(0x20, arg_passwd);
    eepromWriteStr(0x40, arg_uid);
    webServer.send(200, "text/plain", "设置成功");
  }
  Serial.println("DEBUG3");
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
