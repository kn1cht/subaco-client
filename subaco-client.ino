/*=================================/
 ******** subaco-client.ino *******
 * Author  : kn1cht
 * Created : Oct 26 2017
 * IDE Ver : 1.8.5
 * Board   : ESP32 Dev Module
/=================================*/

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiMulti.h>
#include <esp_deep_sleep.h>
#include <esp_sleep.h>
#include <esp32-hal-log.h>

#include "ACS712_05B.hpp"
#include "my_config.h"

#define JST 3600 * 9
#define RESET 21
#define SW_1 22
#define SW_2 23
#define ACS712_VIOUT 34
#define ACS712_OFFST 35

WiFiMulti wifiMulti;
HardwareSerial USBHost(2);
ACS712 acs712(ACS712_VIOUT, ACS712_OFFST);   // ACS712 current sensor

const char* ntp_server1 = "ntp.nict.jp";
const char* ntp_server2 = "ntp.jst.mfeed.ad.jp";

String product;
String serialNum;

void setup() {
  Serial.begin(115200);
  USBHost.begin(115200);
  delay(10); // wait for serial monitor
  pinMode(RESET, OUTPUT);
  pinMode(SW_1, OUTPUT);
  pinMode(SW_2, OUTPUT);
  digitalWrite(RESET, HIGH);
  digitalWrite(SW_2, HIGH);
  log_i("Hello, this is ESP32.");
  addMyAP(wifiMulti); // my_config.h
}

void loop() {
  float current = acs712;
  log_i("current: %.2lf mA", current * 1000);
  log_i("product/serial %d %d", product.length(), serialNum.length());
  if(current >= 0.05) {
    if(product.length() == 0 && serialNum.length() == 0) {
      digitalWrite(SW_2, LOW);
      delay(5);
      digitalWrite(SW_1, HIGH);
      String usb_descr[3];
      while(usb_descr[1].length() == 0 || usb_descr[2].length() == 0) {
        while(USBHost.available() <= 0) { delay(50); }
        while(USBHost.available() > 0) {
          String str = USBHost.readStringUntil('\n');
          log_d("Received str: %s", str.c_str());
          if(str.startsWith("0")) { usb_descr[0] = str; }
          else if(str.startsWith("1")) { usb_descr[1] = str; }
          else if(str.startsWith("2")) { usb_descr[2] = str; }
        }
        digitalWrite(RESET, LOW);
        delay(5);
        digitalWrite(RESET, HIGH);
        delay(1000);
      }
      while(USBHost.available()) { USBHost.read(); }

      digitalWrite(SW_1, LOW); // usb host shield is off
      product = usb_descr[1].substring(2);
      serialNum = usb_descr[2].substring(2);
    }
    log_i("Product Name: %s", product.c_str());
    log_i("Serial Number: %s", serialNum.c_str());
    digitalWrite(SW_2, HIGH);

    connectToWiFi();
    configTime(JST, 0, ntp_server1, ntp_server2); // time setting
    delay(100);
    time_t time_now = time(NULL);
    log_d("Time: %s", ctime(&time_now));
    postToServer(getJSON(current, time_now, product));
    WiFi.disconnect();
  }
  else {
    product = "";
    serialNum = "";
  }
  delay(10000);
}

String getJSON(float current, time_t time_now, String to_dev) {
    StaticJsonBuffer<500> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["app"] = SUBACO_APP_ID;
    JsonObject& record = root.createNestedObject("record");
    JsonObject& datetime = root.createNestedObject("datetime");
    JsonObject& currentA = record.createNestedObject("currentA");
    JsonObject& from_device = record.createNestedObject("from_device");
    JsonObject& to_device = record.createNestedObject("to_device");
    currentA["value"] = current;
    from_device["value"] = "";
    to_device["value"] = to_dev;
    datetime["value"] = time_now;
    String res;
    root.printTo(res);
    return res;
}

void connectToWiFi() {
  WiFi.disconnect();
  WiFi.begin("lolllool","loooooooool"); //Can be a nonexistent network
  while(wifiMulti.run() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
  }
  log_i("WiFi connected! IP address: ");
  Serial.println(WiFi.localIP());
}

void postToServer(String jsonStr) {
  WiFiClientSecure client;
  client.setCACert(ca_cert);

  if (!client.connect(SUBACO_HOST, 443))
    log_e("Connection failed!");
  else {
    log_i("https connected");
    String req;
    req = "POST " + String(SUBACO_TAG) + " HTTP/1.1\r\n";
    req += "Content-Type: application/json\r\n";
    req += "X-Cybozu-API-Token: " + String(SUBACO_API_TOKEN) + "\r\n";
    req += "Host: " + String(SUBACO_HOST) + "\r\n";
    req += "Content-Length: " + String(jsonStr.length()) + "\r\n\r\n";
    req += jsonStr + "\0";
    client.print(req);
    client.flush();
    while (!client.available()){ delay(50); }
    while (client.connected()) {
      if (client.readStringUntil('\n') == "\r") { break; }
    }
    while (client.available()) {
      char c = client.read();
      Serial.write(c);
    }
    client.stop();
  }
}

//esp_sleep_enable_timer_wakeup(uint64_t time_in_us);
//esp_light_sleep_start();
