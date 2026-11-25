-- Age of Miracles: lobby game mode ("age_of_miracles").
-- Rules run only when Game.Mode() == "age_of_miracles".
-- Multiplayer-safe: all resource/unit/victory changes go through lockstep game commands,
-- and Monastery button clicks run only on the clicking player's machine.

local S = MOD.settings or {}

local FAITH_RES = "miracle_faith"
local MONASTERY_ID = 104
local KNIGHT_ID = 38

local COST_MULT = { Cheap = 0.5, Normal = 1.0, Expensive = 1.5 }
local SPAWN_OFFSETS = { { 2.5, -1.0 }, { 2.5, 1.0 }, { -2.5, -1.0 }, { -2.5, 1.0 } }

local active = false
local ended = false
local income_timer = nil
local counter = nil
local defeated = {}
local passed_1000 = {}
local passed_1500 = {}

local function faith_target()
    local t = tonumber(S.faith_target) or 2000
    if t < 1000 then t = 1000 elseif t > 5000 then t = 5000 end
    return math.floor(t)
end

local function miracle_cost(base)
    local mult = COST_MULT[S.cost_scale] or 1.0
    return math.max(1, math.floor(base * mult + 0.5))
end

local function player_name(p)
    return "Player " .. tostring(p.Id)
end

local function oracle(text, player_id)
    UI.Chat(text, { From = "Oracle", Color = player_id })
end

