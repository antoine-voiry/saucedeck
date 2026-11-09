-- SAUCEDECK text hygiene: UTF-8 in, printable ASCII (<= 48 chars) out.
-- Pure Lua 5.4, no Hammerspoon dependencies (host-tested in test/lua).
local M = {}

local MAX = 48

local MAP = {
  ["\u{2019}"] = "'", ["\u{2018}"] = "'",
  ["\u{201C}"] = '"', ["\u{201D}"] = '"',
  ["\u{2013}"] = "-", ["\u{2014}"] = "-",
  ["\u{2026}"] = "...", ["\u{00B0}"] = "",
}

-- Latin-1 supplement letters U+00C0..U+00FF, folded by family.
local function fold(from, to, repl)
  for cp = from, to do MAP[utf8.char(cp)] = repl end
end
fold(0xC0, 0xC5, "A"); MAP["\u{C6}"] = "AE"; MAP["\u{C7}"] = "C"
fold(0xC8, 0xCB, "E"); fold(0xCC, 0xCF, "I")
MAP["\u{D0}"] = "D"; MAP["\u{D1}"] = "N"; fold(0xD2, 0xD6, "O"); MAP["\u{D8}"] = "O"
fold(0xD9, 0xDC, "U"); MAP["\u{DD}"] = "Y"; MAP["\u{DE}"] = "Th"; MAP["\u{DF}"] = "ss"
fold(0xE0, 0xE5, "a"); MAP["\u{E6}"] = "ae"; MAP["\u{E7}"] = "c"
fold(0xE8, 0xEB, "e"); fold(0xEC, 0xEF, "i")
MAP["\u{F0}"] = "d"; MAP["\u{F1}"] = "n"; fold(0xF2, 0xF6, "o"); MAP["\u{F8}"] = "o"
fold(0xF9, 0xFC, "u"); MAP["\u{FD}"] = "y"; MAP["\u{FE}"] = "th"; MAP["\u{FF}"] = "y"

local function trim(s) return (s:gsub("^ +", ""):gsub(" +$", "")) end

function M.clean(s)
  if type(s) ~= "string" then return "" end
  s = s:gsub("[\xC2-\xF4][\x80-\xBF]*", function(seq) return MAP[seq] or "" end)
  s = s:gsub("[\x00-\x1F]", " ")
  s = s:gsub("[\x7F-\xFF]", "")
  s = s:gsub(" +", " ")
  return trim(trim(s):sub(1, MAX))
end

return M
