-- jobs.lua: job handlers. Loaded once per worker thread into that
-- worker's own lua_State (no cross-thread sharing, no locks needed).
--
-- Available from C:
--   jobq.enqueue(kind, args_json [, {unique_key=, run_at=, priority=, max_attempts=}])
--   jobq.heartbeat(job_id)   -- for handlers that run longer than the rescue timeout

local ok, cjson = pcall(require, "cjson.safe")
if not ok then error("lua-cjson required (luarocks install lua-cjson)") end

----------------------------------------------------------------------
-- Canonical JSON: sorted keys, so equal args => equal bytes.
-- Uniqueness is byte-comparison; cjson.encode does NOT sort keys.
-- Use this when building unique_key values at the producer side too.
----------------------------------------------------------------------
local function canonical_json(v)
  local t = type(v)
  if t == "table" then
    -- array?
    local n = #v
    local is_array = n > 0
    if is_array then
      local parts = {}
      for i = 1, n do parts[i] = canonical_json(v[i]) end
      return "[" .. table.concat(parts, ",") .. "]"
    end
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    local parts = {}
    for i, k in ipairs(keys) do
      parts[i] = cjson.encode(tostring(k)) .. ":" .. canonical_json(v[k])
    end
    return "{" .. table.concat(parts, ",") .. "}"
  end
  return cjson.encode(v)
end

----------------------------------------------------------------------
-- Handlers. Signature: handler(args_table, job) where job = {id, kind}.
-- Raise an error (error("...")) to fail the job -> retry with backoff.
-- Return normally to complete it.
----------------------------------------------------------------------
local handlers = {}

handlers["demo.print"] = function(args, job)
  print(("job %d: hello, %s"):format(job.id, args.name or "world"))
end

handlers["email.send"] = function(args, job)
  -- Blocking I/O is fine here: it only occupies this worker thread.
  -- e.g. with luasocket/lua-http:
  --   local ok, err = smtp_send(args.to, args.subject, args.body)
  --   if not ok then error("smtp: " .. err) end

  -- Follow-up job, deduped for an hour per recipient:
  -- jobq.enqueue("email.log", canonical_json({to = args.to}),
  --              { unique_key = "email.log:" .. args.to,
  --                run_at = os.time() + 3600 })
end

handlers["report.generate"] = function(args, job)
  -- Long-running example: heartbeat inside the loop so the rescue
  -- sweeper (RESCUE_TIMEOUT in main.c) doesn't reclaim us.
  for chunk = 1, (args.chunks or 1) do
    -- ... produce chunk ...
    jobq.heartbeat(job.id)
  end
end

----------------------------------------------------------------------
-- Entry point called from C for every claimed job.
----------------------------------------------------------------------
function dispatch(id, kind, args_json)
  local h = handlers[kind]
  if not h then
    error(("no handler for kind %q"):format(kind))
  end
  local args, err = cjson.decode(args_json)
  if args == nil then
    error(("bad args JSON for job %d: %s"):format(id, tostring(err)))
  end
  return h(args, { id = id, kind = kind })
end

-- exported for producers that build unique keys / args in Lua
_G.canonical_json = canonical_json
