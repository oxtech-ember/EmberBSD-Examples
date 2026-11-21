-- Export a published note from a real Spark worktree with a local demo
-- assignment. Only the two server responses used to seed the cache are fixtures.
local Worktree = require("worktree")
local fs = require("asenta.fs")
local json = require("json")

local cache, note, mode = arg[1], arg[2], arg[3]
assert(cache and note and (mode == "seed" or mode == "offline"),
    "usage: agent-lua demo-worktree.lua CACHE NOTE seed|offline")
assert(not os.getenv("OXTORG_WORKTREE") or os.getenv("OXTORG_WORKTREE") == "",
    "use a separate demo cache with OXTORG_WORKTREE unset")

local name = "notebook/connector.md"
local identity = {organization = {orgId = "workshop-demo"}, user = {userId = "reader"}}
local config = {worktreeRoot = cache, server = {baseUrl = "https://workshop-demo.invalid"}}
local client = {config = config}
if mode == "seed" then
    local handle = assert(io.open(note, "rb"))
    local text = assert(handle:read(1048577))
    assert(handle:close())
    assert(#text <= 1048576, "guide exceeds Spark source limit")
    local binding = {workspaceId = "workshop-demo", worktreeId = "bench",
        revision = fs.sha256(text), name = "Workbench"}
    function client:request(method, path, _, _, options)
        assert(method == "GET")
        if path == "/api/v1/me/worktree" then return binding, 200 end
        assert(path == "/api/v1/me/worktree/snapshot?revision=" .. binding.revision)
        options.consume(json.encode({workspaceId = binding.workspaceId,
            worktreeId = binding.worktreeId, revision = binding.revision,
            files = json.array({{name = name, text = text}})}))
        return {}, 200
    end
else
    function client:request()
        error({message = "offline demo"})
    end
end

local manager = Worktree.new(config, client, identity)
local view = assert(manager:enter("connector-inspection", "reader"))
local content = view:show(view.revision, name)
manager:close()
assert(io.stderr:write(name, " @ ", view.revision, "\n"))
assert(io.stdout:write(content))
