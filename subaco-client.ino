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
#define USB_D_PLS    GPIO_NUM_12
#define USB_D_MNS    GPIO_NUM_14
#define UHS_POWER    GPIO_NUM_16
#define USB_SW_ON    GPIO_NUM_26
#define BAT_SW_OFF   GPIO_NUM_27
#define ACS712_VIOUT GPIO_NUM_34
#define ACS712_OFFST GPIO_NUM_35

struct subacoDevice {
  String manufacturer;
  String product;
  String serialNum;
  bool empty() {
    return manufacturer.length() == 0 && product.length() == 0 && serialNum.length() == 0;
  }
  void print() {
    log_i("Manufacturer  : %s", manufacturer.c_str());
    log_i("Product Name  : %s", product.c_str());
    log_i("Serial Number : %s", serialNum.c_str());
  }
};

enum subacoChargeState { 
  SUBACO_CHARGE_START = 1,
  SUBACO_CHARGE_ONGOING = 2,
  SUBACO_CHARGE_END = 0 
};
ACS712*      acs712; // ACS712 current sensor
subacoDevice device;
USB          Usb;
WiFiMulti    wifiMulti;

unsigned long preTime = 0;
const char*   ntp_server1 = "ntp.nict.jp";
const char*   ntp_server2 = "ntp.jst.mfeed.ad.jp";

void setup() {
  Serial.begin(115200);
  delay(10); // wait for serial monitor
  log_i("Hello, this is ESP32.");
  preTime = millis();
  addMyAP(wifiMulti); // my_config.h
  
  acs712 = new ACS712(ACS712_VIOUT, ACS712_OFFST);
  pinMode(USB_D_PLS, INPUT_PULLDOWN);
  pinMode(USB_D_MNS, INPUT_PULLDOWN);
  pinMode(UHS_POWER, OUTPUT);
  digitalWrite(UHS_POWER, HIGH);
  if (Usb.Init() == -1) { log_e("OSC did not start."); }

  pinMode(USB_SW_ON, OUTPUT);
  digitalWrite(USB_SW_ON, HIGH);
  pinMode(BAT_SW_OFF, OUTPUT);
  digitalWrite(BAT_SW_OFF, LOW);
}

void loop() {
  float current = *acs712;
  int usbPin = digitalRead(USB_D_PLS) + digitalRead(USB_D_MNS);
  if(!usbPin && (device.empty() || current < 0.05)) {
    if(!device.empty()) {
      connectToWiFi();
      postToServer(getJSON(current, device, SUBACO_CHARGE_END), "append");
      WiFi.disconnect();
    }
    digitalWrite(UHS_POWER, LOW);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext1_wakeup((1<<USB_D_PLS) + (1<<USB_D_MNS), ESP_EXT1_WAKEUP_ANY_HIGH);
    log_i("Sleep starting...");
    esp_deep_sleep_start();
  }
  
  if(device.empty()) {
    log_i("retrieving usb descr...");
    digitalWrite(USB_SW_ON, LOW);
    delay(2);
    while(Usb.getUsbTaskState() < USB_STATE_RUNNING) { Usb.Task(); }
    Usb.ForEachUsbDevice(&getStrDescriptors);
    device.print();
    digitalWrite(UHS_POWER, LOW);
    digitalWrite(USB_SW_ON, HIGH);
    digitalWrite(BAT_SW_OFF, HIGH);
    delay(50);
    digitalWrite(BAT_SW_OFF, LOW);
    connectToWiFi();
    configTime(JST, 0, ntp_server1, ntp_server2); // time setting
    delay(100);
    postToServer(getJSON(0, device, SUBACO_CHARGE_START), "start");
    WiFi.disconnect();
    digitalWrite(UHS_POWER, HIGH);
    while(Usb.getUsbTaskState() < USB_STATE_RUNNING) { Usb.Task(); }
  }
  else if(millis() - preTime > 20000) { // every 20 seconds
  log_i("current: %.2lf mA", current * 1000);
  log_i("USB pins: %s", usbPin ? "HIGH" : "LOW");
    preTime = millis();
    connectToWiFi();
    postToServer(getJSON(current, device, SUBACO_CHARGE_ONGOING), "append");
    WiFi.disconnect();
  }
  delay(100);
}

String getJSON(float current, subacoDevice device, subacoChargeState state) {
  String json;
  StaticJsonBuffer<500> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["token"] = SUBACO_API_TOKEN;
  root["current"] = current * 1000;
  root["state"] = (int)state;

  while(time(NULL) == 0) { delay(10); }
  time_t now = time(NULL);
  log_d("Time: %s", ctime(&now));
  root["ts"] = now;

  JsonObject& toDevice = root.createNestedObject("toDevice");
  toDevice["name"] = device.product;
  toDevice["manufacturer"] = device.manufacturer;
  toDevice["serialNum"] = device.serialNum;

  root.printTo(json);
  return json;
}

void postToServer(String json, String method) {
  WiFiClient client;
  if (!client.connect(SUBACO_HOST, SUBACO_PORT)) { log_e("Connection failed!"); }
  else {
    log_i("http connected");
    String req = "POST " + String(SUBACO_PATH) + method + " HTTP/1.1\r\n"\
               + "Content-Type: application/json\r\n"\
               + "Host: " + String(SUBACO_HOST) + "\r\n"\
               + "Content-Length: " + String(json.length()) + "\r\n\r\n"\
               + json + "\0";
    client.print(req);
    client.flush();
    while (!client.available()){
      Serial.print(".");
      delay(50);
     }
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

void connectToWiFi() {
  WiFi.disconnect();
  WiFi.begin("dummy","pass"); //Can be a nonexistent network
  for(int i = 0; i < 10; ++i) { // Timeout in 5 seconds
    if(wifiMulti.run() == WL_CONNECTED) {
      log_i("WiFi connected! IP address: ");
      Serial.println(WiFi.localIP());
      return;
    }
    Serial.print(".");
    delay(500);
  }
}

void getStrDescriptors(UsbDevice *pdev) {
  uint8_t addr = pdev->address.devAddress;
  while (Usb.getUsbTaskState() < USB_STATE_CONFIGURING) { Usb.Task(); } // state must be configuring or higher
  USB_DEVICE_DESCRIPTOR buf;
  if (Usb.getDevDescr(addr, 0, DEV_DESCR_LEN, (uint8_t *)&buf) != NULL) { return; }
  if (buf.iManufacturer > 0) {
    String manufacturer = getstrdescr(addr, buf.iManufacturer);
    if(manufacturer != "") { device.manufacturer = manufacturer; }
  }
  if (buf.iProduct > 0) {
    String product = getstrdescr(addr, buf.iProduct);
    if(product != "") { device.product = product; }
  }
  if (buf.iSerialNumber > 0) {
    String serialNum = getstrdescr(addr, buf.iSerialNumber);
    if(serialNum != "") { device.serialNum = serialNum; }
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
  uint16_t langid = (buf[3] << 8) | buf[2];
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
