#pragma once
#include <functional>
#include <string>
#include <vector>

// tzapu WiFiManager 2.0.17 subset used by wifi_portal.cpp. State in board().wifi.
// autoConnect: saved credentials work (wifi.joinOk) -> true; else the AP portal comes up if
// wifi.portalOk. Plan 3 adds form submission (submitForm) and link drops.
class WiFiManagerParameter {
 public:
  explicit WiFiManagerParameter(const char *custom);
  WiFiManagerParameter(const char *id, const char *label, const char *defaultValue, int length);
  const char *getID() const { return id_; }
  const char *getLabel() const { return label_; }
  const char *getValue() const { return value_.c_str(); }
  int getValueLength() const { return length_; }
  const char *getCustomHTML() const { return custom_; }
  void setValue(const char *v, int length);

 private:
  const char *id_ = nullptr;
  const char *label_ = nullptr;
  const char *custom_ = nullptr;
  std::string value_;
  int length_ = 0;
};

class WiFiManager {
 public:
  WiFiManager();
  bool addParameter(WiFiManagerParameter *p);
  void setSaveParamsCallback(std::function<void()> f) { saveCb_ = f; }
  void setConfigPortalBlocking(bool b) { blocking_ = b; }
  void setConnectTimeout(unsigned long s) { connectTimeout_ = s; }
  void setShowInfoUpdate(bool b) { showInfoUpdate_ = b; }
  bool autoConnect(const char *ssid, const char *pass);
  bool getConfigPortalActive();
  void stopWebPortal();
  bool stopConfigPortal();
  bool process();
  void startWebPortal();

  // sim side
  std::vector<WiFiManagerParameter *> params;
  std::function<void()> saveCb_;
  bool blocking_ = true, showInfoUpdate_ = true;
  unsigned long connectTimeout_ = 0;
};
