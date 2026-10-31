local C = require("saucedeck.cards")
local t = {}
t.meeting_first_line = function(T)
  T.eq(C.meeting("\n\xE2\x80\xA2 14:30 Standup \xC3\xA9quipe\n    notes\n"), "14:30 Standup equipe")
  T.eq(C.meeting("* 09:00 Review\n"), "09:00 Review")
  T.eq(C.meeting("\n  \n"), nil); T.eq(C.meeting(nil), nil)
end
t.now_playing = function(T)
  T.eq(C.nowPlaying("Daft Punk", "One More Time", true), "Daft Punk - One More Time")
  T.eq(C.nowPlaying("", "Podcast", true), "Podcast")
  T.eq(C.nowPlaying("A", "B", false), nil); T.eq(C.nowPlaying("A", nil, true), nil)
end
t.stats_rounded = function(T) T.eq(C.stats(12.4, 60.6), "CPU 12% RAM 61%") end
t.ram_percent = function(T)
  local vm = { pagesActive = 100, pagesWiredDown = 50, pagesUsedByVMCompressor = 50, pageSize = 16384, memSize = 16384 * 400 }
  T.eq(C.ramPercent(vm), 50)
  T.eq(C.ramPercent({ pageSize = 1 }), nil)
  vm.memSize = 0; T.eq(C.ramPercent(vm), nil)
end
t.battery = function(T)
  T.eq(C.battery(87, false), "Battery 87%"); T.eq(C.battery(86.6, true), "Battery 87% charging")
  T.eq(C.battery(nil, false), nil)
end
t.weather = function(T)
  T.eq(C.weather("Patchy rain nearby +13\xC2\xB0C\n"), "Patchy rain nearby +13C")
  T.eq(C.weather("<html>503</html>"), nil); T.eq(C.weather("Unknown location"), nil)
  T.eq(C.weather(""), nil); T.eq(C.weather(nil), nil)
end
t.badge = function(T)
  T.eq(C.badge('"StatusLabel"={ "label"="7" }'), 7)
  T.eq(C.badge('"StatusLabel"={ "label"="\xE2\x80\xA2" }'), 1)
  T.eq(C.badge('"StatusLabel"=[ NULL ]'), 0); T.eq(C.badge(""), 0); T.eq(C.badge(nil), 0)
end
t.unread = function(T)
  T.eq(C.unread({ { "Mail", 3 }, { "Telegram", 2 } }), "Mail 3 Telegram 2")
  T.eq(C.unread({ { "Mail", 0 }, { "Telegram", 5 } }), "Telegram 5")
  T.eq(C.unread({ { "Mail", 0 } }), nil)
end
t.all_outputs_fit = function(T) T.ok(#C.nowPlaying(string.rep("a", 40), string.rep("b", 40), true) <= 48) end
return t
