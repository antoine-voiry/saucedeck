// The ONLY file with Wi-Fi code (CI: tools/check_radio_scope.sh).
// WiFiManager joins home Wi-Fi (credentials saved by ESP-IDF in plain NVS: accepted risk) or
// runs the SAUCEDECK-setup hotspot with a fresh random password; its param form edits the key
// map. Saved forms are only staged: Deck applies them after a knob 1 press on the device.
#include "wifi_portal.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_random.h>
#include "icons.h"

static WiFiManager wm;
static WifiState state = WifiState::Off;
static char apPass[9];
static char typeBuf[kKeyCount][10];
static char valueBuf[kKeyCount][kKeyValueMax + 1];
static char labelBuf[kKeyCount][kKeyLabelMax + 1];
static constexpr size_t kIconNameMax = 10;
static char iconBuf[kKeyCount][kIconNameMax + 1];
static constexpr size_t kFields = 4;  // per key: type, value, label, icon
static char ids[kKeyCount * kFields][4];
static char labels[kKeyCount * kFields][24];
static WiFiManagerParameter *params[kKeyCount * kFields];
static bool paramsAdded = false;
static bool savedPending = false;
static bool webPortal = false;

// Preset loader (presets/*.json): browser-side only, it just fills the 48 inputs below.
// The deck still re-validates every field on Save and waits for the knob 1 confirm.
static const char kPresetHtml[] =
    "<p><b>Load preset</b> (.json, 12 keys), check the fields, then Save.</p>"
    "<input type='file' id='pf' accept='.json,application/json'><div id='ps'></div>"
    "<p><small>Icons: camera globe envelope plane cube gear question play eye-off chat weather "
    "printer mic music folder terminal lock moon bolt home</small></p>"
    "<script>document.getElementById('pf').onchange=function(e){"
    "var f=e.target.files[0],s=document.getElementById('ps');if(!f)return;"
    "f.text().then(function(t){var k=JSON.parse(t).keys;"
    "if(!Array.isArray(k)||k.length!=12)throw Error('need 12 keys');"
    "k.forEach(function(x,i){if(['key','app','url','shortcut'].indexOf(x.type)<0)"
    "throw Error('key '+(i+1)+': bad type');"
    "if(typeof x.value!='string'||!x.value.length||x.value.length>56)"
    "throw Error('key '+(i+1)+': value 1-56 chars');"
    "if(x.label!==undefined&&(typeof x.label!='string'||x.label.length>16))"
    "throw Error('key '+(i+1)+': label 0-16 chars');"
    "if(x.icon!==undefined&&(typeof x.icon!='string'||x.icon.length>10))"
    "throw Error('key '+(i+1)+': bad icon');});"
    "k.forEach(function(x,i){document.getElementById('t'+(i+1)).value=x.type;"
    "document.getElementById('v'+(i+1)).value=x.value;"
    "document.getElementById('l'+(i+1)).value=x.label||'';"
    "document.getElementById('i'+(i+1)).value=x.icon||'';});"
    "s.textContent='Loaded '+f.name+'. Press Save, then knob 1 on the deck.';"
    "}).catch(function(err){s.textContent='Bad preset: '+err.message;});};</script>";
static WiFiManagerParameter presetParam(kPresetHtml);  // custom HTML: no id, no value

static char resetsHtml[280] = "";  // filled by portalSetResets() at boot
static WiFiManagerParameter resetsParam(resetsHtml);

void portalSetResets(const char *summary) {
  snprintf(resetsHtml, sizeof resetsHtml, "<p><small>Last resets (newest first): %s</small></p>", summary);
}

static void onSave() { savedPending = true; }

