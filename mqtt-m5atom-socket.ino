#include "M5Atom.h"
#include "AtomSocket.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

#include "config.h"

ATOMSOCKET ATOM;
HardwareSerial AtomSerial(2);

bool relay_status = false;

WiFiClient wifi_client;
void mqtt_sub_callback(char* topic, byte* payload, unsigned int length);
PubSubClient mqtt_client(mqtt_host, mqtt_port, mqtt_sub_callback, wifi_client);

#define RXD 22
#define RELAY 23

float last_v = 0.0f;
float last_a = 0.0f;
float last_w = 0.0f;
unsigned long last_t = 0;

void publish_status() {
  struct tm t;
  if (!getLocalTime(&t)) {
    reboot();
  }

  char msg[120];
  snprintf(msg, 120, "{\"voltage\":%.2f, \"current\":%.2f, \"power\":%.2f, \"relay_status\":%d,\"last_update_t\":\"%04d-%02d-%02dT%02d:%02d:%02d+09:00\"}",
           last_v,
           last_a,
           last_w,
           relay_status == true ? 1 : 0,
           1900 + t.tm_year, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  Serial.println(msg);
  mqtt_client.publish(mqtt_publish_topic, msg);

  last_t = millis();
}

void setup() {
  Serial.begin(115200);
  M5.begin(true, false, true);
  ATOM.Init(AtomSerial, RELAY, RXD);
  M5.dis.drawpix(0, 0xffffff);  // white

  // Wifi
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  WiFi.setSleep(false);
  int count = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    switch (count % 4) {
      case 0:
        Serial.println("|");
        M5.dis.drawpix(0, 0xffff00);  // yellow
        break;
      case 1:
        Serial.println("/");
        break;
      case 2:
        M5.dis.drawpix(0, 0x000000);  // black
        Serial.println("-");
        break;
      case 3:
        Serial.println("\\");
        break;
    }
    count ++;
    if (count >= 240) reboot(); // 240 / 4 = 60sec
  }
  Serial.println("WiFi connected!");
  delay(1000);

  // MQTT
  bool rv = false;
  if (mqtt_use_auth == true) {
    rv = mqtt_client.connect(mqtt_client_id, mqtt_username, mqtt_password);
  }
  else {
    rv = mqtt_client.connect(mqtt_client_id);
  }
  if (rv == false) {
    Serial.println("mqtt connecting failed...");
    reboot();
  }
  Serial.println("MQTT connected!");
  delay(1000);

  M5.dis.drawpix(0, 0x000088);  // blue

  mqtt_client.subscribe(mqtt_subscribe_topic);

  last_t = millis();


  // configTime
  configTime(9 * 3600, 0, "ntp.nict.jp");
  struct tm t;
  if (!getLocalTime(&t)) {
    Serial.println("getLocalTime() failed...");
    delay(1000);
    reboot();
  }
  Serial.println("configTime() success!");
  
  delay(1000);
}

void loop() {
  if (!mqtt_client.connected()) {
    Serial.println("MQTT disconnected...");
    reboot();
  }
  mqtt_client.loop();

  ATOM.SerialReadLoop();
  if (ATOM.SerialRead == 1)
  {
    last_v = ATOM.GetVol();
    last_a = ATOM.GetCurrent();
    last_w = ATOM.GetActivePower();
  }

  if (M5.Btn.wasPressed()) {
    relay_status = !relay_status;
  }

  if (relay_status) {
    M5.dis.drawpix(0, 0x00ff00); // green
    ATOM.SetPowerOn();
  } else {
    ATOM.SetPowerOff();
    M5.dis.drawpix(0, 0xff0000); // red
  }
  M5.update();

  if (millis() - last_t > 10 * 1000) {
    publish_status();
    last_t = millis();
  }
}

#define BUF_LEN 16
char buf[BUF_LEN];

void mqtt_sub_callback(char* topic, byte* payload, unsigned int length) {

  int len = BUF_LEN - 1 < length ? (BUF_LEN - 1) : length;
  memset(buf, 0, BUF_LEN);
  strncpy(buf, (const char*)payload, len);

  String cmd = String(buf);
  Serial.print("payload=");
  Serial.println(cmd);

  if (cmd == "on") {
    relay_status = true;
    publish_status();
  }
  else if (cmd == "off") {
    relay_status = false;
    publish_status();
  }
  else if (cmd == "toggle") {
    relay_status = !relay_status;
    publish_status();
  }
}

void reboot() {
  Serial.println("REBOOT!!!!!");
  for (int i = 0; i < 30; ++i) {
    M5.dis.drawpix(0, 0xffff00); // yellow
    delay(50);
    M5.dis.drawpix(0, 0x000000);
    delay(50);
  }

  ESP.restart();
}
