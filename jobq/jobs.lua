-- jobs.lua: job handlers, and nothing else. Dispatch, JSON decoding, and
-- canonical encoding are part of the jobq daemon itself (src/runtime_lua.h);
-- point -l at any file that just calls jobq.register(kind, fn) to swap in a
-- different set of handlers.
--
-- Available from C / the runtime:
--   jobq.register(kind, fn)  -- fn(args, job) where job = {id, kind}
--                             -- error("...") fails the job -> retry with backoff
--                             -- return normally to complete it
--   jobq.enqueue(kind, args_json [, {unique_key=, run_at=, priority=, max_attempts=}])
--   jobq.heartbeat(job_id)    -- for handlers that run longer than the rescue timeout
--   canonical_json(value)     -- sorted-key JSON encode, for building unique_key values

local jobq = require("jobq")

jobq.register("demo.print", function(args, job)
  print(("job %d: hello, %s"):format(job.id, args.name or "world"))
end)

jobq.register("email.send", function(args, job)
  -- Blocking I/O is fine here: it only occupies this worker thread.
  -- e.g. with luasocket/lua-http:
  --   local ok, err = smtp_send(args.to, args.subject, args.body)
  --   if not ok then error("smtp: " .. err) end

  -- Follow-up job, deduped for an hour per recipient:
  -- jobq.enqueue("email.log", canonical_json({to = args.to}),
  --              { unique_key = "email.log:" .. args.to,
  --                run_at = os.time() + 3600 })
end)

jobq.register("report.generate", function(args, job)
  -- Long-running example: heartbeat inside the loop so the rescue
  -- sweeper (RESCUE_TIMEOUT in main.c) doesn't reclaim us.
  for chunk = 1, (args.chunks or 1) do
    -- ... produce chunk ...
    jobq.heartbeat(job.id)
  end
end)
