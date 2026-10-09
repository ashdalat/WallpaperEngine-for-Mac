local M = {}

-- Steam hands persona names out through ISteamUser/GetPlayerSummaries, which
-- takes a Web API publisher key. waywallen ships no key and must not ask a user
-- for one, so the names come from the place that serves them without any: a
-- Community profile asked for its XML representation.
local PROFILE = "https://steamcommunity.com/profiles/"
local PROFILE_QUERY = "/?xml=1"
local PROFILE_HEADERS = {
    Accept = "application/xml,text/xml,*/*;q=0.8",
    ["Sec-Fetch-Dest"] = "empty",
    ["Sec-Fetch-Mode"] = "cors",
    ["Sec-Fetch-Site"] = "same-origin",
}
local REQUEST_TIMEOUT = 10

-- One name is one round trip to Steam and a Workshop page carries up to thirty
-- distinct creators, so resolving a cold page in full would hold the grid for
-- half a minute. Spend at most this much on the names a page still needs and
-- leave the rest to the next search: the cache is what fills the grid in, no
-- single call has to.
local BUDGET_MS = 4000

-- Steam being unreachable has to cost one search a couple of failed requests,
-- not one per creator on the page.
local FAILURE_LIMIT = 2

-- steamid64 -> persona name, or false once Steam has answered without one.
-- Persona names are public and account-independent, so this outlives a sign-out.
local names = {}

local ENTITIES = { amp = "&", lt = "<", gt = ">", quot = '"', apos = "'" }

local function decode_entities(text)
    return (text:gsub("&(#?%w+);", function(name)
        if name:sub(1, 1) ~= "#" then return ENTITIES[name] end
        local hex = name:match("^#[xX](%x+)$")
        local code = hex and tonumber(hex, 16) or tonumber(name:sub(2))
        if not code or code < 0 or code > 0x10FFFF then return nil end
        local ok, encoded = pcall(utf8.char, code)
        return ok and encoded or nil
    end))
end

-- The reply is XML, and the plugin runtime only offers an HTML parser. HTML
-- parsing turns the CDATA section Steam wraps every name in into a comment and
-- drops the name with it, so read the one element that is wanted out of a
-- document whose shape is fixed rather than pull in an XML parser for it.
-- `steamID` is the persona name and appears once; a group's name is `groupName`.
local function persona_name(xml)
    if xml:find("<error>", 1, true) then return nil end
    local raw = xml:match("<steamID>(.-)</steamID>")
    if not raw then return nil end
    local text = raw:match("^<!%[CDATA%[(.-)%]%]>$") or raw
    text = decode_entities(text):match("^%s*(.-)%s*$")
    if text == "" then return nil end
    return text
end

local function fetch(ctx, steamid)
    local response = ctx.http
        :get(PROFILE .. ctx.url.encode_component(steamid) .. PROFILE_QUERY)
        :headers(PROFILE_HEADERS)
        :timeout(REQUEST_TIMEOUT)
        :send()
    if not response:ok() then
        error("steam profile http " .. tostring(response:status()))
    end
    return persona_name(response:text() or "")
end

-- Persona names for the given steamid64s, in as far as they are already known
-- or can be looked up inside the budget. Ids left out of the answer keep the
-- empty author the Workshop grid showed before, which is also what a profile
-- that is gone, private or unreachable falls back to.
function M.names(ctx, ids)
    local resolved = {}
    local pending = {}
    local seen = {}
    for _, id in ipairs(ids) do
        local key = tostring(id or "")
        if key ~= "" and not seen[key] then
            seen[key] = true
            local cached = names[key]
            if cached then
                resolved[key] = cached
            elseif cached == nil then
                pending[#pending + 1] = key
            end
        end
    end
    if #pending == 0 then return resolved end

    local deadline = ctx.time.now() + BUDGET_MS
    local failures = 0
    for _, key in ipairs(pending) do
        if failures >= FAILURE_LIMIT or ctx.time.now() >= deadline then break end
        local ok, name = pcall(fetch, ctx, key)
        if not ok then
            -- Nothing was learned about this creator, so do not remember an
            -- answer: the next search asks again.
            failures = failures + 1
            ctx.log("wallpaper_engine: Steam profile " .. key .. " unavailable")
        else
            failures = 0
            names[key] = name or false
            if name then resolved[key] = name end
        end
    end
    return resolved
end

return M
