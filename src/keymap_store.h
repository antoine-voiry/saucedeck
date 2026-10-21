#pragma once
#include "keymap.h"

// Key map persistence in NVS. Format + validation: deck_core/keymap.h.
bool keymapExists();
bool keymapLoad(KeyMap &out);
bool keymapSave(const KeyMap &m);
