-- Frontend witness for the DuckStation package: run the core inside Chimera
-- for a fixed number of frames with nothing pressed, then dump the start of
-- Main RAM. The driver compares that dump byte for byte against the native
-- reference (run-native --dump-domain).
--
-- A slice rather than the whole domain: pulling two megabytes through Lua a
-- byte at a time takes longer than the emulation does. The first megabyte is
-- where the kernel and the running program live and where a divergence would
-- show first; the domain's full size travels in the metadata, so the driver
-- still sees whether the frontend built the machine it expected.
--
-- Job description comes from the file named by the MINIHAWK_JOB env var:
--   frames=<how many frames to advance>
--   out=<path to write the RAM dump (binary)>
--   meta=<path to write result metadata (text)>
--   shot=<optional path to write a screenshot>
--   bytes=<optional slice length, default 1MB>
--   system=<optional system id the project must be, default PS1>

local DOMAIN = "Main RAM"

local function writeAll(path, data)
	local f = assert(io.open(path, "wb"))
	f:write(data)
	f:close()
end

local meta = {}
local function finish(status, detail)
	local lines = {
		"status=" .. status,
		"detail=" .. (detail or ""),
		"frames=" .. (meta.frames or 0),
		"lag=" .. (meta.lag or 0),
		"ramsize=" .. (meta.ramsize or 0),
	}
	if meta.metaPath then
		writeAll(meta.metaPath, table.concat(lines, "\n") .. "\n")
	end
	client.exit()
end

local jobPath = os.getenv("MINIHAWK_JOB")
if jobPath == nil then
	error("MINIHAWK_JOB env var not set")
end
local job = {}
for line in io.lines(jobPath) do
	local k, v = line:match("^([^=]+)=(.*)$")
	if k then job[k] = v end
end
meta.metaPath = job.meta

local sysid = emu.getsystemid()
-- which machine of the package this project should have built (system=<id>)
local wantSystem = job.system
if wantSystem == nil or wantSystem == "" then wantSystem = "PS1" end
if sysid ~= wantSystem then
	finish("ERROR", "wrong system id: " .. tostring(sysid))
end
if emu.getcorename() ~= "DuckStation" then
	finish("ERROR", "wrong core: " .. tostring(emu.getcorename()))
end

pcall(function() client.speedmode(6400) end)
pcall(function() client.invisibleemulation(true) end)

-- Up to a frame NUMBER, not for a count of frames. The frontend starts playing
-- the project as soon as it is open, and whether this script gets its first
-- turn before frame 0 or after frame 2 is the host's scheduling: under Mono it
-- was the first, on Windows the second. Counting advances from wherever that
-- was asks for frames past the end of the movie, where a headless run pauses
-- for good - and would compare the wrong frame if it did not.
local frames = tonumber(job.frames) or 120
if emu.framecount() > frames then
	finish("ERROR", "the script started at frame " .. emu.framecount() .. ", past frame " .. frames)
	return
end
while emu.framecount() < frames do
	emu.frameadvance()
end

meta.frames = emu.framecount()
meta.lag = emu.lagcount()
pcall(function()
	memory.usememorydomain(DOMAIN)
	meta.ramsize = memory.getcurrentmemorydomainsize()
end)

if job.shot ~= nil and job.shot ~= "" then
	client.screenshot(job.shot)
end

local slice = tonumber(job.bytes) or (1024 * 1024)
if slice > (meta.ramsize or 0) then slice = meta.ramsize or 0 end
local ram = memory.read_bytes_as_array(0, slice, DOMAIN)
local chunks = {}
for i = 1, #ram do
	chunks[i] = string.char(ram[i])
end
writeAll(job.out, table.concat(chunks))

finish("OK", "")
