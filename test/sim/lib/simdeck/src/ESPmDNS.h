#pragma once
class MDNSResponder {
 public:
  bool begin(const char *hostName);
  void end();
};
extern MDNSResponder MDNS;
