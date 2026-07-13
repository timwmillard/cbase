-- Type stub for the `jobq` module, for editor/LSP use only.
--
-- Never loaded at runtime: require("jobq") is satisfied straight out of
-- package.loaded (populated in C, src/pool.c:setup_lua) before any require
-- call touches disk. Keep this in sync with l_enqueue/l_heartbeat (pool.c)
-- and jobq.register (src/runtime.lua) by hand — nothing enforces it.

---@alias JobqOpts { unique_key?: string, run_at?: integer, priority?: integer, max_attempts?: integer }
---@alias JobqJob { id: integer, kind: string }

---@class jobq
local jobq = {}

--- Enqueue a follow-up job. Returns the new job id, or 0 if `opts.unique_key`
--- matched an existing undone job (skipped as a duplicate).
---@param kind string
---@param args_json string   JSON-encoded args; see canonical_json for building unique_key values
---@param opts? JobqOpts
---@return integer id
function jobq.enqueue(kind, args_json, opts) end

--- Call from a long-running handler so the rescue sweeper doesn't reclaim
--- the job out from under you (see RESCUE_TIMEOUT in src/main.c).
---@param id integer
function jobq.heartbeat(id) end

--- Register a handler for `kind`. fn(args, job): error("...") fails the job
--- (retried with backoff up to max_attempts); returning normally completes it.
---@param kind string
---@param fn fun(args: table, job: JobqJob)
function jobq.register(kind, fn) end

return jobq
