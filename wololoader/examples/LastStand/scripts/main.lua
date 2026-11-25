-- Last Stand 2.0: co-op "zombies" survival lobby game mode ("last_stand").
-- All players (humans and bots) are on one team (mod.json "teams": "all") and defend a shared Castle
-- against the Horde: Gaia units spawned in rounds that cannot be captured ({ Capturable = false }).
-- Rules run only when Game.Mode() == "last_stand".
-- Multiplayer-safe: everything that changes the game goes through lockstep commands, and all
-- decisions (spawn points, kills, round end) use only game time and shared game state, never
-- math.random (the only random part is the Mystery Box, and only the clicker's machine runs it).

local S = MOD.settings or {}

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
local TECHS_JUGGERNAUT = { 74, 76, 77, 81, 82, 80, 211, 212, 219 }
local TECHS_SPEED_COLA = { 213, 249, 315, 202, 203, 221, 55, 182, 39 }
local TECHS_PACK_A_PUNCH = {
    207, 24, 25, 222,   -- Long Swordsman, Two-Handed, Champion, Man-at-Arms
    197, 429,           -- Pikeman, Halberdier
    98,                 -- Elite Skirmisher
    100, 237,           -- Crossbowman, Arbalest
    209, 265, 428,      -- Cavalier, Paladin, Hussar
    67, 68, 75, 199, 200, 201, -- Forging, Iron Casting, Blast Furnace, Fletching, Bodkin Arrow, Bracer
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
local players = {}          -- every player (humans and bots), without Gaia

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

local function get_config()
    return {
        rounds = clamp(S.rounds, 0, 100, 0),                -- 0 = endless
        break_seconds = clamp(S.break_seconds, 5, 120, 20),
        grace_seconds = clamp(S.grace_seconds, 10, 300, 90),
        difficulty = S.difficulty or "Normal",
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

-- ---------------------------------------------------------------------------------------------
-- Castle shop (the Castle's page 2; its own page 1 buttons stay untouched)
-- ---------------------------------------------------------------------------------------------

local function has_perk(p, name)
    return perks[p.Id] and perks[p.Id][name] or false
end

local function research_all(p, list)
    for _, tech in ipairs(list) do
        local ok, err = pcall(p.Research, p, tech)
        if not ok then print("LastStand: Research " .. tech .. " failed: " .. tostring(err)) end
    end
end

-- Mystery Box prizes. Each one is a function(bx, by, buyer).
local TEDDY_LINES = {
    "The box giggles. You got a Teddy Bear. It does nothing. It is very cute.",
    "A Teddy Bear! The Horde is not impressed.",
    "Teddy Bear! Hug it and hope for the best.",
    "The box spits out a Teddy Bear. Maybe next time.",
}
local BOX_PRIZES = {
    { w = 25, run = function(bx, by, buyer)
        for i = 1, 6 do
            local off = UNIT_OFFSETS[i]
            safe_create(buyer, UNIT_KNIGHT, bx + off[1], by + off[2])
        end
        commander(player_label(buyer) .. " opened the Mystery Box: 6 Knights ride out!", buyer.Id)
    end },
    { w = 15, run = function(bx, by, buyer)
        for i = 1, 2 do
            local off = UNIT_OFFSETS[i]
            safe_create(buyer, UNIT_TREBUCHET, bx + off[1], by + off[2])
        end
        commander(player_label(buyer) .. " opened the Mystery Box: 2 Trebuchets!", buyer.Id)
    end },
    { w = 20, run = function(_, _, buyer)
        local t = BOX_TECHS[rand(1, #BOX_TECHS)]
        pcall(buyer.Research, buyer, t[1])
        commander(player_label(buyer) .. " opened the Mystery Box: free " .. t[2] .. "!", buyer.Id)
    end },
    { w = 20, run = function(_, _, buyer)
        buyer:AddGold(1000)
        commander(player_label(buyer) .. " opened the Mystery Box: +1000 Gold!", buyer.Id)
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
local SHOP_ITEMS = {
    {
        name = "LastStandMercenaries", button = 1, icon = "hire_mercenaries", fallback_icon = 32, cost = 300,
        text = "Hire Mercenaries (300 Points)",
        help = "Cost: 300 Points. Summon 5 Men-at-Arms beside the Castle.",
        run = function(bx, by, buyer)
            for i = 1, 5 do
                local off = UNIT_OFFSETS[i]
                safe_create(buyer, UNIT_MAA, bx + off[1], by + off[2])
            end
            commander(player_label(buyer) .. " hired 5 Mercenaries!", buyer.Id)
            Sound.Play("Objective")
        end,
    },
    {
        name = "LastStandArchers", button = 2, icon = "archer_company", fallback_icon = 32, cost = 400,
        text = "Archer Company (400 Points)",
        help = "Cost: 400 Points. Summon 6 Archers beside the Castle.",
        run = function(bx, by, buyer)
            for i = 1, 6 do
                local off = UNIT_OFFSETS[i]
                safe_create(buyer, UNIT_ARCHER, bx + off[1], by + off[2])
            end
            commander(player_label(buyer) .. " deployed an Archer Company!", buyer.Id)
            Sound.Play("Objective")
        end,
    },
    {
        name = "LastStandTower", button = 3, icon = "watch_tower", fallback_icon = 32, cost = 500,
        text = "Build Watch Tower (500 Points)",
        help = "Cost: 500 Points. Construct a Watch Tower beside the Castle.",
        run = function(bx, by, buyer)
            tower_count = tower_count + 1
            local off = TOWER_OFFSETS[((tower_count - 1) % #TOWER_OFFSETS) + 1]
            safe_create(buyer, WATCH_TOWER_ID, bx + off[1], by + off[2])
            commander(player_label(buyer) .. " raised a Watch Tower by the Castle!", buyer.Id)
            Sound.Play("Objective")
        end,
    },
    {
        name = "LastStandSupplies", button = 4, icon = "supply_drop", fallback_icon = 49, cost = 200,
        text = "Supply Drop (200 Points)",
        help = "Cost: 200 Points. Gain +200 Food, +200 Wood, and +100 Gold.",
        run = function(_, _, buyer)
            buyer:AddFood(200)
            buyer:AddWood(200)
            buyer:AddGold(100)
            commander(player_label(buyer) .. " called a Supply Drop! (+200 Food, +200 Wood, +100 Gold)", buyer.Id)
            Sound.Play("Tribute")
        end,
    },
    {
        name = "LastStandMysteryBox", button = 5, icon = "mystery_box", fallback_icon = 32, cost = 950,
        text = "Mystery Box (950 Points)",
        help = "Cost: 950 Points. A random prize: 6 Knights, 2 Trebuchets, a free Imperial tech, +1000 Gold... or a Teddy Bear.",
        run = function(bx, by, buyer)
            Sound.Play("Relic")
            open_mystery_box(bx, by, buyer)
        end,
    },
    {
        name = "LastStandJuggernaut", button = 6, icon = "juggernaut", fallback_icon = 32, cost = 2500, perk = true,
        text = "Juggernaut (2500 Points)",
        help = "Perk, once per player. Cost: 2500 Points. Researches all infantry, cavalry and archer armour upgrades.",
        run = function(_, _, buyer)
            research_all(buyer, TECHS_JUGGERNAUT)
            commander(player_label(buyer) .. " drank Juggernaut! Armour up.", buyer.Id)
            Sound.Play("TownRecall")
        end,
    },
    {
        name = "LastStandSpeedCola", button = 7, icon = "speed_cola", fallback_icon = 32, cost = 3000, perk = true,
        text = "Speed Cola (3000 Points)",
        help = "Perk, once per player. Cost: 3000 Points. Researches Wheelbarrow, Hand Cart, Conscription, Husbandry and the gathering speed upgrades.",
        run = function(_, _, buyer)
            research_all(buyer, TECHS_SPEED_COLA)
            commander(player_label(buyer) .. " drank Speed Cola! Faster work, faster training.", buyer.Id)
            Sound.Play("TownRecall")
        end,
    },
    {
        name = "LastStandPackAPunch", button = 8, icon = "pack_a_punch", fallback_icon = 32, cost = 5000, perk = true,
        text = "Pack-a-Punch (5000 Points)",
        help = "Perk, once per player. Cost: 5000 Points. Upgrades every unit line (Champion, Halberdier, Elite Skirmisher, Arbalest, Paladin...) and the blacksmith attack techs.",
        run = function(_, _, buyer)
            research_all(buyer, TECHS_PACK_A_PUNCH)
            commander(player_label(buyer) .. " Pack-a-Punched the army!", buyer.Id)
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

local function register_shop()
    if not Game.AddCommand then return end
    for _, item in ipairs(SHOP_ITEMS) do
        add_command_safe({
            building = CASTLE_ID,
            page = 2,
            button = item.button,
            icon = item.icon,
            name = item.name,
            text = item.text,
            help = item.help,
            condition = item.name,
            condition_action = "disable",
        }, item.fallback_icon)
    end
end

local function unregister_shop()
    if not Game.RemoveCommand then return end
    for _, item in ipairs(SHOP_ITEMS) do
        Game.RemoveCommand(item.name)
    end
end

for _, item in ipairs(SHOP_ITEMS) do
    Game.CommandCondition(item.name, function(building)
        if not active or ended then return false end
        local buyer = Game.LocalPlayer or (building and building.Owner)
        if not buyer then return false end
        if item.perk and has_perk(buyer, item.name) then return false end
        return (buyer:GetResource(RES_POINTS) or 0) >= item.cost
    end)

    Game.OnCommand(item.name, function(building)
        if not active or ended then return end
        local buyer = Game.LocalPlayer or (building and building.Owner)
        if not buyer then return end
        if item.perk and has_perk(buyer, item.name) then return end
        if (buyer:GetResource(RES_POINTS) or 0) < item.cost then return end
        buyer:AddResource(RES_POINTS, -item.cost)
        if item.perk then
            perks[buyer.Id] = perks[buyer.Id] or {}
            perks[buyer.Id][item.name] = true
        end
        local bx = (building and building.X) or castle_x or castle_target_x or 20
        local by = (building and building.Y) or castle_y or castle_target_y or 20
        item.run(bx, by, buyer)
    end)
end

-- ---------------------------------------------------------------------------------------------
-- The Horde: what a round contains
-- ---------------------------------------------------------------------------------------------

-- Normal mix: { unit, label, weight, first round, last round (nil = no end) }
local MIX = {
    { UNIT_MILITIA,   "Militia",   50, 1, 5 },
    { UNIT_SPEARMAN,  "Spearmen",  30, 1, 8 },
    { UNIT_SCOUT,     "Scouts",    15, 3, nil },  -- the fast "runners"
    { UNIT_ARCHER,    "Archers",   20, 4, nil },
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

local function spawn_points(r)
    local cx, cy, hx, hy = base_bounds()
    local count = math.min(8, 4 + r // 4)
    local base = rnd() * 2 * math.pi
    local pts = {}
    for i = 1, count do
        local a = base + (i - 1) * 2 * math.pi / count + (rnd() - 0.5) * 0.4
        local d = 25 + rnd() * 15
        -- unknown map size: only the lower edge can be clamped (3). A point beyond the upper edge is
        -- not detected here (the API has no map size), see the report.
        pts[i] = {
            math.max(3, cx + math.cos(a) * (hx + d)),
            math.max(3, cy + math.sin(a) * (hy + d)),
        }
    end
    return pts
end

local function start_round(now)
    local cfg = get_config()
    current_round = current_round + 1
    local r = current_round
    rng_state = (r * 7919 + 31337) & 0x7fffffff

    local groups, boss_msg = build_round(r, cfg.mult)
    local pts = spawn_points(r)

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
    local gaia = Game.Player(0)
    local rest = {}
    for _, c in ipairs(spawn_queue) do
        if c.at <= now then
            local cols = 3
            for i = 1, c.n do
                local dx = ((i - 1) % cols - 1) * 1.3
                local dy = ((i - 1) // cols) * 1.3
                safe_create(gaia, c.id, c.x + dx, c.y + dy, { Capturable = false })
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
    local gaia = Game.Player(0)
    local cur, n = {}, 0
    for _, t in ipairs(HORDE_TYPES) do
        for _, u in ipairs(gaia:UnitsOfType(t)) do
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

local function horde_ai(cur, now)
    local reps = build_targets()
    if #reps == 0 then return end
    local sent = 0
    for id, u in pairs(cur) do
        if sent >= 80 then break end -- at most 80 orders per tick (the command queue is limited)
        local best, bd = nil, 1e18
        for i = 1, #reps do
            local dx, dy = reps[i].x - u.X, reps[i].y - u.Y
            local d = dx * dx + dy * dy
            if d < bd then best, bd = reps[i], d end
        end
        local o = orders[id]
        local go = false
        if not o then
            go = true                                   -- fresh Horde unit
        else
            local mx, my = u.X - o.x, u.Y - o.y
            if mx * mx + my * my > 0.36 then
                go = (now - o.at >= 20)                 -- walking: retarget now and then
            else
                go = (now - o.at >= 10)                 -- standing (idle or stuck): attack again
            end
            o.x, o.y = u.X, u.Y
        end
        if go and best then
            -- Gaia units do not fight on their own: an attack order makes them hit the target (and the
            -- players' defences answer); a plain move is only the fallback.
            if not (best.id and u.Attack and pcall(u.Attack, u, best.id)) then
                pcall(u.MoveTo, u, best.x, best.y)
            end
            orders[id] = { x = u.X, y = u.Y, at = now }
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

    if not active then
        unregister_shop()
        return
    end

    if not Game.LocalPlayer then
        ended = true
        return
    end

    for _, p in ipairs(Game.Players()) do
        players[#players + 1] = p
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

    register_shop()

    local cfg = get_config()
    start_time = math.max(0, Game.Time or 0)
    next_round_at = start_time + cfg.grace_seconds
    set_bar(0, cfg.grace_seconds)

    commander("Last Stand (" .. cfg.difficulty .. "): defend the Castle together" ..
        (cfg.rounds > 0 and (" for " .. cfg.rounds .. " rounds") or " for as long as you can") ..
        ". Round 1 in " .. cfg.grace_seconds .. "s. Click the Castle (page 2) to spend Points.")
    Sound.Play("Objective")
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
            if not castle_seen and not castle_retry_done and (now - start_time) >= 4 then
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
    local cur, alive = scan_horde()
    local kills = 0
    for id in pairs(horde_prev) do
        if not cur[id] then kills = kills + 1 end
    end
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
        ally_assist(cur)
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
                commander("Max Ammo! (+200 Food, +200 Wood, +100 Gold for everyone)")
                Sound.Play("Tribute")
            end
        end
    elseif now >= next_round_at then
        start_round(now)
    end

    -- 5. Top bar: Round and break countdown
    local secs = (state == "break") and math.max(0, math.ceil(next_round_at - now)) or 0
    set_bar(current_round, secs)
end)
