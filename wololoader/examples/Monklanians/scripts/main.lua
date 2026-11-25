-- Monklanians example script (Wololoader Lua API; see MODDING.md).
-- Mod scripts run in every game, so everything here first checks that the local player
-- plays the Monklanians: MOD.civs lists this mod's civ ids, Player.Civ is a player's civ.
print("loaded " .. MOD.name .. " v" .. MOD.version)

local function is_monklanian(p)
    if not p then return false end
    local civ = p.Civ
    for _, c in ipairs(MOD.civs or {}) do
        if c == civ then return true end
    end
    return false
end

local monkCounter = nil   -- "Monks: N" line, only for Monklanian players

Game.OnStart(function()
    local me = Game.LocalPlayer
    if not is_monklanian(me) then return end
    UI.Chat("Wololo!", { From = "Monklanians", Color = 2 })
    monkCounter = UI:CreateCounter("Monks: 0")
end)

-- How many monks the player has, updated every second.
Game.OnSecond(function()
    local me = Game.LocalPlayer
    if monkCounter and me then monkCounter:SetText("Monks: " .. #me:Units("Monk")) end
end)

-- Celebrate new monks.
Game.OnUnitCreated(function(unit)
    if unit.ClassName == "Monk" and unit.Owner.IsLocal and is_monklanian(unit.Owner) then
        UI.Chat("A new monk joins the order.", { From = "Abbey", Color = 2 })
    end
end)

-- The Abbey (unit 1400, the first mod building slot) is finished.
Game.OnBuildingDone(function(b)
    if b.Type == 1400 and b.Owner == Game.LocalPlayer then
        UI.Chat("The Abbey is finished: train monks and Abbots there.", { From = "Abbey", Color = 2 })
    end
end)

-- The Abbey's "Bless" button (unique_building commands, "lua": "Bless" in the civ JSON).
-- Only Monklanians have the Abbey, so this needs no civ check.
Game.OnCommand("Bless", function(abbey)
    local me = Game.LocalPlayer
    if me.Gold < 50 then
        UI.Chat("Not enough gold to bless (50).", { From = "Abbey", Color = 2 })
        return
    end
    me:AddGold(-50)
    me:CreateUnit(125, (abbey.X or 0) + 3, (abbey.Y or 0) + 3)
    Sound.Play("TownBell")
end)
