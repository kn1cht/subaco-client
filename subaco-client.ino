/*=================================/
 ******** subaco-client.ino *******
 * Author  : kn1cht
 * Created : Oct 26 2017
 * IDE Ver : 1.8.5
 * Board   : ESP32 Dev Module
/=================================*/

#include <ArduinoJson.h>
#include <esp_deep_sleep.h>
#include <esp_sleep.h>
#include <esp32-hal-log.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <usbhub.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiMulti.h>

#include "ACS712_05B.hpp"
#include "my_config.h"

#define JST 3600 * 9
#define SW_1 22
#define SW_2 23
#define ACS712_VIOUT 34
#define ACS712_OFFST 35

WiFiMulti wifiMulti;
ACS712    acs712(ACS712_VIOUT, ACS712_OFFST);   // ACS712 current sensor
USB       Usb;

const char* ntp_server1 = "ntp.nict.jp";
const char* ntp_server2 = "ntp.jst.mfeed.ad.jp";

String product;
String serialNum;

void setup() {
  Serial.begin(115200);
  delay(10); // wait for serial monitor
  pinMode(SW_1, OUTPUT);
  pinMode(SW_2, OUTPUT);
  digitalWrite(SW_2, HIGH);
  log_i("Hello, this is ESP32.");
  addMyAP(wifiMulti); // my_config.h
  if (Usb.Init() == -1) { log_e("OSC did not start."); }
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
      while(Usb.getUsbTaskState() != USB_STATE_RUNNING) { Usb.Task(); }
      Usb.ForEachUsbDevice(&getStrDescriptors);
      digitalWrite(SW_1, LOW); // usb host shield is off
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
  root["current"] = current;
  root["from_device"] = "";
  root["to_device"] = to_dev;
  root["datetime"] = time_now;
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
  WiFiClient client;

  if (!client.connect(SUBACO_HOST, 443))
    log_e("Connection failed!");
  else {
    log_i("https connected");
    String req;
    req = "POST " + String(SUBACO_TAG) + " HTTP/1.1\r\n";
    req += "Content-Type: application/json\r\n";
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

void getStrDescriptors(UsbDevice *pdev) {
  uint8_t addr = pdev->address.devAddress;
  Usb.Task();
  if (Usb.getUsbTaskState() < USB_STATE_CONFIGURING) { return; } // state must be configuring or higher
  USB_DEVICE_DESCRIPTOR buf;
  if (Usb.getDevDescr(addr, 0, DEV_DESCR_LEN, (uint8_t *)&buf) != NULL) { return; }
  if (buf.iManufacturer > 0) {
    String manufacturer = getstrdescr(addr, buf.iManufacturer);
  }
  if (buf.iProduct > 0) {
    product = getstrdescr(addr, buf.iProduct);
  }
  if (buf.iSerialNumber > 0) {
    serialNum = getstrdescr(addr, buf.iSerialNumber);
  }
}

String getstrdescr(uint8_t addr, uint8_t idx) {
  uint8_t buf[256], rcode;
  if (rcode = Usb.getStrDescr(addr, 0, 1, 0, 0, buf) != NULL) { //get language table length
    log_e("Error retrieving LangID table length: %#08x", rcode);
    return "";
  }
  uint8_t length = buf[0]; //length is the first byte
  if (rcode = Usb.getStrDescr(addr, 0, length, 0, 0, buf) != NULL) { //get language table
    log_e("Error retrieving LangID table: %#08x", rcode);
    return "";
  }
  uint8_t langid = (buf[3] << 8) | buf[2];
  if (rcode = Usb.getStrDescr(addr, 0, 1, idx, langid, buf) != NULL) {
    log_e("Error retrieving string length: %#08x", rcode);
    return "";
  }
  length = buf[0];
  if (rcode = Usb.getStrDescr(addr, 0, length, idx, langid, buf) != NULL) {
    log_e("Error retrieving string: %#08x", rcode);
    return "";
  }
  String descString = "";
  for (uint8_t i = 2; i < length; i += 2) { //string is UTF-16LE encoded
    descString += (char)buf[i];
  }
  return descString;
}
