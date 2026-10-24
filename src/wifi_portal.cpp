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

static WiFiManager wm;
static WifiState state = WifiState::Off;
static char apPass[9];
static char typeBuf[kKeyCount][10];
static char valueBuf[kKeyCount][kKeyValueMax + 1];
static char ids[kKeyCount * 2][4];
static char labels[kKeyCount * 2][24];
static WiFiManagerParameter *params[kKeyCount * 2];
static bool paramsAdded = false;
static bool savedPending = false;
static bool webPortal = false;

// Preset loader (presets/*.json): browser-side only, it just fills the 24 inputs below.
// The deck still re-validates every field on Save and waits for the knob 1 confirm.
static const char kPresetHtml[] =
    "<p><b>Load preset</b> (.json, 12 keys), check the fields, then Save.</p>"
    "<input type='file' id='pf' accept='.json,application/json'><div id='ps'></div>"
    "<script>document.getElementById('pf').onchange=function(e){"
    "var f=e.target.files[0],s=document.getElementById('ps');if(!f)return;"
    "f.text().then(function(t){var k=JSON.parse(t).keys;"
    "if(!Array.isArray(k)||k.length!=12)throw Error('need 12 keys');"
    "k.forEach(function(x,i){if(['key','app','url','shortcut'].indexOf(x.type)<0)"
    "throw Error('key '+(i+1)+': bad type');"
    "if(typeof x.value!='string'||!x.value.length||x.value.length>56)"
    "throw Error('key '+(i+1)+': value 1-56 chars');});"
    "k.forEach(function(x,i){document.getElementById('t'+(i+1)).value=x.type;"
    "document.getElementById('v'+(i+1)).value=x.value;});"
    "s.textContent='Loaded '+f.name+'. Press Save, then knob 1 on the deck.';"
    "}).catch(function(err){s.textContent='Bad preset: '+err.message;});};</script>";
static WiFiManagerParameter presetParam(kPresetHtml);  // custom HTML: no id, no value

static void onSave() { savedPending = true; }

static void buildParams(const KeyMap &m) {
  if (!paramsAdded) wm.addParameter(&presetParam);
  for (uint8_t i = 0; i < kKeyCount; i++) {
    snprintf(typeBuf[i], sizeof typeBuf[i], "%s", keyTypeName(m.entry(i).type));
    snprintf(valueBuf[i], sizeof valueBuf[i], "%s", m.entry(i).value);
    if (paramsAdded) {
      params[2 * i]->setValue(typeBuf[i], 9);
      params[2 * i + 1]->setValue(valueBuf[i], kKeyValueMax);
      continue;
    }
    snprintf(ids[2 * i], sizeof ids[0], "t%u", static_cast<unsigned>(i + 1));
    snprintf(ids[2 * i + 1], sizeof ids[0], "v%u", static_cast<unsigned>(i + 1));
    snprintf(labels[2 * i], sizeof labels[0], "Key %u type", static_cast<unsigned>(i + 1));
    snprintf(labels[2 * i + 1], sizeof labels[0], "Key %u value", static_cast<unsigned>(i + 1));
    params[2 * i] = new WiFiManagerParameter(ids[2 * i], labels[2 * i], typeBuf[i], 9);
    params[2 * i + 1] = new WiFiManagerParameter(ids[2 * i + 1], labels[2 * i + 1], valueBuf[i], kKeyValueMax);
    wm.addParameter(params[2 * i]);
    wm.addParameter(params[2 * i + 1]);
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
  if (webPortal) wm.stopWebPortal();
  wm.stopConfigPortal();
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
    if (m.setFromStrings(i, params[2 * i]->getValue(), params[2 * i + 1]->getValue()) != KeyMapError::None) {
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
