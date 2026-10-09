local api = import("wallpaper_engine.api")

local M = {}

local function tag_names(item)
    local out = {}
    for _, t in ipairs(item.tags or {}) do
        local name = type(t) == "table" and t.tag or t
        if name and name ~= "" then
            table.insert(out, name)
        end
    end
    return out
end

local function wp_type(item)
    for _, name in ipairs(tag_names(item)) do
        if api.TYPE_WP[name] then
            return api.TYPE_WP[name]
        end
    end
    return "scene"
end

local function preview_url(item)
    if item.preview_url and item.preview_url ~= "" then
        return item.preview_url
    end
    local previews = item.previews or {}
    local first = previews[1] or {}
    return first.url or ""
end

-- `authors` maps a creator's steamid64 to a persona name. A creator missing
-- from it keeps the empty author the Workshop grid has always shown.
function M.search_item(item, authors)
    local creator = tostring(item.creator or "")
    return {
        id = tostring(item.publishedfileid or ""),
        title = item.title or "",
        preview_url = preview_url(item),
        author = authors and authors[creator] or "",
        wp_type = wp_type(item),
    }
end

local function summary(item)
    local parts = {}
    local subs = item.subscriptions or item.lifetime_subscriptions
    if subs then
        table.insert(parts, tostring(subs) .. " subscribers")
    end
    if item.favorited then
        table.insert(parts, tostring(item.favorited) .. " favorites")
    end
    local vote = item.vote_data or {}
    if vote.votes_up then
        table.insert(parts, tostring(vote.votes_up) .. " up")
    end
    return table.concat(parts, "  \194\183  ")
end

function M.details(item)
    local desc = item.description or item.short_description or item.file_description or ""
    local head = summary(item)
    if head ~= "" then
        desc = head .. "\n\n" .. desc
    end
    return {
        description = desc,
        size = tostring(item.file_size or ""),
        tags = tag_names(item),
        web_url = "https://steamcommunity.com/sharedfiles/filedetails/?id=" ..
            tostring(item.publishedfileid or ""),
    }
end

return M
