-- Fake `hs` table for smoke-testing tools/mac/saucedeck_helper.lua on the host. Records calls.
return function(opts)
  opts = opts or {}
  local hs = { ports = opts.ports or {}, opened = {}, sent = {}, closed = 0, timers = {}, alerts = {}, tasks = setmetatable({}, { __mode = "v" }), taskCount = 0 }  -- tasks weak: a GC'd task never calls back, like Hammerspoon

  local function device(vol, muted)
    local d = { vol = vol, muted = muted }
    function d:outputVolume() return self.vol end
    function d:outputMuted() return self.muted end
    function d:inputVolume() return self.vol end
    function d:inputMuted() return self.muted end
    function d:setInputVolume(v) self.vol = v; return true end
    function d:setInputMuted(m) self.muted = m; return true end
    return d
  end
  hs.outDev = device(30, false)
  hs.inDev = device(50, false)
  hs.audiodevice = {
    defaultOutputDevice = function() return hs.outDev end,
    defaultInputDevice = function() return hs.inDev end,
  }

  hs.serial = {
    availablePortNames = function() return hs.ports end,
    newFromName = function(name)
      local p = { name = name }
      function p:baudRate(b) self.baud = b; return self end
      function p:callback(fn) hs.serialCallback = fn; return self end
      function p:open() hs.opened[#hs.opened + 1] = { name = name, baud = self.baud }; return self end
      function p:close() hs.closed = hs.closed + 1; return self end
      function p:sendData(d) hs.sent[#hs.sent + 1] = d; return self end
      return p
    end,
  }

  hs.timer = {
    doEvery = function(sec, fn)
      local t = { sec = sec, fn = fn, stopped = false }
      function t:stop() self.stopped = true; return self end
      hs.timers[#hs.timers + 1] = t
      return t
    end,
    secondsSinceEpoch = function() return 1793455200 end,
    absoluteTime = function() return hs.nowNs or 1e9 end,
  }
  hs.alert = { show = function(s) hs.alerts[#hs.alerts + 1] = s end }
  hs.task = { new = function(path, cb, args)
    local t = { path = path, cb = cb, args = args }
    function t:start() return self end
    function t:terminate() return self end
    hs.taskCount = hs.taskCount + 1
    hs.tasks[hs.taskCount] = t
    return t
  end }
  hs.http = { asyncGet = function() end }
  hs.spotify = { isPlaying = function() return false end, getCurrentArtist = function() end, getCurrentTrack = function() end }
  hs.itunes = { isPlaying = function() return false end, getCurrentArtist = function() end, getCurrentTrack = function() end }
  hs.host = {
    cpuUsage = function() return { overall = { active = 12.3 } } end,
    vmStat = function() return {} end,
  }
  hs.battery = { percentage = function() return nil end, isCharging = function() return false end }

  package.preload["hs.ipc"] = function() return {} end
  return hs
end
