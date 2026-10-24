-- SAUCEDECK key actions for Hammerspoon. Matches presets/mac-daily.json.
-- Load from ~/.hammerspoon/init.lua:  dofile(os.getenv("HOME") .. "/saucedeck/tools/mac/hammerspoon.lua")
-- macOS has no keycodes for F21-F24, so keys 10-12 send ctrl+alt+cmd+F13..F15.
-- Key 9 (cmd+h) is a plain keystroke and needs no binding here.

local function app(bundleID)
  return function() hs.application.launchOrFocusByBundleID(bundleID) end
end

local function url(u)
  return function() hs.urlevent.openURLWithBundle(u, "com.google.Chrome") end
end

local hyper = { "ctrl", "alt", "cmd" }

local keys = {
  { {},    "f13", app("com.apple.screenshot.launcher") },                         -- 1 Screenshot
  { {},    "f14", app("com.google.Chrome") },                                     -- 2 Chrome
  { {},    "f15", url("https://mail.google.com") },                               -- 3 Gmail
  { {},    "f16", app("ru.keepcoder.Telegram") },                                 -- 4 Telegram
  { {},    "f17", app("com.autodesk.dls.streamer.scriptapp.Autodesk-Fusion") },   -- 5 Fusion
  { {},    "f18", app("com.apple.systempreferences") },                           -- 6 System Settings
  { {},    "f19", app("com.apple.helpviewer") },                                  -- 7 Help (Tips)
  { {},    "f20", url("https://www.youtube.com") },                               -- 8 YouTube
  { hyper, "f13", app("com.apple.MobileSMS") },                                   -- 10 Messages
  { hyper, "f14", app("com.apple.weather") },                                     -- 11 Weather
  { hyper, "f15", app("com.bambulab.bambu-studio") },                             -- 12 Bambu Studio
}

for _, k in ipairs(keys) do
  hs.hotkey.bind(k[1], k[2], k[3])
end

hs.alert.show("SAUCEDECK keys loaded")
