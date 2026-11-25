-- Last Town Standing example game mode.
-- Any player whose finished Town Centers (unit type 109) are all destroyed is
-- defeated at once; the last surviving player with a Town Center wins.

local TOWN_CENTER_ID = 109

local active = false
local had_tc = {}
local defeated = {}
local ended = false

local function count_built_tcs(player)
    local n = 0
    for _, u in ipairs(player:UnitsOfType(TOWN_CENTER_ID)) do
        if u.Built then
            n = n + 1
        end
    end
    return n
end

Game.OnStart(function()
    active = (Game.Mode() == "last_town_standing")
    had_tc = {}
    defeated = {}
    ended = false
    if active then
        UI.Message("Last Town Standing: lose all of your Town Centers and you are defeated!")
    end
end)

Game.OnSecond(function()
    if not active or ended then return end

    local participants = 0
    local alive = 0
    local last_alive = nil

    for _, p in ipairs(Game.Players()) do
        local tcs = count_built_tcs(p)
        if tcs > 0 then
            had_tc[p.Id] = true
        elseif had_tc[p.Id] and not defeated[p.Id] then
            defeated[p.Id] = true
            p:Defeat()
        end

        if had_tc[p.Id] then
            participants = participants + 1
            if not defeated[p.Id] then
                alive = alive + 1
                last_alive = p
            end
        end
    end

    if participants >= 2 and alive == 1 and last_alive then
        ended = true
        last_alive:Win()
    end
end)