static void buildParams(const KeyMap &m) {
  if (!paramsAdded) {
    wm.addParameter(&resetsParam);
    wm.addParameter(&presetParam);
  }
  for (uint8_t i = 0; i < kKeyCount; i++) {
    WiFiManagerParameter **p = &params[kFields * i];
    snprintf(typeBuf[i], sizeof typeBuf[i], "%s", keyTypeName(m.entry(i).type));
    snprintf(valueBuf[i], sizeof valueBuf[i], "%s", m.entry(i).value);
    snprintf(labelBuf[i], sizeof labelBuf[i], "%s", m.entry(i).label);
    snprintf(iconBuf[i], sizeof iconBuf[i], "%s", iconName(m.entry(i).icon));
    if (paramsAdded) {
      p[0]->setValue(typeBuf[i], 9);
      p[1]->setValue(valueBuf[i], kKeyValueMax);
      p[2]->setValue(labelBuf[i], kKeyLabelMax);
      p[3]->setValue(iconBuf[i], kIconNameMax);
      continue;
    }
    const unsigned n = i + 1;
    snprintf(ids[kFields * i], sizeof ids[0], "t%u", n);
    snprintf(ids[kFields * i + 1], sizeof ids[0], "v%u", n);
    snprintf(ids[kFields * i + 2], sizeof ids[0], "l%u", n);
    snprintf(labels[kFields * i], sizeof labels[0], "Key %u type", n);
    snprintf(labels[kFields * i + 1], sizeof labels[0], "Key %u value", n);
    snprintf(ids[kFields * i + 3], sizeof ids[0], "i%u", n);
    snprintf(labels[kFields * i + 2], sizeof labels[0], "Key %u label", n);
    snprintf(labels[kFields * i + 3], sizeof labels[0], "Key %u icon", n);
    p[0] = new WiFiManagerParameter(ids[kFields * i], labels[kFields * i], typeBuf[i], 9);
    p[1] = new WiFiManagerParameter(ids[kFields * i + 1], labels[kFields * i + 1], valueBuf[i], kKeyValueMax);
    p[2] = new WiFiManagerParameter(ids[kFields * i + 2], labels[kFields * i + 2], labelBuf[i], kKeyLabelMax);
    p[3] = new WiFiManagerParameter(ids[kFields * i + 3], labels[kFields * i + 3], iconBuf[i], kIconNameMax);
    for (size_t f = 0; f < kFields; f++) wm.addParameter(p[f]);
  }
  paramsAdded = true;
}

bool portalStart(const KeyMap &current) {
  snprintf(apPass, sizeof apPass, "%08lu", static_cast<unsigned long>(esp_random() % 100000000UL));
  buildParams(current);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_11dBm);  // limits TX peaks on the 500 mA USB budget (bench-tune)
  wm.setSaveParamsCallback(onSave);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(20);
  wm.setShowInfoUpdate(false);  // no OTA page
  savedPending = false;
  webPortal = false;
  state = wm.autoConnect("SAUCEDECK-setup", apPass) ? WifiState::Joined : WifiState::Portal;
  if (state == WifiState::Portal && !wm.getConfigPortalActive()) {  // neither joined nor AP up
    portalStop();
    return false;
  }
  return true;
}

void portalStop() {
  // One shutdown only. WiFiManager 2.0.17 stopWebPortal() returns early when neither portal
  // is active, else shuts down both (web portal + config AP). A second shutdown, or one with
  // no portal up, calls server->handleClient() on a null server (WiFiManager.cpp:970): that
  // crashed the deck on every Wi-Fi off while joined (core dump 2026-11-08).
  wm.stopWebPortal();
  MDNS.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  state = WifiState::Off;
  webPortal = false;
}

void portalPoll() {
  if (state == WifiState::Off) return;
  wm.process();
  bool up = WiFi.status() == WL_CONNECTED;
  if (state == WifiState::Joined && !up) state = WifiState::Starting;  // link lost: show it
  if (up && webPortal) state = WifiState::Joined;                      // reconnected
  if (up && !webPortal) {
    wm.startWebPortal();  // same param page on the home network (/param)
    MDNS.begin("saucedeck");
    webPortal = true;
    state = WifiState::Joined;
  }
}

WifiState portalState() { return state; }

bool portalTakeStaged(KeyMap &out, uint8_t &badKey) {
  if (!savedPending) return false;
  savedPending = false;
  KeyMap m;
  badKey = 0;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    WiFiManagerParameter **p = &params[kFields * i];
    if (m.setFromStrings(i, p[0]->getValue(), p[1]->getValue()) != KeyMapError::None ||
        m.setLabel(i, p[2]->getValue()) != KeyMapError::None ||
        m.setIcon(i, p[3]->getValue()) != KeyMapError::None) {
      badKey = i + 1;
      return true;
    }
  }
  out = m;
  return true;
}

const char *portalApPassword() { return apPass; }

void portalInfo(char *out, size_t n) {
  if (WiFi.status() == WL_CONNECTED)
    snprintf(out, n, "%s %ddBm", WiFi.localIP().toString().c_str(), static_cast<int>(WiFi.RSSI()));
  else
    snprintf(out, n, "status %d", static_cast<int>(WiFi.status()));
}
