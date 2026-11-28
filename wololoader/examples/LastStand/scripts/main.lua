-- Last Stand: co-op survival lobby game mode ("last_stand").
-- All players (humans and bots) are on one team (mod.json "teams": "all") and defend a shared Castle
-- against the Horde: Gaia units spawned in rounds that cannot be captured ({ Capturable = false }).
-- Rules run only when Game.Mode() == "last_stand".
-- Multiplayer-safe: everything that changes the game goes through lockstep commands, and all
-- decisions (spawn points, kills, round end) use only game time and shared game state, never
-- math.random (the only random part is the Mystery Box, and only the clicker's machine runs it).

local S = MOD.settings or {}

local MARKET_ID = 84   -- every player starts with one: the shop is on its page 2 too (an ally's Castle has no buttons for you)
local RES_CARTOGRAPHY = 50 -- MODDING.md resource table: 1 = shared vision with allies
local CASTLE_ID = 82   -- the objective: a Castle (a Wonder would start the game's own Wonder victory timer)
local WATCH_TOWER_ID = 79
local TOWN_CENTERS = { [109] = true, [71] = true, [141] = true, [142] = true }

local UNIT_MILITIA = 74
local UNIT_MAA = 75
local UNIT_SPEARMAN = 93
local UNIT_ARCHER = 4
local UNIT_SKIRMISHER = 6
local UNIT_SCOUT = 448
local UNIT_KNIGHT = 38
local UNIT_RAM = 35
local UNIT_CAPPED_RAM = 422
local UNIT_MANGONEL = 280
local UNIT_ELEPHANT = 239
local UNIT_TREBUCHET = 331 -- Packed Trebuchet (the form the game trains; unpacks by itself to attack)

-- every unit type the Horde can contain: the Horde is found with Player(0):UnitsOfType(type)
-- (a scan of all Gaia units would also return trees, mines and animals)
local HORDE_TYPES = {
    UNIT_MILITIA, UNIT_MAA, UNIT_SPEARMAN, UNIT_ARCHER, UNIT_SKIRMISHER, UNIT_SCOUT,
    UNIT_KNIGHT, UNIT_RAM, UNIT_CAPPED_RAM, UNIT_MANGONEL, UNIT_ELEPHANT, UNIT_TREBUCHET,
}

local RES_WAVE = "wave"   -- top bar: "Round"
local RES_NEXT = "next"   -- top bar: "Next round" (break countdown)
local RES_POINTS = "glory" -- top bar: "Points" (the resource key stays "glory", the name is "Points")
-- Saved games: Lua state is not in the savegame, resources are. "ls_state" (every player) is
-- 0 before the mode set up the match, else 1 + the Horde player's id (1 = Gaia Horde);
-- "ls_perks" holds each player's perk levels (PERK_SHIFT). Round and countdown are
-- RES_WAVE / RES_NEXT already.
local RES_STATE = "ls_state"
local RES_PERKS = "ls_perks"
local RES_VISION = "ls_vision"   -- map vision bought at the Castle: last round it lasts, -1 = whole game, 0 = none
-- "ls_perks" = armour level + 4 * guild level + 16 * armoury level (each 0..3: the ages bought)
local PERK_SHIFT = { LastStandJuggernaut = 1, LastStandSpeedCola = 4, LastStandPackAPunch = 16 }

local POINTS_PER_KILL = 10
local POINTS_PER_ROUND = 50

local DIFF_MULT = { Easy = 0.6, Normal = 1.0, Hard = 1.5, Insane = 2.2 }

local UNIT_OFFSETS = {
    {  3.5, -1.5 }, {  3.5,  0.0 }, {  3.5,  1.5 },
    { -3.5, -1.5 }, { -3.5,  0.0 }, { -3.5,  1.5 },
}
local TOWER_OFFSETS = {
    {  4.5,  4.5 }, { -4.5,  4.5 }, {  4.5, -4.5 }, { -4.5, -4.5 },
    {  6.0,  0.0 }, { -6.0,  0.0 }, {  0.0,  6.0 }, {  0.0, -6.0 },
}

-- Tech ids. 67 68 75 (blacksmith) and the Mystery Box ids below are verified in MODDING.md (Common Technology
-- IDs table). All other ids are the standard Age of Empires II tech ids and are NOT verified against the dat:
-- armour 74 76 77 (infantry), 81 82 80 (cavalry barding), 211 212 219 (archers), 213 Wheelbarrow, 249 Hand Cart,
-- 315 Conscription, 202 203 221 wood, 55 182 gold, 39 Husbandry. A wrong id is skipped (pcall) with a log line.
-- Perk techs by age (index 1 Feudal, 2 Castle, 3 Imperial): a perk is bought once per age and
-- gives the techs of every age up to the buyer's own that were not bought yet, so it follows the
-- buyer's age instead of handing out Imperial techs in the Feudal Age.
local TECHS_ARMOUR = {
    { 74, 81, 211 },    -- Scale Mail, Scale Barding, Padded Archer Armor
    { 76, 82, 212 },    -- Chain Mail, Chain Barding, Leather Archer Armor
    { 77, 80, 219 },    -- Plate Mail, Plate Barding, Ring Archer Armor
}
local TECHS_GUILD = {
    { 213, 202, 55 },   -- Wheelbarrow, Double-Bit Axe, Gold Mining
    { 249, 203, 182, 39 }, -- Hand Cart, Bow Saw, Gold Shaft Mining, Husbandry
    { 315, 221 },       -- Conscription, Two-Man Saw
}
local TECHS_ARMOURY = {
    { 222, 67, 199 },                  -- Man-at-Arms, Forging, Fletching
    { 207, 197, 98, 100, 68, 200 },    -- Long Swordsman, Pikeman, Elite Skirmisher, Crossbowman, Iron Casting, Bodkin Arrow
    { 24, 25, 429, 237, 209, 265, 428, 75, 201 }, -- Two-Handed, Champion, Halberdier, Arbalest, Cavalier, Paladin, Hussar, Blast Furnace, Bracer
}
-- Mystery Box "free Imperial-Age tech" prizes (ids from the verified MODDING.md table)
local BOX_TECHS = {
    { 93, "Ballistics" }, { 47, "Chemistry" }, { 377, "Siege Engineers" }, { 435, "Bloodlines" },
    { 231, "Sanctity" }, { 252, "Fervor" }, { 233, "Illumination" }, { 230, "Block Printing" },
    { 380, "Heated Shot" }, { 51, "Architecture" },
}

-- mystery box random numbers: math.random on the clicker's machine only
local rand = math.random or function(a, b) return a + math.floor((Game.Time or 0) * 7) % (b - a + 1) end

-- state
local active = false
local ended = false
local start_time = 0
local players = {}          -- every player (humans and bots), without Gaia and without the Horde player
-- The Horde's owner: the mode's enemy computer player (mod.json "enemy_player": true, single player),
-- so every unit and building fights it on sight. Without one (multiplayer, 8 players) it is Gaia.
local horde_owner = nil
local function horde_player() return horde_owner or Game.Player(0) end

local castle_id = nil
local castle_seen = false
local castle_target_x, castle_target_y = nil, nil
local castle_x, castle_y = nil, nil
local castle_retry_done = false
local pre_castles = {}

local current_round = 0
local rounds_survived = 0
local state = "break"       -- "break" (countdown) | "fight" (round running)
local next_round_at = 0
local spawn_queue = {}      -- chunks of the running round: { at, id, n, x, y }
local spawn_done_at = 0     -- game time of the last spawn command of the round
local tower_count = 0

local horde_prev = {}       -- [unit id] = true: Horde units seen last second (a missing one = a kill)
local orders = {}           -- [unit id] = { x, y, at }: where a Horde unit was at the last tick, and when ordered
local last_ai_at = -100
local shown_wave, shown_next = -1, -1
local perks = {}            -- [player id][perk name] = true (local to the machine that clicked)

local rng_state = 1

local function clamp(val, lo, hi, fallback)
    local n = tonumber(val) or fallback
    if n < lo then n = lo elseif n > hi then n = hi end
    return math.floor(n)
end

local AGE_INDEX = { Dark = 0, Feudal = 1, Castle = 2, Imperial = 3 }
local function get_config()
    return {
        rounds = clamp(S.rounds, 0, 100, 0),                -- 0 = endless
        break_seconds = clamp(S.break_seconds, 5, 120, 20),
        grace_seconds = clamp(S.grace_seconds, 10, 300, 90),
        difficulty = S.difficulty or "Normal",
        start_bonus = clamp(S.start_bonus, 0, 5000, 1000),
        start_age = AGE_INDEX[S.start_age or "Feudal"] or 1,
        mult = DIFF_MULT[S.difficulty] or 1.0,
    }
end

-- deterministic random numbers for spawn points (same result on every machine)
local function rnd()
    rng_state = (rng_state * 1103515245 + 12345) & 0x7fffffff
    return rng_state / 2147483648
end

local function commander(text, color)
    UI.Chat(text, { From = "Commander", Color = color })
end

local function player_label(p)
    return "Player " .. tostring(p.Id)
end

local function safe_create(player, unit_id, x, y, opts)
    local ok, res = pcall(player.CreateUnit, player, unit_id, math.max(3, x), math.max(3, y), opts)
    if not ok then
        print("LastStand: CreateUnit failed for type " .. tostring(unit_id) .. ": " .. tostring(res))
        return false
    end
    return true
end

local function find_player_tcs(p)
    local list = {}
    for _, u in ipairs(p:Units()) do
        if TOWN_CENTERS[u.Type] then
            list[#list + 1] = u
        end
    end
    return list
end

-- The Castle stands in the middle of the map. Random maps put the players on a circle around the
-- map centre, so the average of every player's Town Center (bots included) is the centre and the
-- players start around the Castle. Near-by spots are tried if a building is already there.
local function compute_castle_spot()
    local sx, sy, n, buildings = 0, 0, 0, {}
    for _, p in ipairs(players) do
        for _, u in ipairs(p:Units()) do
            if TOWN_CENTERS[u.Type] then sx, sy, n = sx + u.X, sy + u.Y, n + 1 end
            if u.IsBuilding then buildings[#buildings + 1] = u end
        end
    end
    if n == 0 then return 30, 30 end
    local mx, my = sx / n, sy / n
    if n == 1 then mx, my = mx + 8, my end      -- a lone player: next to the Town Center
    local candidates = { { 0, 0 }, { 4, 0 }, { 0, 4 }, { -4, 0 }, { 0, -4 }, { 6, 6 }, { -6, -6 } }
    for _, off in ipairs(candidates) do
        local cx, cy = mx + off[1], my + off[2]
        local clear = cx >= 4 and cy >= 4
        for _, o in ipairs(buildings) do
            if not clear then break end
            local dx, dy = cx - o.X, cy - o.Y
            if dx * dx + dy * dy < 20 then clear = false end
        end
        if clear then return cx, cy end
    end
    return math.max(4, mx), math.max(4, my)
end

-- Players start around the Castle. The mode's own map (maps/LastStandArena.rms) does that already;
-- on any other map the Town Centers are near the map edge, so at the start every far player gets a new Town Center on a ring (RING_RADIUS tiles) around the Castle, the old
-- Town Center and houses are removed, and the starting units (villagers, scout...) are rebuilt next to
-- the new Town Center (same unit types, so civ-specific starts stay right). The ring angle comes from
-- the player's place in the sorted list, so every machine computes the same spots; the commands are
-- sent only by the machine that sends for that player (gamecmd).
local RING_RADIUS = 13
local function relocate_players(cx, cy)
    local n = #players
    for i, p in ipairs(players) do
        local a = (i - 1) * 2 * math.pi / n + math.pi / 4
        local dx, dy = math.cos(a), math.sin(a)
        local tx, ty = cx + dx * RING_RADIUS, cy + dy * RING_RADIUS
        local old_buildings, old_units = {}, {}
        for _, u in ipairs(p:Units()) do
            if u.IsBuilding then
                if u.Type ~= CASTLE_ID then old_buildings[#old_buildings + 1] = u end
            else
                old_units[#old_units + 1] = u
            end
        end
        -- the Last Stand Arena map (mod.json "map") already starts everyone near the centre:
        -- only a player whose Town Center is far away (another map, e.g. a multiplayer lobby map) moves
        local near = false
        for _, u in ipairs(old_buildings) do
            if TOWN_CENTERS[u.Type] and (u.X - cx) ^ 2 + (u.Y - cy) ^ 2 <= 24 * 24 then near = true end
        end
        if not near and (#old_buildings > 0 or #old_units > 0) then
            safe_create(p, 109, tx, ty)                   -- the new Town Center first (never zero buildings)
            for _, u in ipairs(old_buildings) do pcall(u.Delete, u) end
            -- starting units on the far side of the Town Center (away from the Castle), in rows
            local ox, oy = tx + dx * 3.5, ty + dy * 3.5
            local px, py = -dy, dx                        -- along the row
            for k, u in ipairs(old_units) do
                local row, col = (k - 1) // 4, (k - 1) % 4
                local ux = ox + dx * row * 1.2 + px * (col - 1.5) * 1.2
                local uy = oy + dy * row * 1.2 + py * (col - 1.5) * 1.2
                if safe_create(p, u.Type, ux, uy) then pcall(u.Delete, u) end
            end
        end
    end
end

-- Start kit for everyone: extra resources, shared vision with the allies (Cartography) and a
-- finished Market next to the Town Center, on the side away from the Castle (for trading; the
-- shop is on the Castle's page 2, shared with every ally).
local function equip_players(cx, cy, bonus, start_age)
    for _, p in ipairs(players) do
        if bonus > 0 then
            p:AddFood(bonus); p:AddWood(bonus); p:AddGold(bonus); p:AddStone(bonus)
        end
        pcall(p.SetResource, p, RES_CARTOGRAPHY, 1)
        if start_age > 0 then pcall(p.SetAge, p, start_age) end
        local tc = find_player_tcs(p)[1]
        if tc then
            local dx, dy = tc.X - cx, tc.Y - cy
            local len = math.sqrt(dx * dx + dy * dy)
            if len < 1 then dx, dy, len = 1, 0, 1 end
            safe_create(p, MARKET_ID, tc.X + dx / len * 7, tc.Y + dy / len * 7)
        end
    end
end

-- ---------------------------------------------------------------------------------------------
-- Castle shop (the Castle's page 2; its own page 1 buttons stay untouched)
-- ---------------------------------------------------------------------------------------------

-- player's age 1 Feudal .. 3 Imperial (0 Dark): resource 6, MODDING.md
local function age_of(p)
    return math.max(0, math.min(3, math.floor((p:GetResource(6) or 0) + 0.5)))
end
local function perk_level(p, name)
    return perks[p.Id] and perks[p.Id][name] or 0
end
local function has_perk(p, name)   -- nothing left to buy for the player's current age
    return perk_level(p, name) >= math.max(1, age_of(p))
end

local UNIT_LONGSWORD, UNIT_TWOHANDED = 77, 473
local UNIT_CROSSBOW, UNIT_ARBALEST = 24, 492
local UNIT_GUARD_TOWER, UNIT_KEEP = 234, 235
local UNIT_CAVALIER = 283
local function tier(p) return math.max(1, age_of(p)) end   -- Dark counts as Feudal
local function by_age(p, t) return t[tier(p)] end

local function research_all(p, list)
    for _, tech in ipairs(list) do
        local ok, err = pcall(p.Research, p, tech)
        if not ok then print("LastStand: Research " .. tech .. " failed: " .. tostring(err)) end
    end
end

-- Mystery Box prizes. Each one is a function(bx, by, buyer).
local TEDDY_LINES = {
    "The chest holds a wooden toy horse. The Horde is not impressed.",
    "Only a jester's hat inside. Maybe next time.",
    "The chest is full of turnips. The cooks are pleased, the soldiers are not.",
    "A forged royal letter promising reinforcements. None will come.",
}
local BOX_PRIZES = {
    { w = 25, run = function(bx, by, buyer)
        for i = 1, 6 do
            local off = UNIT_OFFSETS[i]
            safe_create(buyer, age_of(buyer) >= 3 and UNIT_CAVALIER or UNIT_KNIGHT, bx + off[1], by + off[2])
        end
        commander(player_label(buyer) .. " opened the Merchant's Chest: 6 " .. (age_of(buyer) >= 3 and "Cavaliers" or "Knights") .. " ride out!", buyer.Id)
    end },
    { w = 15, run = function(bx, by, buyer)
        for i = 1, 2 do
            local off = UNIT_OFFSETS[i]
            safe_create(buyer, UNIT_TREBUCHET, bx + off[1], by + off[2])
        end
        commander(player_label(buyer) .. " opened the Merchant's Chest: 2 Trebuchets!", buyer.Id)
    end },
    { w = 20, run = function(_, _, buyer)
        local t = BOX_TECHS[rand(1, #BOX_TECHS)]
        pcall(buyer.Research, buyer, t[1])
        commander(player_label(buyer) .. " opened the Merchant's Chest: free " .. t[2] .. "!", buyer.Id)
    end },
    { w = 20, run = function(_, _, buyer)
        buyer:AddGold(1000)
        commander(player_label(buyer) .. " opened the Merchant's Chest: +1000 Gold!", buyer.Id)
    end },
    { w = 20, run = function(_, _, buyer)
        commander(TEDDY_LINES[rand(1, #TEDDY_LINES)], buyer.Id)
    end },
}

local function open_mystery_box(bx, by, buyer)
    local roll = rand(1, 100)
    for _, prize in ipairs(BOX_PRIZES) do
        if roll <= prize.w then
            prize.run(bx, by, buyer)
            return
        end
        roll = roll - prize.w
    end
    BOX_PRIZES[#BOX_PRIZES].run(bx, by, buyer)
end

-- perk = true: bought once per player; the button is disabled afterwards
-- Map vision: a grid of map revealers (837 REVEAL, line of sight 4) over the whole map, owned by
-- the buyer; every player has Cartography (shared vision), so all allies see every enemy.
local VISION_STEP = 7
local vision_until = 0      -- 0 none, -1 whole game, else the last round it lasts
local vision_owner = nil
local vision_ending = false
local function vision_grid(owner)
    local mx, my = castle_target_x or 60, castle_target_y or 60
    for x = 3, mx * 2 - 3, VISION_STEP do
        for y = 3, my * 2 - 3, VISION_STEP do safe_create(owner, 837, x, y) end
    end
end
-- The grid's owner is whoever has more than 20 revealers (the same on every machine; only the
-- owner's machine sends the deletes).
local function vision_remove()
    for _, p in ipairs(players) do
        local list = p:UnitsOfType(837)
        if #list > 20 then for _, u in ipairs(list) do pcall(u.Delete, u) end end
    end
    vision_owner = nil
end
local function set_vision(until_round)
    vision_until = until_round
    for _, p in ipairs(players) do p:SetResource(RES_VISION, until_round) end
end


local SHOP_ITEMS = {
    {
        name = "LastStandMercenaries", button = 1, icon = "hire_mercenaries", fallback_icon = 32,
        costs = { 300, 450, 650 }, label = "Hire Mercenaries",
        help_by_age = { "5 Men-at-Arms", "5 Long Swordsmen", "5 Two-Handed Swordsmen" },
        run = function(bx, by, buyer)
            local id = by_age(buyer, { UNIT_MAA, UNIT_LONGSWORD, UNIT_TWOHANDED })
            for i = 1, 5 do
                local off = UNIT_OFFSETS[i]
                safe_create(buyer, id, bx + off[1], by + off[2])
            end
            commander(player_label(buyer) .. " hired " .. by_age(buyer, { "5 Men-at-Arms", "5 Long Swordsmen", "5 Two-Handed Swordsmen" }) .. "!", buyer.Id)
            Sound.Play("Objective")
        end,
    },
    {
        name = "LastStandArchers", button = 2, icon = "archer_company", fallback_icon = 32,
        costs = { 400, 550, 750 }, label = "Archer Company",
        help_by_age = { "6 Archers", "6 Crossbowmen", "6 Arbalests" },
        run = function(bx, by, buyer)
            local id = by_age(buyer, { UNIT_ARCHER, UNIT_CROSSBOW, UNIT_ARBALEST })
            for i = 1, 6 do
                local off = UNIT_OFFSETS[i]
                safe_create(buyer, id, bx + off[1], by + off[2])
            end
            commander(player_label(buyer) .. " deployed an Archer Company!", buyer.Id)
            Sound.Play("Objective")
        end,
    },
    {
        name = "LastStandTower", button = 3, icon = "watch_tower", fallback_icon = 32,
        costs = { 500, 700, 900 }, label = "Build a Tower",
        help_by_age = { "a Watch Tower", "a Guard Tower", "a Keep" },
        run = function(bx, by, buyer)
            -- the next free spot around the Castle (from the towers on the map: the same for every
            -- buyer, not a count kept on one machine)
            local off = TOWER_OFFSETS[1]
            for _, o in ipairs(TOWER_OFFSETS) do
                local free = true
                for _, p in ipairs(players) do
                    for _, tid in ipairs({ WATCH_TOWER_ID, UNIT_GUARD_TOWER, UNIT_KEEP }) do
                        for _, t in ipairs(p:UnitsOfType(tid)) do
                            if (t.X - (bx + o[1])) ^ 2 + (t.Y - (by + o[2])) ^ 2 < 2 then free = false end
                        end
                    end
                end
                if free then off = o break end
            end
            safe_create(buyer, by_age(buyer, { WATCH_TOWER_ID, UNIT_GUARD_TOWER, UNIT_KEEP }), bx + off[1], by + off[2])
            commander(player_label(buyer) .. " raised " .. by_age(buyer, { "a Watch Tower", "a Guard Tower", "a Keep" }) .. " by the Castle!", buyer.Id)
            Sound.Play("Objective")
        end,
    },
    {
        name = "LastStandSupplies", button = 4, icon = "supply_drop", fallback_icon = 49,
        costs = { 150, 250, 350 }, label = "Supply Wagon",
        help_by_age = { "+250 Food, +250 Wood, +150 Gold", "+450 Food, +450 Wood, +250 Gold", "+650 Food, +650 Wood, +350 Gold" },
        run = function(_, _, buyer)
            local f, g = by_age(buyer, { 250, 450, 650 }), by_age(buyer, { 150, 250, 350 })
            buyer:AddFood(f)
            buyer:AddWood(f)
            buyer:AddGold(g)
            commander(player_label(buyer) .. " bought a Supply Wagon! (+" .. f .. " Food, +" .. f .. " Wood, +" .. g .. " Gold)", buyer.Id)
            Sound.Play("Tribute")
        end,
    },
    {
        name = "LastStandMysteryBox", button = 5, icon = "mystery_box", fallback_icon = 32,
        costs = { 950, 950, 950 }, label = "Merchant's Chest",
        help_by_age = { "A random prize: 6 Knights, 2 Trebuchets, a free tech, +1000 Gold... or nothing useful." },
        run = function(bx, by, buyer)
            Sound.Play("Relic")
            open_mystery_box(bx, by, buyer)
        end,
    },
    {
        name = "LastStandScouts", button = 9, icon = "watch_tower", fallback_icon = 32,
        costs = { 1000, 1000, 1000 }, label = "Scout Network",
        help_by_age = { "Your scouts watch the whole map: every enemy is visible to all allies for the next 3 rounds." },
        run = function(_, _, buyer)
            if vision_until ~= -1 then
                -- vision_until is the synced resource (read every second): one grid for all buyers
                if vision_until == 0 then vision_owner = buyer; vision_grid(buyer) end
                set_vision(math.max(vision_until, current_round + 3))
            end
            commander(player_label(buyer) .. " set up a Scout Network: the whole map is watched until round " ..
                (vision_until == -1 and "the end" or tostring(vision_until)) .. ".", buyer.Id)
            Sound.Play("Objective")
        end,
    },
    {
        name = "LastStandCartographers", button = 10, icon = "watch_tower", fallback_icon = 32,
        costs = { 5000, 5000, 5000 }, label = "Royal Cartographers",
        help_by_age = { "The whole map stays visible to all allies for the rest of the game." },
        condition = function() return vision_until ~= -1 end,
        run = function(_, _, buyer)
            if vision_until == 0 then vision_owner = buyer; vision_grid(buyer) end
            set_vision(-1)
            commander(player_label(buyer) .. " hired the Royal Cartographers: the whole map is visible for the rest of the game!", buyer.Id)
            Sound.Play("Objective")
        end,
    },
    {
        name = "LastStandJuggernaut", button = 6, icon = "juggernaut", fallback_icon = 32, perk = true,
        costs = { 700, 1100, 1600 }, label = "Iron Armour", techs = TECHS_ARMOUR,
        help_by_age = { "Once per Age. The armour upgrades of your Age and the ones before it (infantry, cavalry, archers)." },
        run = function(_, _, buyer)
            commander(player_label(buyer) .. " equipped the army with Iron Armour!", buyer.Id)
            Sound.Play("TownRecall")
        end,
    },
    {
        name = "LastStandSpeedCola", button = 7, icon = "speed_cola", fallback_icon = 32, perk = true,
        costs = { 500, 800, 1200 }, label = "Guild Charter", techs = TECHS_GUILD,
        help_by_age = { "Once per Age. The economy upgrades of your Age and the ones before it (Wheelbarrow, Hand Cart, Conscription, gathering)." },
        run = function(_, _, buyer)
            commander(player_label(buyer) .. " signed the Guild Charter! Faster work and training.", buyer.Id)
            Sound.Play("TownRecall")
        end,
    },
    {
        name = "LastStandPackAPunch", button = 8, icon = "pack_a_punch", fallback_icon = 32, perk = true,
        costs = { 900, 1500, 2400 }, label = "Royal Armoury", techs = TECHS_ARMOURY,
        help_by_age = { "Once per Age. The unit line and blacksmith attack upgrades of your Age and the ones before it." },
        run = function(_, _, buyer)
            commander(player_label(buyer) .. " opened the Royal Armoury! Every unit line upgraded.", buyer.Id)
            Sound.Play("TownRecall")
        end,
    },
}

local function add_command_safe(spec, fallback_icon)
    local ok, err = pcall(Game.AddCommand, spec)
    if not ok then
        print("LastStand: icon fallback for " .. spec.name .. ": " .. tostring(err))
        spec.icon = fallback_icon
        Game.AddCommand(spec)
    end
end

-- The shop is on the Castle's page 2. Its commands are shared (Game.AddCommand shared = true), so
-- every ally who selects the Castle gets the same page 2 and buys with their own Points.
local SHOP_BUILDINGS = { { id = CASTLE_ID, suffix = "" } }

local shop_tier = 0   -- the Age the shop buttons were registered for
local function item_cost(item, p) return item.costs[p and tier(p) or 1] end
-- Button text and tooltip for the local player's Age (the commands are registered again on an age up).
local function item_texts(item)
    local me = Game.LocalPlayer
    local t = me and tier(me) or 1
    local cost = item.costs[t]
    local what = item.help_by_age[t] or item.help_by_age[1]
    return item.label .. " (" .. cost .. " Points)", "Cost: " .. cost .. " Points. " .. what
end

local function register_shop()
    if not Game.AddCommand then return end
    shop_tier = Game.LocalPlayer and tier(Game.LocalPlayer) or 1
    for _, item in ipairs(SHOP_ITEMS) do
      for _, sb in ipairs(SHOP_BUILDINGS) do
        local page, button = 2, item.button
        item.text, item.help = item_texts(item)
        add_command_safe({
            building = sb.id,
            page = page,
            button = button,
            icon = item.icon,
            name = item.name .. sb.suffix,
            text = item.text,
            help = item.help,
            condition = item.name .. sb.suffix,
            condition_action = "disable",
            shared = true,
        }, item.fallback_icon)
      end
    end
end

local function unregister_shop()
    if not Game.RemoveCommand then return end
    for _, item in ipairs(SHOP_ITEMS) do
        for _, sb in ipairs(SHOP_BUILDINGS) do Game.RemoveCommand(item.name .. sb.suffix) end
    end
end

-- Can this player buy the item now? (enough Points, perk not bought yet, item's own condition)
local function can_buy(item, buyer)
    if not active or ended or not buyer then return false end
    if item.perk and has_perk(buyer, item.name) then return false end
    if item.condition and not item.condition() then return false end
    return (buyer:GetResource(RES_POINTS) or 0) >= item_cost(item, buyer)
end

-- A purchase, run on the buyer's machine only (a button click): every effect is a game command.
local function buy_item(item, buyer)
    if not can_buy(item, buyer) then return end
    buyer:AddResource(RES_POINTS, -item_cost(item, buyer))
    if item.perk then
        -- the techs of every age from the last bought one up to the buyer's own
        local from, to = perk_level(buyer, item.name) + 1, tier(buyer)
        for t = from, to do research_all(buyer, item.techs[t]) end
        perks[buyer.Id] = perks[buyer.Id] or {}
        perks[buyer.Id][item.name] = to
        buyer:AddResource(RES_PERKS, (to - from + 1) * PERK_SHIFT[item.name])
    end
    item.run(castle_x or castle_target_x or 20, castle_y or castle_target_y or 20, buyer)
end

for _, item in ipairs(SHOP_ITEMS) do
  for _, sb in ipairs(SHOP_BUILDINGS) do
    Game.CommandCondition(item.name .. sb.suffix, function(building)
        return can_buy(item, Game.LocalPlayer or (building and building.Owner))
    end)

    Game.OnCommand(item.name .. sb.suffix, function(building)
        buy_item(item, Game.LocalPlayer or (building and building.Owner))
    end)
  end
end


-- Trade goods for Points at your own Market (page 2, slots 1-4: the Market's own buttons and the
-- Sell/Buy rows stay as they are). Points are worth a lot (25 = half a survived round), and the
-- rates follow how easy a resource is to get: wood and food come without end (forests, farms), so
-- they cost the most; stone and gold mines run out. Like the game's own Market, every trade of a
-- resource makes the next one 15% dearer for that player (the clicker's machine runs it, so all
-- machines see the same commands).
local TRADE_STEP = 0.15
local MARKET_TRADES = {
    { name = "LastStandSellFood",  res = 0, base = 500, button = 1, label = "Food",  icon = "trade_food",  fallback = 30 },
    { name = "LastStandSellWood",  res = 1, base = 700, button = 2, label = "Wood",  icon = "trade_wood",  fallback = 29 },
    { name = "LastStandSellStone", res = 2, base = 300, button = 3, label = "Stone", icon = "trade_stone", fallback = 31 },
    { name = "LastStandSellGold",  res = 3, base = 250, button = 4, label = "Gold",  icon = "trade_gold",  fallback = 28 },
}
local TRADE_POINTS = 25
local trades_done = {}   -- [resource] = trades made by the local player
local function trade_amount(t)
    local n = trades_done[t.res] or 0
    return math.floor(t.base * (1 + TRADE_STEP) ^ n / 10 + 0.5) * 10
end
local function register_trades()
    if not Game.AddCommand then return end
    for _, t in ipairs(MARKET_TRADES) do
        local amount = trade_amount(t)
        local spec = {
            building = MARKET_ID, page = 2, button = t.button, icon = t.icon, name = t.name,
            text = "Trade " .. amount .. " " .. t.label .. " for " .. TRADE_POINTS .. " Points",
            help = "Give " .. amount .. " " .. t.label .. " for " .. TRADE_POINTS .. " Points to spend at the Castle. Each trade raises the price of the next one.",
            condition = t.name, condition_action = "disable",
        }
        if not pcall(Game.AddCommand, spec) then spec.icon = t.fallback; pcall(Game.AddCommand, spec) end
    end
end
local function unregister_trades()
    if not Game.RemoveCommand then return end
    for _, t in ipairs(MARKET_TRADES) do Game.RemoveCommand(t.name) end
end
for _, t in ipairs(MARKET_TRADES) do
    Game.CommandCondition(t.name, function()
        local me = Game.LocalPlayer
        return active and not ended and me ~= nil and (me:GetResource(t.res) or 0) >= trade_amount(t)
    end)
    Game.OnCommand(t.name, function()
        local me = Game.LocalPlayer
        local amount = trade_amount(t)
        if not (active and not ended and me) or (me:GetResource(t.res) or 0) < amount then return end
        me:AddResource(t.res, -amount)
        me:AddResource(RES_POINTS, TRADE_POINTS)
        trades_done[t.res] = (trades_done[t.res] or 0) + 1
        register_trades()        -- the new price on the button
        Sound.Play("Tribute")
    end)
end

-- The shop's names, costs and units follow the local player's Age: refresh the buttons on an age up.
Game.OnAgeUp(function(p)
    local me = Game.LocalPlayer
    if active and not ended and me and p and p.Id == me.Id then register_shop() end
end)

-- ---------------------------------------------------------------------------------------------
-- The Horde: what a round contains
-- ---------------------------------------------------------------------------------------------

-- Normal mix: { unit, label, weight, first round, last round (nil = no end) }
local MIX = {
-- weakest first: rounds 1-2 are Militia only (round 1 is smaller, see build_round), Scouts join in
-- round 3, Spearmen in round 4. No weaker unit works: villagers were put to work by the enemy AI
-- and animals (Bear 486) cannot attack buildings, so they ignored the Castle (seen 2026-11-26).
    { UNIT_MILITIA,   "Militia",   50, 1, 7 },
    { UNIT_SPEARMAN,  "Spearmen",  25, 4, 9 },
    { UNIT_SCOUT,     "Scouts",    15, 3, nil },  -- the fast "runners"
    { UNIT_ARCHER,    "Archers",   20, 5, nil },
    { UNIT_MAA,       "Men-at-Arms", 25, 6, nil },
    { UNIT_KNIGHT,    "Knights",   15, 9, nil },
    { UNIT_SKIRMISHER, "Skirmishers", 10, 12, nil },
}

-- returns { {id, label, count}, ... }, boss message or nil
local function build_round(r, mult)
    local groups = {}
    local function add(uid, label, n)
        n = math.floor(n + 0.5)
        if n >= 1 then groups[#groups + 1] = { id = uid, label = label, count = n } end
    end

    local total = math.min(160, math.floor((6 + 3 * r) * mult + 0.5))
    if r == 1 then total = math.max(3, math.floor(5 * mult + 0.5)) end -- an easy start
    local boss_msg = nil
    local share = 1.0
    if r % 5 == 0 then
        local tier = r // 5
        share = 0.6 -- the rest of the round is an escort
        if r % 10 == 0 then
            -- every 10th round: something wild
            add(UNIT_ELEPHANT, "War Elephants", (1 + tier) * mult)
            add(UNIT_TREBUCHET, "Trebuchets", math.min(6, tier) * mult)
            add(UNIT_CAPPED_RAM, "Capped Rams", (1 + tier) * mult)
            add(UNIT_MANGONEL, "Mangonels", tier * mult)
            boss_msg = "ROUND " .. r .. ": THE APOCALYPSE! Elephants, trebuchets and rams are coming!"
        elseif tier % 2 == 1 and tier % 3 ~= 0 then
            add(UNIT_ELEPHANT, "War Elephants", (1 + tier) * mult)
            boss_msg = "ROUND " .. r .. ": BOSS ROUND! The War Elephants are coming!"
        else
            add(UNIT_TREBUCHET, "Trebuchets", math.min(4, 1 + tier // 2) * mult)
            add(UNIT_RAM, "Battering Rams", (1 + tier) * mult)
            boss_msg = "ROUND " .. r .. ": BOSS ROUND! Siege engines march on the Castle!"
        end
    end

    local want = total * share
    local sum = 0
    local live = {}
    for _, m in ipairs(MIX) do
        if r >= m[4] and (not m[5] or r <= m[5]) then
            live[#live + 1] = m
            sum = sum + m[3]
        end
    end
    for _, m in ipairs(live) do
        add(m[1], m[2], math.max(1, want * m[3] / sum))
    end
    if r >= 14 and r % 5 ~= 0 then
        add(UNIT_RAM, "Battering Rams", (1 + (r - 14) // 3) * math.max(1, mult))
    end
    if r >= 18 and r % 5 ~= 0 then
        add(UNIT_MANGONEL, "Mangonels", (1 + (r - 18) // 4) * math.max(1, mult))
    end
    return groups, boss_msg
end

-- ---------------------------------------------------------------------------------------------
-- Spawn points: a ring 25..40 tiles outside the players' bases
-- ---------------------------------------------------------------------------------------------

local function base_bounds()
    local minx, miny, maxx, maxy = 1e9, 1e9, -1e9, -1e9
    local found = false
    local function take(u)
        found = true
        if u.X < minx then minx = u.X end
        if u.X > maxx then maxx = u.X end
        if u.Y < miny then miny = u.Y end
        if u.Y > maxy then maxy = u.Y end
    end
    for _, p in ipairs(players) do
        for _, u in ipairs(p:Units()) do
            if u.IsBuilding then take(u) end
        end
    end
    if not found then
        for _, p in ipairs(players) do
            for _, u in ipairs(p:Units()) do take(u) end
        end
    end
    if not found then
        local x, y = castle_x or 30, castle_y or 30
        return x, y, 6, 6
    end
    return (minx + maxx) / 2, (miny + maxy) / 2, math.max(6, (maxx - minx) / 2), math.max(6, (maxy - miny) / 2)
end

-- Collect static obstacle positions (Gaia trees/rocks/mines/fish + player buildings) once per round
-- so raiders never spawn trapped inside forests, cliffs, ponds, or buildings.
local function collect_obstacles()
    local obs = {}
    local gaia = Game.Player(0)
    if gaia then
        for _, u in ipairs(gaia:Units()) do
            obs[#obs + 1] = { u.X, u.Y }
        end
    end
    for _, p in ipairs(players) do
        for _, u in ipairs(p:Units()) do
            if u.IsBuilding then
                obs[#obs + 1] = { u.X, u.Y }
            end
        end
    end
    return obs
end

local function min_obstacle_dist2(x, y, obs)
    local best = 1e18
    for i = 1, #obs do
        local dx = x - obs[i][1]
        local dy = y - obs[i][2]
        local d2 = dx * dx + dy * dy
        if d2 < best then best = d2 end
    end
    return best
end

-- Map tiles the Horde must not spawn on: every Gaia object except animals (trees, mines, bushes,
-- cliffs, relics...), one entry per map tile. Rebuilt at each round start (trees get cut). Units
-- created on such a tile were stuck inside the forest (seen in a multiplayer game 2026-11-28).
local GAIA_WALKABLE = { [58] = true, [9] = true }   -- classes: livestock (sheep), prey animals
local blocked = {}
local function rebuild_blocked()
    blocked = {}
    local ok, list = pcall(Game.Player(0).Units, Game.Player(0))
    if not ok or not list then return end
    for _, u in ipairs(list) do
        if not GAIA_WALKABLE[u.Class or -1] then
            blocked[(math.floor(u.X)) * 4096 + math.floor(u.Y)] = true
        end
    end
end
-- the free tile nearest to (x, y), searched in growing squares up to 10 tiles away
local function free_spot(x, y, taken)
    local tx, ty = math.floor(x), math.floor(y)
    for r = 0, 10 do
        for dx = -r, r do
            for dy = -r, r do
                if math.max(math.abs(dx), math.abs(dy)) == r then
                    local k = (tx + dx) * 4096 + (ty + dy)
                    if tx + dx >= 3 and ty + dy >= 3 and not blocked[k] and not taken[k] then
                        taken[k] = true
                        return tx + dx + 0.5, ty + dy + 0.5
                    end
                end
            end
        end
    end
    return x, y
end

-- The Horde comes from the edge of the map: points on a ring a few tiles inside the map border
-- (the Castle stands at the map centre, so the map is about 2 * Castle tiles wide), spread around
-- the map, each one the clearest of several tries (away from trees, mines and cliffs).
local function spawn_points(r)
    local mx, my = castle_target_x or 60, castle_target_y or 60
    local radius = math.max(20, math.min(mx, my) - 9)
    local count = math.min(8, 4 + r // 4)
    local base = rnd() * 2 * math.pi
    local obs = collect_obstacles()
    local CLEAR_D2 = 4.5 * 4.5  -- chunk footprint is ~3 tiles wide; 4.5 tiles keeps every unit clear of trees
    local pts = {}
    for i = 1, count do
        local sector = base + (i - 1) * 2 * math.pi / count
        local best_x, best_y, best_d2 = nil, nil, -1
        for attempt = 1, 24 do
            local spread = (attempt <= 8) and 0.35 or 0.9
            local a = sector + (rnd() - 0.5) * spread
            local d = radius - rnd() * 8
            local px = math.max(6, math.min(mx * 2 - 6, mx + math.cos(a) * d))
            local py = math.max(6, math.min(my * 2 - 6, my + math.sin(a) * d))
            local d2 = min_obstacle_dist2(px, py, obs)
            if d2 > best_d2 then
                best_d2 = d2
                best_x, best_y = px, py
            end
            if d2 >= CLEAR_D2 then break end
        end
        pts[i] = { best_x or 10, best_y or 10 }
    end
    return pts
end

-- The round's spawn points are shown for REVEAL_SECONDS: a flare on the map and the minimap
-- (unit 274 FLARE2) and a few revealers (837 REVEAL, line of sight 4) that lift the fog there. They
-- belong to the first player and every player has Cartography (equip_players), so all allies see
-- them. Removed again by hide_spawn_points.
local REVEAL_SECONDS = 8
local REVEAL_OFFSETS = { { 0, 0 }, { 5, 0 }, { -5, 0 }, { 0, 5 }, { 0, -5 } }
local reveal_until = nil
local shown_points = {}     -- spawn points shown this round (their revealers are removed after REVEAL_SECONDS)
local function reveal_spawn_points(pts, used, now)
    local owner = players[1]
    if not owner then return end
    if vision_until ~= 0 then return end   -- the map is watched already; deleting these later would cut holes in the grid
    for i = 1, math.min(#pts, used) do
        local x, y = pts[i][1], pts[i][2]
        shown_points[#shown_points + 1] = { x, y }
        safe_create(owner, 274, x, y)
        for _, off in ipairs(REVEAL_OFFSETS) do safe_create(owner, 837, x + off[1], y + off[2]) end
    end
    reveal_until = now + REVEAL_SECONDS
end
local function hide_spawn_points(now)
    if not reveal_until or now < reveal_until then return end
    reveal_until = nil
    local owner = players[1]
    if not owner then return end
    for _, t in ipairs({ 837, 274 }) do
        for _, u in ipairs(owner:UnitsOfType(t)) do
            for _, pt in ipairs(shown_points) do
                if (u.X - pt[1]) ^ 2 + (u.Y - pt[2]) ^ 2 <= 49 then pcall(u.Delete, u) break end
            end
        end
    end
    shown_points = {}
end

local function start_round(now)
    local cfg = get_config()
    current_round = current_round + 1
    local r = current_round
    rng_state = (r * 7919 + 31337) & 0x7fffffff

    local groups, boss_msg = build_round(r, cfg.mult)
    rebuild_blocked()
    local pts = spawn_points(r)
    for _, pt in ipairs(pts) do pt[1], pt[2] = free_spot(pt[1], pt[2], {}) end  -- flare/reveal on open ground

    -- chunks of up to 5 units (1-2 for siege), mixed and spread over 10..20 s
    local chunks = {}
    local parts = {}
    for _, g in ipairs(groups) do
        parts[#parts + 1] = g.count .. " " .. g.label
        local size = (g.id == UNIT_ELEPHANT or g.id == UNIT_TREBUCHET or g.id == UNIT_RAM
            or g.id == UNIT_CAPPED_RAM or g.id == UNIT_MANGONEL) and 2 or 5
        local left = g.count
        while left > 0 do
            local n = math.min(size, left)
            chunks[#chunks + 1] = { id = g.id, n = n }
            left = left - n
        end
    end
    for i = #chunks, 2, -1 do -- shuffle (deterministic rnd)
        local j = 1 + math.floor(rnd() * i)
        chunks[i], chunks[j] = chunks[j], chunks[i]
    end
    local window = math.min(20, 10 + r)
    for i, c in ipairs(chunks) do
        local pt = pts[((i - 1) % #pts) + 1]
        spawn_queue[#spawn_queue + 1] = {
            at = now + math.floor((i - 1) * window / #chunks),
            id = c.id, n = c.n, x = pt[1], y = pt[2],
        }
    end
    reveal_spawn_points(pts, #chunks, now)
    state = "fight"
    spawn_done_at = now + window

    local summary = table.concat(parts, ", ")
    if boss_msg then
        commander(boss_msg .. " (" .. summary .. ")", 2)
        Sound.Play("TownBell")
    else
        local lines = {
            "Round " .. r .. "... they're coming!",
            "Round " .. r .. "! Hold the line!",
            "Round " .. r .. " begins. Don't let them reach the Castle!",
            "Round " .. r .. "... brace yourselves!",
        }
        commander(lines[((r - 1) % #lines) + 1] .. " (" .. summary .. ")", 2)
        Sound.Play("Attack")
    end
end

-- spawn the chunks that are due (game time, same on every machine)
local function run_spawn_queue(now)
    if #spawn_queue == 0 then return end
    local owner = horde_player()
    local opts = (not horde_owner) and { Capturable = false } or nil
    local rest = {}
    local taken = {}
    for _, c in ipairs(spawn_queue) do
        if c.at <= now then
            local cols = 3
            for i = 1, c.n do
                local dx = ((i - 1) % cols - 1) * 1.3
                local dy = ((i - 1) // cols) * 1.3
                local sx, sy = free_spot(c.x + dx, c.y + dy, taken)
                safe_create(owner, c.id, sx, sy, opts)
            end
        else
            rest[#rest + 1] = c
        end
    end
    spawn_queue = rest
end

-- ---------------------------------------------------------------------------------------------
-- The Horde's brain (cheap: one scan of the players' units per tick, one target per map cell)
-- ---------------------------------------------------------------------------------------------

local function scan_horde()
    local owner = horde_player()
    local cur, n = {}, 0
    for _, t in ipairs(HORDE_TYPES) do
        for _, u in ipairs(owner:UnitsOfType(t)) do
            cur[u.Id] = u
            n = n + 1
        end
    end
    return cur, n
end

-- one representative (preferably a building) per 8x8 tile cell of everything the players own
local function build_targets()
    local cells = {}
    for _, p in ipairs(players) do
        for _, u in ipairs(p:Units()) do
            local key = (u.X // 8) * 4096 + (u.Y // 8)
            local c = cells[key]
            if not c or (u.IsBuilding and not c.b) then
                cells[key] = { x = u.X, y = u.Y, b = u.IsBuilding, id = u.Id }
            end
        end
    end
    local reps = {}
    for _, c in pairs(cells) do reps[#reps + 1] = c end
    if castle_x then reps[#reps + 1] = { x = castle_x, y = castle_y, b = true, id = castle_id } end -- the Castle is always a target
    return reps
end

local FIGHTERS = { Archer = true, Infantry = true, Cavalry = true, Siege = true, CavalryArcher = true } -- MODDING.md class list

-- Other players' army attacks the nearest Horde unit within 30 tiles (at most 60 orders).
-- Only idle units get an order (a unit that moved since the last check is busy) and nobody is
-- ordered twice within 15 s unless its target is dead: repeating orders froze marching units.
local ally_orders = {}   -- [unit id] = { x, y, at, target }
local function ally_assist(cur)
    local list = {}
    for _, h in pairs(cur) do list[#list + 1] = h end
    if #list == 0 then return end
    local me = Game.LocalPlayer
    local now = Game.Time or 0
    local sent = 0
    for _, p in ipairs(players) do
        if not (me and p.Id == me.Id) then
            for _, u in ipairs(p:Units()) do
                if sent >= 60 then return end
                if not u.IsBuilding and FIGHTERS[u.ClassName or ""] then
                    local o = ally_orders[u.Id]
                    local moving = o and ((u.X - o.x) ^ 2 + (u.Y - o.y) ^ 2 > 0.25)
                    local target_alive = o and o.target and cur[o.target]
                    local due = not o or (not moving and (not target_alive or now - o.at >= 15))
                    if o then o.x, o.y = u.X, u.Y end
                    if due then
                        local best, bd = nil, 900
                        for i = 1, #list do
                            local dx, dy = list[i].X - u.X, list[i].Y - u.Y
                            local d = dx * dx + dy * dy
                            if d < bd then best, bd = list[i], d end
                        end
                        if best then
                            pcall(u.Attack, u, best.Id)
                            ally_orders[u.Id] = { x = u.X, y = u.Y, at = now, target = best.Id }
                            sent = sent + 1
                        end
                    end
                end
            end
        end
    end
end

-- The Horde marches on the Castle, but it does not walk past the players: a Horde unit with a
-- player's unit or building within AGGRO_RANGE tiles fights that first (the nearest one), then goes
-- on to the Castle. Orders: a fresh unit, a new target, a walking unit every 20 s, a standing one
-- every 10 s (repeating orders more often froze marching units).
local AGGRO_RANGE = 7
local NOT_TARGETS = { [837] = true, [274] = true } -- the spawn point revealers and flares

local function horde_ai(cur, now)
    local castle_ref = (castle_seen and castle_id) and { x = castle_x, y = castle_y, id = castle_id } or nil
    local reps = castle_ref and {} or build_targets()
    if #reps == 0 and not castle_ref then return end
    -- the players' things, in 8x8 tile buckets for the near-by search
    local buckets = {}
    for _, p in ipairs(players) do
        for _, pu in ipairs(p:Units()) do
            if not NOT_TARGETS[pu.Type] then
                local key = (pu.X // 8) * 4096 + (pu.Y // 8)
                local bk = buckets[key]
                if not bk then bk = {}; buckets[key] = bk end
                bk[#bk + 1] = pu
            end
        end
    end
    local r2 = AGGRO_RANGE * AGGRO_RANGE
    local sent = 0
    for id, u in pairs(cur) do
        if sent >= 80 then break end -- at most 80 orders per tick (the command queue is limited)
        local best, bd = nil, r2
        local cx, cy = u.X // 8, u.Y // 8
        for gx = cx - 1, cx + 1 do
            for gy = cy - 1, cy + 1 do
                local bk = buckets[gx * 4096 + gy]
                if bk then
                    for i = 1, #bk do
                        local t = bk[i]
                        local dx, dy = t.X - u.X, t.Y - u.Y
                        local d = dx * dx + dy * dy
                        if d < bd then best, bd = { x = t.X, y = t.Y, id = t.Id }, d end
                    end
                end
            end
        end
        if not best then
            best, bd = castle_ref, 1e18
            for i = 1, (castle_ref and 0 or #reps) do   -- the nearest player cell while the Castle is unknown
                local dx, dy = reps[i].x - u.X, reps[i].y - u.Y
                local d = dx * dx + dy * dy
                if d < bd then best, bd = reps[i], d end
            end
        end
        local o = orders[id]
        local go = false
        if not o then
            go = true                                   -- fresh Horde unit
        else
            local mx, my = u.X - o.x, u.Y - o.y
            if best and best.id ~= o.target then
                go = true                               -- something closer to fight (or back to the Castle)
            elseif mx * mx + my * my > 0.36 then
                go = (now - o.at >= 20)                 -- walking: retarget now and then
            else
                go = (now - o.at >= 10)                 -- standing (idle or stuck): attack again
            end
            o.x, o.y = u.X, u.Y
        end
        if go and best then
            if not (best.id and u.Attack and pcall(u.Attack, u, best.id)) then
                pcall(u.MoveTo, u, best.x, best.y)
            end
            orders[id] = { x = u.X, y = u.Y, at = now, target = best.id }
            sent = sent + 1
        end
    end
    for id in pairs(orders) do
        if not cur[id] then orders[id] = nil end
    end
end

-- ---------------------------------------------------------------------------------------------
-- End of the game
-- ---------------------------------------------------------------------------------------------

local function trigger_defeat(reason)
    if ended then return end
    ended = true
    commander(reason, 2)
    commander("Game over! You survived " .. rounds_survived .. (rounds_survived == 1 and " round." or " rounds."), 2)
    Sound.Play("TownBell")
    for _, p in ipairs(players) do
        p:Defeat()
    end
end

local function trigger_victory(total)
    if ended then return end
    ended = true
    commander("All " .. total .. " rounds survived! The Castle stands - VICTORY!")
    Sound.Play("Objective")
    for _, p in ipairs(players) do
        p:Win(true)
    end
end

local function set_bar(round, secs)
    if round ~= shown_wave or secs ~= shown_next then
        shown_wave, shown_next = round, secs
        for _, p in ipairs(players) do
            p:SetResource(RES_WAVE, round)
            p:SetResource(RES_NEXT, secs)
        end
    end
end

-- Loaded savegame: nothing is created again; the Castle, the Horde player, the round, the
-- countdown and the perks come back from the saved game state.
local function resume_saved_game(saved)
    local enemy_id = saved - 1
    horde_owner = nil
    if enemy_id > 0 then
        horde_owner = (Game.SetModeEnemy and Game.SetModeEnemy(enemy_id)) or Game.Player(enemy_id)
    end
    for _, p in ipairs(Game.Players()) do
        if not (horde_owner and p.Id == horde_owner.Id) then players[#players + 1] = p end
    end
    table.sort(players, function(a, b) return a.Id < b.Id end)
    if #players == 0 then ended = true return end
    local host = players[1]
    -- the Castle: the host's Castle nearest to the middle of the players' Town Centers
    local cx, cy = compute_castle_spot()
    local best, bd = nil, 1e18
    for _, u in ipairs(host:UnitsOfType(CASTLE_ID)) do
        local d = (u.X - cx) ^ 2 + (u.Y - cy) ^ 2
        if d < bd then best, bd = u, d end
    end
    castle_target_x, castle_target_y = cx, cy
    if best then
        castle_id, castle_x, castle_y, castle_seen = best.Id, best.X, best.Y, true
    end
    castle_retry_done = true
    for _, p in ipairs(players) do
        local bits = math.floor((p:GetResource(RES_PERKS) or 0) + 0.5)
        for name, shift in pairs(PERK_SHIFT) do
            local lv = (bits // shift) % 4
            if lv > 0 then perks[p.Id] = perks[p.Id] or {}; perks[p.Id][name] = lv end
        end
    end
    -- spawn point flares and revealers saved while showing (not the map vision grid: a saved
    -- vision keeps its revealers)
    local saved_vision = math.floor((host:GetResource(RES_VISION) or 0) + 0.5)
    for _, u in ipairs(host:UnitsOfType(274)) do pcall(u.Delete, u) end
    if saved_vision == 0 then
        for _, u in ipairs(host:UnitsOfType(837)) do pcall(u.Delete, u) end
    end
    tower_count = #host:UnitsOfType(WATCH_TOWER_ID)
    vision_until = math.floor((host:GetResource(RES_VISION) or 0) + 0.5)
    if vision_until ~= 0 then
        for _, p in ipairs(players) do                 -- whoever owns the saved revealer grid
            if #p:UnitsOfType(837) > 20 then vision_owner = p break end
        end
    end
    register_shop()
    register_trades()
    local now = math.max(0, Game.Time or 0)
    start_time = now
    current_round = math.floor((host:GetResource(RES_WAVE) or 0) + 0.5)
    local secs = math.floor((host:GetResource(RES_NEXT) or 0) + 0.5)
    if current_round > 0 and secs == 0 then
        state = "fight"                         -- a round was running: it ends when its Horde is gone
        spawn_done_at = now
        rounds_survived = current_round - 1
    else
        state = "break"
        next_round_at = now + (secs > 0 and secs or get_config().break_seconds)
        rounds_survived = current_round
    end
    shown_wave, shown_next = current_round, secs
    commander("Last Stand continues: round " .. current_round .. (state == "break" and (", next in " .. secs .. "s.") or " is on!"))
end

-- Enemies left (top right, this machine only): the Horde on the map plus the part of the round
-- that has not come out yet.
local counter_frame, counter_label, counter_text = nil, nil, nil
local function update_counter(alive)
    if not counter_label then
        -- solid background: a translucent one kept the old text visible under a changed one
        local ok, f = pcall(UI.CreateFrame, UI, { Anchor = "TopRight", Position = { 8, 40 }, Size = { 230, 30 },
            Color = "#2b2116", Transparency = 0, BorderColor = "#c8a046" })
        if not ok then return end
        counter_frame = f
        local ok2, l = pcall(UI.CreateLabel, UI, { Parent = f, Text = "", Position = { 0, 5 }, Size = { 230, 22 },
            Align = "Center", Bold = true, TextColor = "#f2dc9b", Color = false })
        if not ok2 then return end
        counter_label = l
    end
    local queued = 0
    for _, c in ipairs(spawn_queue) do queued = queued + c.n end
    local text
    if state == "fight" then
        text = "Round " .. current_round .. "  -  Enemies left: " .. (alive + queued)
    else
        text = "Round " .. (current_round + 1) .. " in " .. math.max(0, math.ceil(next_round_at - math.max(0, Game.Time or 0))) .. "s"
    end
    if text ~= counter_text then counter_text = text; pcall(counter_label.SetText, counter_label, text) end
end

Game.OnStart(function()
    active = (Game.Mode() == "last_stand")
    ended = false
    players = {}
    castle_id = nil
    castle_seen = false
    castle_target_x, castle_target_y = nil, nil
    castle_x, castle_y = nil, nil
    castle_retry_done = false
    pre_castles = {}
    current_round = 0
    rounds_survived = 0
    state = "break"
    spawn_queue = {}
    spawn_done_at = 0
    tower_count = 0
    horde_prev = {}
    orders = {}
    last_ai_at = -100
    shown_wave, shown_next = -1, -1
    perks = {}
    trades_done = {}
    vision_until, vision_owner = 0, nil

    if not active then
        unregister_shop()
        unregister_trades()
        return
    end

    if not Game.LocalPlayer then
        ended = true
        return
    end

    -- a loaded savegame: continue where it was (see RES_STATE)
    local saved = 0
    for _, p in ipairs(Game.Players()) do
        saved = math.max(saved, math.floor((p:GetResource(RES_STATE) or 0) + 0.5))
    end
    if saved > 0 then
        resume_saved_game(saved)
        return
    end

    horde_owner = Game.ModeEnemy and Game.ModeEnemy() or nil
    for _, p in ipairs(Game.Players()) do
        if not (horde_owner and p.Id == horde_owner.Id) then players[#players + 1] = p end
    end
    if horde_owner then
        -- the Horde starts with nothing: its map start (Town Center, villagers, scout) is removed
        for _, u in ipairs(horde_owner:Units()) do pcall(u.Delete, u) end
    end
    table.sort(players, function(a, b) return a.Id < b.Id end)
    if #players == 0 then
        ended = true
        return
    end

    -- the shared Castle belongs to the lowest-id player, at the midpoint of all Town Centers
    local host = players[1]
    for _, u in ipairs(host:UnitsOfType(CASTLE_ID)) do
        pre_castles[u.Id] = true
    end
    castle_target_x, castle_target_y = compute_castle_spot()
    castle_x, castle_y = castle_target_x, castle_target_y
    safe_create(host, CASTLE_ID, castle_target_x, castle_target_y)
    relocate_players(castle_target_x, castle_target_y)
    equip_players(castle_target_x, castle_target_y, get_config().start_bonus, get_config().start_age)

    register_shop()
    register_trades()
    local mark = 1 + (horde_owner and horde_owner.Id or 0)
    for _, p in ipairs(Game.Players()) do p:SetResource(RES_STATE, mark) end

    local cfg = get_config()
    start_time = math.max(0, Game.Time or 0)
    next_round_at = start_time + cfg.grace_seconds
    set_bar(0, cfg.grace_seconds)

    commander("Last Stand (" .. cfg.difficulty .. "): defend the Castle together" ..
        (cfg.rounds > 0 and (" for " .. cfg.rounds .. " rounds") or " for as long as you can") ..
        ". Round 1 in " .. cfg.grace_seconds .. "s. Spend Points at the Castle (page 2).")
end)

Game.OnUnitDied(function(id)
    if not active or ended then return end
    if castle_seen and id == castle_id then
        trigger_defeat("The Castle has been destroyed!")
    end
end)

Game.OnSecond(function()
    if not active or ended then return end

    local cfg = get_config()
    local now = math.max(0, Game.Time or 0)
    local host = players[1]

    -- 1. Track or verify the Castle objective
    if host then
        local castles = host:UnitsOfType(CASTLE_ID)
        if not castle_seen then
            for _, u in ipairs(castles) do
                if not pre_castles[u.Id] then
                    castle_id = u.Id
                    castle_x, castle_y = u.X, u.Y
                    castle_seen = true
                    break
                end
            end
            if not castle_seen and not castle_retry_done and (now - start_time) >= 12 then  -- multiplayer runs the first commands ~4 s late
                castle_retry_done = true
                local tc = find_player_tcs(host)[1]
                local rx = tc and (tc.X + 6) or (castle_target_x + 4)
                local ry = tc and (tc.Y + 6) or (castle_target_y - 4)
                castle_target_x, castle_target_y = math.max(4, rx), math.max(4, ry)
                safe_create(host, CASTLE_ID, castle_target_x, castle_target_y)
            end
        else
            local alive = false
            for _, u in ipairs(castles) do
                if u.Id == castle_id then
                    castle_x, castle_y = u.X, u.Y
                    alive = true
                    break
                end
            end
            if not alive then
                trigger_defeat("The Castle has fallen!")
                return
            end
        end
    end

    -- 2. Spawn the due groups, then look at the Horde: a unit that vanished is a kill
    run_spawn_queue(now)
    hide_spawn_points(now)
    local cur, alive = scan_horde()
    local kills = 0
    for id in pairs(horde_prev) do
        if not cur[id] then kills = kills + 1 end
    end
    if current_round == 0 then kills = 0 end   -- the enemy player's map start being removed is no kill
    horde_prev = {}
    for id in pairs(cur) do horde_prev[id] = true end
    if kills > 0 then
        for _, p in ipairs(players) do
            p:AddResource(RES_POINTS, kills * POINTS_PER_KILL) -- everyone gets the same
        end
    end

    -- 3. Horde brain every 5 s; computer allies fight the Horde too (they ignore Gaia units on
    --    their own). Commands for other humans' units are not sent from this machine.
    if alive > 0 and now - last_ai_at >= 5 then
        last_ai_at = now
        horde_ai(cur, now)
        if not horde_owner then ally_assist(cur) end   -- a real enemy is fought by the AI on its own
    end

    -- 4. Round flow: break -> fight -> break ...
    if state == "fight" then
        if #spawn_queue == 0 and now >= spawn_done_at + 5 and alive == 0 then
            rounds_survived = current_round
            for _, p in ipairs(players) do
                p:AddResource(RES_POINTS, POINTS_PER_ROUND)
            end
            if cfg.rounds > 0 and current_round >= cfg.rounds then
                trigger_victory(cfg.rounds)
                return
            end
            state = "break"
            next_round_at = now + cfg.break_seconds
            commander("Round " .. current_round .. " survived! (+" .. POINTS_PER_ROUND .. " Points) Next round in " ..
                cfg.break_seconds .. "s.")
            Sound.Play("Objective")
            if current_round % 4 == 0 then -- bonus drop every 4th round
                for _, p in ipairs(players) do
                    p:AddFood(200)
                    p:AddWood(200)
                    p:AddGold(100)
                end
                commander("A supply caravan arrives! (+200 Food, +200 Wood, +100 Gold for everyone)")
                Sound.Play("Tribute")
            end
        end
    elseif now >= next_round_at then
        start_round(now)
    end

    -- shop prices follow the local player's Age (also an age set before the first age-up event)
    local me = Game.LocalPlayer
    if me and tier(me) ~= shop_tier then register_shop() end

    -- 5. Map vision bought at the Castle ends after its last round (the value is a synced resource:
    --    the purchase ran on the buyer's machine only)
    if players[1] then vision_until = math.floor((players[1]:GetResource(RES_VISION) or 0) + 0.5) end
    if vision_until > 0 and current_round >= vision_until and state == "break" and not vision_ending then
        vision_ending = true        -- the resource change arrives a turn later: do this once
        vision_remove()
        set_vision(0)
        commander("The Scout Network is called back: the map is in the fog again.")
    end
    if vision_until <= 0 then vision_ending = false end
    update_counter(alive)

    -- 6. Top bar: Round and break countdown
    local secs = (state == "break") and math.max(0, math.ceil(next_round_at - now)) or 0
    set_bar(current_round, secs)
end)
