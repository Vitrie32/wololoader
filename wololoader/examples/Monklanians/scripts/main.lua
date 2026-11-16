-- Monklanians example script (Wololoader Lua API; see MODDING.md).
print("loaded " .. MOD.name .. " v" .. MOD.version)

Game.OnStart(function()
    local me = Game.LocalPlayer
    UI.Message("Monklanians: Wololo!")
    Sound.Play("Objective")
    UI.Chat("The monks are ready.", { From = "Abbot Wololo", Color = 2 })
    print("start gold:", me.Gold, "age:", me.AgeName)

    Time.Wait(5)
    if UI.Ask("Bless your monks?") then
        Sound.Play("TownBell")
        UI.Chat("Then go, and convert them all! Take 100 gold and a new brother.", { From = "Abbot Wololo", Color = 2 })
        me:AddGold(100)
        -- A new monk (unit id 125) three tiles from the first building (the Town Center).
        local tc = me:Units("Building")[1]
        if tc then me:CreateUnit(125, tc.X + 3, tc.Y + 3) end
    end
end)

-- A counter line that shows how many monks the player has, updated every second.
local monkCounter = UI:CreateCounter("Monks: 0")
Game.OnSecond(function()
    local me = Game.LocalPlayer
    if me then monkCounter:SetText("Monks: " .. #me:Units("Monk")) end
end)

-- Announce when anyone reaches a new age, and celebrate new monks.
Game.OnAgeUp(function(player, age)
    UI.Chat("Player " .. player.Id .. " reached the " .. age .. " Age!", { From = "Abbot Wololo", Color = 2 })
end)
Game.OnUnitCreated(function(unit)
    if unit.ClassName == "Monk" and unit.Owner.IsLocal then UI.Message("A new monk joins the order.") end
end)

-- The Abbey (unit 1400, the first mod building slot) is finished.
Game.OnBuildingDone(function(b)
    if b.Type == 1400 and b.Owner == Game.LocalPlayer then
        UI.Message("The Abbey is finished: train monks and Abbots there.")
    end
end)

-- The Abbey's "Bless" button (unique_building commands, "lua": "Bless" in the civ JSON).
Game.OnCommand("Bless", function(abbey)
    local me = Game.LocalPlayer
    if me.Gold < 50 then
        UI.Message("Not enough gold to bless (50).")
        return
    end
    me:AddGold(-50)
    me:CreateUnit(125, (abbey.X or 0) + 3, (abbey.Y or 0) + 3)
    Sound.Play("TownBell")
end)