-- Monastery miracles (placed on Page 2 buttons 1..4 so Page 1's Monk, techs,
-- Gather Point (5), Ungarrison (10) and Missionary (14) stay intact).
local MIRACLES = {
    {
        name = "MiracleHarvest",
        button = 1,
        icon = "bountiful_harvest",
        fallback_icon = 49,
        text = "Bountiful Harvest",
        base_cost = 100,
        desc = "Receive +400 Food.",
        run = function(building, owner, cost)
            owner:AddFood(400)
            oracle(player_name(owner) .. " invokes Bountiful Harvest! (+400 Food, -" .. cost .. " Faith)", owner.Id)
            Sound.Play("Tribute")
        end,
    },
    {
        name = "MiracleWarriors",
        button = 2,
        icon = "holy_warriors",
        fallback_icon = 32,
        text = "Holy Warriors",
        base_cost = 250,
        desc = "Summon 4 Knights beside this Monastery.",
        run = function(building, owner, cost)
            local bx = building.X or 10
            local by = building.Y or 10
            for _, off in ipairs(SPAWN_OFFSETS) do
                owner:CreateUnit(KNIGHT_ID, math.max(1, bx + off[1]), math.max(1, by + off[2]))
            end
            oracle(player_name(owner) .. " summons 4 Holy Warriors! (-" .. cost .. " Faith)", owner.Id)
            Sound.Play("Objective")
        end,
    },
    {
        name = "MiraclePlague",
        button = 3,
        icon = "plague",
        fallback_icon = 32,
        text = "Plague",
        base_cost = 400,
        desc = "Every enemy player loses up to 4 Villagers.",
        run = function(building, owner, cost)
            local struck = 0
            for _, p in ipairs(Game.Players()) do
                if p.Id ~= owner.Id and owner:IsEnemy(p) then
                    local vills = p:Units("Villager")
                    local n = math.min(4, #vills)
                    for i = 1, n do
                        vills[i]:Delete()
                    end
                    struck = struck + n
                end
            end
            oracle(player_name(owner) .. " unleashes a Plague upon all enemies (" .. struck .. " Villagers lost)!", owner.Id)
            Sound.Play("Attack")
        end,
    },
    {
        name = "MiracleFavour",
        button = 4,
        icon = "divine_favour",
        fallback_icon = 49,
        text = "Divine Favour",
        base_cost = 150,
        desc = "Receive +150 Gold and +150 Stone.",
        run = function(building, owner, cost)
            owner:AddGold(150)
            owner:AddStone(150)
            oracle(player_name(owner) .. " receives Divine Favour! (+150 Gold, +150 Stone, -" .. cost .. " Faith)", owner.Id)
            Sound.Play("Relic")
        end,
    },
}

local function add_command_safe(spec, fallback_icon)
    local ok, err = pcall(Game.AddCommand, spec)
    if not ok then
        -- Fallback if running on a loader build that still expects an integer 50721 frame
        print("AgeOfMiracles: Game.AddCommand icon fallback for " .. spec.name .. ": " .. tostring(err))
        spec.icon = fallback_icon
        Game.AddCommand(spec)
    end
end

local function register_miracles()
    if not Game.AddCommand then
        print("AgeOfMiracles: Game.AddCommand unavailable; update Wololoader.")
        return
    end
    for _, m in ipairs(MIRACLES) do
        local cost = miracle_cost(m.base_cost)
        add_command_safe({
            building = MONASTERY_ID,
            page = 2,
            button = m.button,
            icon = m.icon,
            name = m.name,
            text = m.text .. " (" .. cost .. " Faith)",
            help = "Cost: " .. cost .. " Faith. " .. m.desc,
            condition = m.name,
            condition_action = "disable",
        }, m.fallback_icon)
    end
end

local function unregister_miracles()
    if not Game.RemoveCommand then return end
    for _, m in ipairs(MIRACLES) do
        Game.RemoveCommand(m.name)
    end
end

for _, m in ipairs(MIRACLES) do
    Game.CommandCondition(m.name, function(building)
        if not active or ended then return false end
        local owner = building and building.Owner
        if not owner then return false end
        local faith = owner:GetResource(FAITH_RES) or 0
        return faith >= miracle_cost(m.base_cost)
    end)

    Game.OnCommand(m.name, function(building)
        if not active or ended then return end
        local owner = building and building.Owner
        if not owner then return end
        local cost = miracle_cost(m.base_cost)
        local faith = owner:GetResource(FAITH_RES) or 0
        if faith < cost then return end
        owner:AddResource(FAITH_RES, -cost)
        m.run(building, owner, cost)
    end)
end

Game.OnStart(function()
    if income_timer then
        Time.Cancel(income_timer)
        income_timer = nil
    end
    if counter then
        counter:Destroy()
        counter = nil
    end

    active = (Game.Mode() == "age_of_miracles")
    ended = false
    defeated = {}
    passed_1000 = {}
    passed_1500 = {}

    if not active then
        unregister_miracles()
        return
    end

    local target = faith_target()
    register_miracles()
    counter = UI:CreateCounter("Faith Goal: 0 / " .. target)
    UI.Message("Age of Miracles: gather " .. target .. " Faith or cast Miracles at your Monastery (Page 2)!")

    -- Faith income every 10 game seconds: +1 per Monk, +3 per Monk carrying a Relic.
    -- (Relics garrisoned inside a Monastery are not exposed by the Lua Unit/Player API.)
    income_timer = Time.Every(10, function()
        if not active or ended then return end
        for _, p in ipairs(Game.Players()) do
            if not defeated[p.Id] then
                local monks = #p:Units("Monk")
                local relic_monks = #p:Units("MonkWithRelic")
                local gain = monks + relic_monks * 3
                if gain > 0 then
                    p:AddResource(FAITH_RES, gain)
                end
            end
        end
    end)
end)

Game.OnSecond(function()
    if not active or ended then return end

    local target = faith_target()
    local me = Game.LocalPlayer
    if counter and me then
        local my_faith = math.floor(me:GetResource(FAITH_RES) or 0)
        counter:SetText("Faith Goal: " .. my_faith .. " / " .. target)
    end

    local winner = nil
    local winner_faith = -1

    for _, p in ipairs(Game.Players()) do
        local faith = p:GetResource(FAITH_RES) or 0

        if target > 1000 and faith >= 1000 and not passed_1000[p.Id] then
            passed_1000[p.Id] = true
            oracle("The heavens favour " .. player_name(p) .. " (1000 Faith)!", p.Id)
            Sound.Play("Objective")
        end
        if target > 1500 and faith >= 1500 and not passed_1500[p.Id] then
            passed_1500[p.Id] = true
            oracle("The heavens favour " .. player_name(p) .. " (1500 Faith)!", p.Id)
            Sound.Play("Objective")
        end

        if faith >= target and faith > winner_faith then
            winner = p
            winner_faith = faith
        end
    end

    if winner then
        ended = true
        oracle(player_name(winner) .. " reaches " .. target .. " Faith and wins an Age of Miracles victory!", winner.Id)
        UI.Message(player_name(winner) .. " achieves Miracle Victory (" .. target .. " Faith)!")
        Sound.Play("Objective")
        for _, other in ipairs(Game.Players()) do
            if other.Id ~= winner.Id and not defeated[other.Id] then
                defeated[other.Id] = true
                other:Defeat()
            end
        end
        winner:Win()
    end
end)
