-- Host test runner for the Hammerspoon helper's pure modules (Lua 5.4).
-- Usage (repo root): /opt/homebrew/opt/lua@5.4/bin/lua test/lua/run.lua
package.path = "tools/mac/?.lua;test/lua/?.lua;" .. package.path

local passed, failed = 0, 0
local T = {}

function T.eq(got, want, msg)
  if got ~= want then
    error((msg or "eq") .. ": got " .. tostring(got) .. ", want " .. tostring(want), 2)
  end
end
function T.ok(v, msg) if not v then error(msg or "expected truthy", 2) end end
function T.hex(s) return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end)) end

local suites = { "test_proto", "test_text", "test_cards", "test_core", "test_glue" }
for _, name in ipairs(arg[1] and { arg[1] } or suites) do
  local suite = require(name)
  local names = {}
  for k in pairs(suite) do names[#names + 1] = k end
  table.sort(names)
  for _, k in ipairs(names) do
    local ok, err = pcall(suite[k], T)
    if ok then passed = passed + 1 else
      failed = failed + 1
      print("FAIL " .. name .. "." .. k .. ": " .. tostring(err))
    end
  end
end
print(passed .. " passed, " .. failed .. " failed")
os.exit(failed == 0 and 0 or 1)
