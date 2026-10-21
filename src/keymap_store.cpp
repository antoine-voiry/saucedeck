#include "keymap_store.h"
#include <Preferences.h>

static const char *kNs = "saucedeck";
static const char *kKey = "keymap";

bool keymapExists() {
  Preferences p;
  if (!p.begin(kNs, true)) return false;
  bool e = p.isKey(kKey);
  p.end();
  return e;
}

bool keymapLoad(KeyMap &out) {
  Preferences p;
  if (!p.begin(kNs, true)) return false;
  uint8_t buf[kKeyMapBlobMax];
  size_t n = p.getBytes(kKey, buf, sizeof buf);
  p.end();
  return n > 0 && deserializeKeyMap(buf, n, out);
}

bool keymapSave(const KeyMap &m) {
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  if (n == 0) return false;
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  bool ok = p.putBytes(kKey, buf, n) == n;
  p.end();
  return ok;
}
