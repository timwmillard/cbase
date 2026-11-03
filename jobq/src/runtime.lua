-- Job-system runtime: JSON handling, canonical encoding, and handler
-- dispatch. Embedded into the binary (see CMakeLists.txt / runtime_lua.h)
-- and executed in each worker's lua_State, after the `jobq` module is
-- published and before the user's handlers script (-l PATH) loads, so that
-- script can contain nothing but jobq.register(kind, fn) calls.

local jobq = require("jobq")
local dkjson = require("dkjson")
local cjson = {
  encode = dkjson.encode,
  decode = function(str)
    local obj, _, err = dkjson.decode(str)
    if err then return nil, err end
    return obj
  end,
}

-- Canonical JSON: sorted keys, so equal args => equal bytes. Uniqueness is
-- byte-comparison; cjson.encode does NOT sort keys. Use this when building
-- unique_key values at the producer side too.
local function canonical_json(v)
  local t = type(v)
  if t == "table" then
    local n = #v
    if n > 0 then
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
_G.canonical_json = canonical_json

local handlers = {}
jobq.register = function(kind, fn) handlers[kind] = fn end

-- Entry point called from C for every claimed job.
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
