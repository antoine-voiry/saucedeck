// Spike: does WiFiManager 2.0.17 build and serve custom params on core 3.x / S3?
// Pass = AP portal shows the 2 custom fields, Save prints them; after joining home Wi-Fi,
// http://<ip>/param shows the same fields (startWebPortal in STA mode).
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

static WiFiManager wm;
static WiFiManagerParameter pType("t1", "Key 1 type (key/app/url/shortcut)", "key", 9);
static WiFiManagerParameter pValue("v1", "Key 1 value", "f13", 57);
static bool webPortal = false;

static void onSave() {
  Serial.printf("SAVE t1='%s' v1='%s'\n", pType.getValue(), pValue.getValue());
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  wm.addParameter(&pType);
  wm.addParameter(&pValue);
  wm.setSaveParamsCallback(onSave);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(20);
  bool ok = wm.autoConnect("SAUCEDECK-setup", "12345678");
  Serial.printf("autoConnect=%d (0 = AP portal running)\n", ok);
}

void loop() {
  wm.process();
  if (WiFi.status() == WL_CONNECTED && !webPortal) {
    wm.startWebPortal();
    webPortal = true;
    Serial.printf("JOINED ip=%s, open http://%s/param\n", WiFi.localIP().toString().c_str(),
                  WiFi.localIP().toString().c_str());
  }
}
