#pragma once
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

// NVS as Preferences sees it: namespace -> key -> bytes.
struct SimNvs {
  std::map<std::string, std::map<std::string, std::vector<uint8_t>>> ns;
  bool hasNamespace(const std::string &n) const { return ns.count(n) != 0; }
};
