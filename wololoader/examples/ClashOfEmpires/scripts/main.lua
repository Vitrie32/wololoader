-- Clash of Empires 2.0: Clash of Clans style attack mode ("clash_of_empires").
-- 2 players only: Player 1 = Attacker (no village!), Player 2 = Defender (AI village in centre).
-- Full map visible (mod.json "reveal_map": true). No engine auto-defeat ("no_auto_defeat": true).
-- Pick a unit card (with AoE2 unit icon) from the bottom deployment bar and left-click anywhere
-- outside the enemy village walls to deploy it (plays the Town Center villager spawn sound).
-- Right-click to stop deploying and command your troops. Destroy all non-wall village buildings
-- to show the loading screen and generate a brand-new random map for the next level!

local S = MOD.settings or {}
local MODE = "clash_of_empires"

local RES_LEVEL = "coe_level"
local RES_ARMY  = "coe_army"
local RES_STATE = "coe_state"

local TOWN_CENTERS = { [109] = true, [71] = true, [141] = true, [142] = true }
local WALL_TYPES = {
    [72] = true, [117] = true, [155] = true,                          -- palisade, stone, fortified walls
    [789] = true, [790] = true, [791] = true, [793] = true, [794] = true, [795] = true, -- palisade gates + posts
    [64] = true, [78] = true, [81] = true, [88] = true, [91] = true, [95] = true,       -- stone gates + posts
    [63] = true, [67] = true, [80] = true, [85] = true, [90] = true, [92] = true,       -- fortified gates + posts
}
-- Exact unit class IDs verified from empires2_x2_p1.dat for all catalog units.
-- Strictly excludes projectiles, dead bodies, flares, and buildings so arrows/rocks NEVER freeze in mid-air!
local COMBAT_CLASSES = {
    [0] = true, [6] = true, [12] = true, [13] = true, [18] = true,
    [35] = true, [36] = true, [43] = true, [44] = true, [47] = true,
    [51] = true, [54] = true, [55] = true,
}
local NOT_ARMY = { [837] = true, [274] = true }  -- map revealers / flares (declared before is_combat_unit uses it)
local function is_combat_unit(u)
    return (not u.IsBuilding) and COMBAT_CLASSES[u.Class] and (not NOT_ARMY[u.Type])
end
local RAM_TYPES = { [440] = true, [35] = true, [422] = true, [548] = true }  -- Petard (Wall Breaker) + Rams
local FARM_ID = 50                                             -- decorative village farms (not required to win)
local SFX_CLICK = 0    -- button1.wav
local SFX_SELECT = 1   -- button2.wav
local SFX_CANCEL = 28  -- button3.wav

local GENEROSITY = { Easy = 2.0, Normal = 1.65, Hard = 1.3 }
local CARDS_PER_PAGE = 8

-- Unit catalog: icons extracted from 50730.slp into icons/units/u<id>.png
local CATALOG = {
    { id = 74,  name = "Militia",     cost = 8,  lvl = 1,  line = "sword" },
    { id = 93,  name = "Spearman",    cost = 6,  lvl = 1,  line = "spear" },
    { id = 4,   name = "Archer",      cost = 7,  lvl = 1,  line = "archer" },
    { id = 7,   name = "Skirmish",    cost = 6,  lvl = 1 },
    { id = 448, name = "Scout",       cost = 8,  lvl = 1,  line = "scout" },
    { id = 440, name = "Petard",      cost = 7,  lvl = 1,  siege = true },
    { id = 75,  name = "M-at-Arms",   cost = 9,  lvl = 3,  line = "sword" },
    { id = 692, name = "Berserk",     cost = 11, lvl = 3 },
    { id = 281, name = "Axe Throw",   cost = 10, lvl = 3 },
    { id = 125, name = "Monk",        cost = 10, lvl = 3,  siege = true },
    { id = 35,  name = "Bat. Ram",    cost = 22, lvl = 3,  siege = true, line = "ram" },
    { id = 358, name = "Pikeman",     cost = 7,  lvl = 4,  line = "spear" },
    { id = 24,  name = "Crossbow",    cost = 8,  lvl = 4,  line = "archer" },
    { id = 8,   name = "Longbow",     cost = 10, lvl = 4 },
    { id = 755, name = "Tarkan",      cost = 14, lvl = 4 },
    { id = 38,  name = "Knight",      cost = 17, lvl = 4,  line = "knight" },
    { id = 280, name = "Mangonel",    cost = 28, lvl = 4,  siege = true, line = "mango" },
    { id = 77,  name = "Longsword",   cost = 9,  lvl = 5,  line = "sword" },
    { id = 25,  name = "Teutonic",    cost = 18, lvl = 5 },
    { id = 279, name = "Scorpion",    cost = 16, lvl = 5,  siege = true },
    { id = 422, name = "Capped Ram",  cost = 26, lvl = 6,  siege = true, line = "ram" },
    { id = 546, name = "Light Cav",   cost = 8,  lvl = 6,  line = "scout" },
    { id = 239, name = "Elephant",    cost = 32, lvl = 6 },
    { id = 283, name = "Cavalier",    cost = 20, lvl = 7,  line = "knight" },
    { id = 441, name = "Hussar",      cost = 9,  lvl = 7,  line = "scout" },
    { id = 473, name = "2H Sword",    cost = 10, lvl = 8,  line = "sword" },
    { id = 492, name = "Arbalester",  cost = 9,  lvl = 8,  line = "archer" },
    { id = 5,   name = "Hand Can.",   cost = 11, lvl = 8 },
    { id = 550, name = "Onager",      cost = 34, lvl = 8,  siege = true, line = "mango" },
    { id = 331, name = "Trebuchet",   cost = 38, lvl = 8,  siege = true },
    { id = 359, name = "Halberdier",  cost = 8,  lvl = 9,  line = "spear" },
    { id = 567, name = "Champion",    cost = 12, lvl = 10, line = "sword" },
    { id = 36,  name = "Bombard",     cost = 34, lvl = 10, siege = true },
    { id = 569, name = "Paladin",     cost = 24, lvl = 11, line = "knight" },
    { id = 548, name = "Siege Ram",   cost = 28, lvl = 12, siege = true, line = "ram" },
}

local BUILD_VALUE = {
    [109] = 25, [70] = 2, [12] = 12, [87] = 12, [101] = 12, [68] = 4,
    [103] = 10, [104] = 15, [49] = 14, [209] = 14, [79] = 16, [234] = 26, [235] = 42, [236] = 60, [82] = 85,
}
local BUILD_SIZE_EVEN = { [109] = true, [70] = true, [68] = true, [103] = true, [209] = true, [49] = true, [235] = true, [236] = true, [82] = true }

-- Mode-wide combat pacing boost (applied to BOTH attacker and defender):
-- Level 1+: Arson (602, +2 building dmg), Siege Engineers (377, +20% building dmg), Forging (67),
--           Fletching (199), Squires (215) so units hit a bit harder and buildings fall faster.
-- Attacker gets strong attack + anti-building techs from Level 1 (Forging+Iron Casting+Blast Furnace,
-- Arson, Siege Engineers, Fletching+Bodkin, Squires, Bloodlines) so starting units tear down buildings fast!
local TECH_STEPS_ATT = {
    { lvl = 1, techs = { 602, 377, 67, 68, 75, 199, 200, 215, 435 } },
    { lvl = 3, techs = { 74, 211, 201, 47 } },
    { lvl = 5, techs = { 76, 212 } },
    { lvl = 7, techs = { 77, 219 } },
}
local TECH_STEPS_DEF = {
    { lvl = 1, techs = { 67, 199, 215 } },
    { lvl = 3, techs = { 68, 200, 74, 211 } },
    { lvl = 5, techs = { 75, 201, 76, 212 } },
    { lvl = 7, techs = { 77, 219, 140, 194 } },
}

-- ---------------------------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------------------------
local active, ended = false, false
local attacker, defender = nil, nil
local level = 1
local state = "idle"           -- "building" | "fight" | "loading"
local state_until = 0
local village = nil            -- { cx, cy, R, units, power_u, power_b, age, wall }
local map_tiles = 144
local budget_est = 0
local last_spend_at = -100
local last_spawn_at = -100
local multiplier = 1
local selected_entry = nil
local page = 1
local collapsed = false
local obstacles = nil
local rng_state = 1
local researched = {}
local unit_orders = {}   -- [unit id] = { x, y, at } for auto-attack AI
local def_home = {}      -- [unit id] = { x, y } guard post for village defenders
local initial_bldgs = 0  -- initial non-wall/non-farm village buildings for % destruction

local function cfg_start_level()
    local n = math.floor(tonumber(S.start_level) or 1)
    return math.max(1, math.min(30, n))
end

local function rnd()
    rng_state = (rng_state * 1103515245 + 12345) & 0x7fffffff
    return rng_state / 2147483648
end

local function chat(text)
    UI.Chat(text, { From = "Clash of Empires" })
end

local function safe_create(p, id, x, y)
    local cx = math.max(2, math.min(map_tiles - 3, x))
    local cy = math.max(2, math.min(map_tiles - 3, y))
    local ok, err = pcall(p.CreateUnit, p, id, cx, cy)
    if not ok then print("ClashOfEmpires: CreateUnit " .. tostring(id) .. " failed: " .. tostring(err)) end
    return ok
end

local function research_list(p, list)
    researched[p.Id] = researched[p.Id] or {}
    for _, t in ipairs(list) do
        if not researched[p.Id][t] then
            researched[p.Id][t] = true
            pcall(p.Research, p, t)
        end
    end
end

local function level_age(l)
    if l <= 2 then return 0 elseif l <= 5 then return 1 elseif l <= 9 then return 2 end
    return 3
end

local function level_wall(l)
    if l <= 1 then return nil elseif l <= 3 then return 72 elseif l <= 6 then return 117 end
    return 155
end

-- Matching oriented Gate IDs (verified in empires2_x2_p1.dat by size_radius & annex axes):
-- ns = X-axis (North/South wall, size 1.0x0.5), we = Y-axis (West/East wall, size 0.5x1.0)
local function level_gates(l)
    if l <= 1 then return nil, nil
    elseif l <= 3 then return 789, 793   -- Palisade Gate (PGTAA / PGTBA)
    elseif l <= 6 then return 64, 88     -- Stone Gate (GTAA2 / GTBA2)
    else return 63, 85 end               -- Fortified Gate (GTAA3 / GTBA3)
end

local function level_tower(l)
    if l < 3 then return nil elseif l <= 5 then return 79 elseif l <= 8 then return 234
    elseif l <= 10 then return 235 end
    return 236   -- Bombard Tower at Level 11+
end

local function advance_tech(p, l, is_attacker)
    pcall(p.SetAge, p, level_age(l))
    local steps = is_attacker and TECH_STEPS_ATT or TECH_STEPS_DEF
    for _, step in ipairs(steps) do
        if l >= step.lvl then research_list(p, step.techs) end
    end
end

-- ---------------------------------------------------------------------------------------------
-- Organic Clash-of-Clans Village Generator (4 distinct layouts + real AoE2 Gates!)
-- ---------------------------------------------------------------------------------------------
local function defender_pool(l)
    local pool = {}
    for _, e in ipairs(CATALOG) do
        if e.lvl <= l and not e.siege then
            pool[#pool + 1] = e
            if e.lvl >= l - 2 then pool[#pool + 1] = e end
        end
    end
    return pool
end

-- Footprints (half-radius for collision spacing so buildings never overlap)
local BUILD_RAD = {
    [109] = 2.6, [82] = 2.6, [12] = 2.0, [87] = 2.0, [101] = 2.0,
    [103] = 2.0, [104] = 2.0, [49] = 2.2, [209] = 2.2, [68] = 1.6, [70] = 1.5, [79] = 1.3, [234] = 1.3, [235] = 1.4, [236] = 1.4,
}

local function building_plan(l)
    local core, outer = {}, {}
    if l >= 2 then core[#core + 1] = 12 end                         -- Barracks
    if l >= 3 then core[#core + 1] = 87 end                         -- Archery Range
    if l >= 4 then core[#core + 1] = 101; outer[#outer + 1] = 103 end -- Stable, Blacksmith
    outer[#outer + 1] = 68                                          -- Mill + surrounding Farms from Level 1!
    if l >= 6 then core[#core + 1] = 82; core[#core + 1] = 104 end  -- Castle + Monastery (Castle Age!)
    if l >= 8 then core[#core + 1] = 235; core[#core + 1] = 209 end -- Inner Keep + University
    if l >= 9 then core[#core + 1] = 82; core[#core + 1] = 49 end   -- 2nd Castle + Siege Workshop
    if l >= 13 then core[#core + 1] = 82 end                        -- 3rd Castle (Citadel)
    local houses = math.min(10, 2 + l // 2)
    for h = 1, houses do
        if h <= 2 or l <= 2 then core[#core + 1] = 70 else outer[#outer + 1] = 70 end
    end
    return core, outer
end

local function place(x, even)
    if even then return math.floor(x + 0.5) end
    return math.floor(x) + 0.5
end

local function can_place(x, y, rad, placed)
    for _, b in ipairs(placed) do
        local dx, dy = x - b.x, y - b.y
        local need = rad + b.r + 0.7
        if dx * dx + dy * dy < need * need then return false end
    end
    return true
end

local function build_village(l)
    local p = defender
    local gcount = Game.Player(0) and #Game.Player(0):Units() or 0
    rng_state = l * 7919 + map_tiles * 31 + gcount * 17 + math.floor((Game.Time or 0) * 13) + 29

    -- Slightly offset center inside the central clearing so it's not robotically identical
    local cx = map_tiles / 2 + (rnd() - 0.5) * 6.0
    local cy = map_tiles / 2 + (rnd() - 0.5) * 6.0

    -- Pick one of 4 layout styles: 1=Classic Keep, 2=Bastion Fort, 3=Citadel & Suburbs, 4=Rect Courtyard
    local style = 1 + math.floor(rnd() * 4)
    local base_r = (l <= 3) and 8 or 12
    local rx, ry = base_r, base_r
    if style == 3 then
        rx, ry = math.max(7, base_r - 2), math.max(7, base_r - 2)   -- tighter inner keep + outer suburbs
    elseif style == 4 then
        if rnd() < 0.5 then rx, ry = base_r + 2, math.max(7, base_r - 2)
        else rx, ry = math.max(7, base_r - 2), base_r + 2 end
    end
    local R = math.max(rx, ry)

    local power_b, power_u = 0, 0
    local placed = {}

    advance_tech(p, l, false)
    advance_tech(attacker, l, true)

    local function try_put(id, x, y)
        local rad = BUILD_RAD[id] or 1.8
        local even = BUILD_SIZE_EVEN[id]
        local px, py = place(x, even), place(y, even)
        if not can_place(px, py, rad, placed) then return false end
        safe_create(p, id, px, py)
        placed[#placed + 1] = { x = px, y = py, r = rad }
        power_b = power_b + (BUILD_VALUE[id] or 5)
        return true
    end

    -- Town Center near center (with small organic offset)
    try_put(109, cx + (rnd() - 0.5) * 2.5, cy + (rnd() - 0.5) * 2.5)

    local core, outer = building_plan(l)
    -- Place core buildings organically inside the compound (collision-checked, random jitter)
    local in_rx, in_ry = rx - 2.6, ry - 2.6
    for _, id in ipairs(core) do
        for _ = 1, 40 do
            local bx = cx + (rnd() * 2 - 1) * in_rx
            local by = cy + (rnd() * 2 - 1) * in_ry
            if try_put(id, bx, by) then break end
        end
    end

    -- Improved grid-based wall & gate builder:
    -- Uses an integer tile map so walls never overlap gates or duplicate tiles, aligns 4-tile
    -- gates cleanly onto the exact gap, adds corner tower bastions (Lv 4+), and builds a true
    -- double-layered Inner Keep + Outer Bailey fortress at Lv 7+!
    local wall = level_wall(l)
    local gate_ns, gate_we = level_gates(l)
    local tower = level_tower(l)
    local gx0, gy0 = math.floor(cx), math.floor(cy)
    local rxi, ryi = math.floor(rx), math.floor(ry)

    local wall_grid = {}   -- ["x,y"] = "wall" | "gate"
    local gates_to_place = {}

    local function mark_rect_walls(rx_w, ry_w, gate_sides)
        -- Mark gate openings first (4 tiles wide: a = -2, -1, 0, 1 -> exact center at gx0 or gy0)
        for _, side in ipairs(gate_sides) do
            if side == "N" or side == "S" then
                local wy = (side == "N") and (gy0 - ry_w) or (gy0 + ry_w)
                for a = -2, 1 do wall_grid[(gx0 + a) .. "," .. wy] = "gate" end
                if gate_ns then gates_to_place[#gates_to_place + 1] = { id = gate_ns, x = gx0, y = wy + 0.5 } end
            else
                local wx = (side == "W") and (gx0 - rx_w) or (gx0 + rx_w)
                for a = -2, 1 do wall_grid[wx .. "," .. (gy0 + a)] = "gate" end
                if gate_we then gates_to_place[#gates_to_place + 1] = { id = gate_we, x = wx + 0.5, y = gy0 } end
            end
        end
        -- Mark perimeter wall tiles where not occupied by a gate
        for a = -rx_w, rx_w do
            for _, wy in ipairs({ gy0 - ry_w, gy0 + ry_w }) do
                local k = (gx0 + a) .. "," .. wy
                if not wall_grid[k] then wall_grid[k] = "wall" end
            end
        end
        for a = -ry_w + 1, ry_w - 1 do
            for _, wx in ipairs({ gx0 - rx_w, gx0 + rx_w }) do
                local k = wx .. "," .. (gy0 + a)
                if not wall_grid[k] then wall_grid[k] = "wall" end
            end
        end
    end

    if wall then
        local all_sides = { "N", "S", "W", "E" }
        local g1 = all_sides[1 + math.floor(rnd() * 4)]
        local opp = { N = "S", S = "N", W = "E", E = "W" }
        local adj = { N = "E", E = "S", S = "W", W = "N" }
        local g2 = (style == 4) and adj[g1] or opp[g1]
        local outer_gates = (l >= 3) and { g1, g2 } or { g1 }

        mark_rect_walls(rxi, ryi, outer_gates)

        -- Level 4+: protruding corner bastion pockets around corner towers
        if l >= 4 and (style == 2 or l >= 6) then
            for _, sx in ipairs({ -1, 1 }) do
                for _, sy in ipairs({ -1, 1 }) do
                    local bx, by = gx0 + sx * (rxi + 1), gy0 + sy * (ryi + 1)
                    local pts = {
                        { bx, by }, { bx - sx, by }, { bx - 2 * sx, by },
                        { bx, by - sy }, { bx, by - 2 * sy },
                    }
                    for _, pt in ipairs(pts) do
                        local k = pt[1] .. "," .. pt[2]
                        if not wall_grid[k] then wall_grid[k] = "wall" end
                    end
                end
            end
        end

        -- Level 7+: true double-layered Inner Keep wall around the Town Center!
        if l >= 7 and rxi >= 10 and ryi >= 10 then
            local inner_gate = adj[g1]
            mark_rect_walls(5, 5, { inner_gate })
        end

        -- Spawn all wall segments and gates cleanly from the grid
        for k, kind in pairs(wall_grid) do
            if kind == "wall" then
                local sx, sy = k:match("^(-?%d+),(-?%d+)$")
                if sx and sy then
                    safe_create(p, wall, tonumber(sx) + 0.5, tonumber(sy) + 0.5)
                end
            end
        end
        for _, g in ipairs(gates_to_place) do
            safe_create(p, g.id, g.x, g.y)
        end
    end

    -- Towers at the 4 inner corners (protected by walls / bastions)
    if tower then
        local tspots = {
            { -rxi + 1.5, -ryi + 1.5 }, { rxi - 1.5, -ryi + 1.5 },
            { -rxi + 1.5,  ryi - 1.5 }, { rxi - 1.5,  ryi - 1.5 },
        }
        for _, t in ipairs(tspots) do
            try_put(tower, gx0 + t[1], gy0 + t[2])
        end
    end

    -- Helper: place 3-4 real AoE2 Farms (50) snugly around a Mill (68).
    -- A Mill is 2x2 and a Farm is 3x3, so adjacent center-to-center distance is 2.5 tiles.
    -- Skip checking against the Mill itself so the adjacent farms always spawn reliably!
    local function add_farms_around(mx, my)
        local offsets = {
            { 2.5, 0 }, { -2.5, 0 }, { 0, 2.5 }, { 0, -2.5 },
            { 2.5, 2.5 }, { -2.5, 2.5 }, { 2.5, -2.5 }, { -2.5, -2.5 },
        }
        local want = 3 + (l >= 4 and 1 or 0)
        local made = 0
        for _, o in ipairs(offsets) do
            if made >= want then break end
            local fx, fy = math.floor(mx + o[1]) + 0.5, math.floor(my + o[2]) + 0.5
            local ok = true
            for _, b in ipairs(placed) do
                -- Ignore the Mill right at (mx, my)
                if math.abs(b.x - mx) > 0.2 or math.abs(b.y - my) > 0.2 then
                    local dx, dy = fx - b.x, fy - b.y
                    local need = 1.45 + b.r
                    if dx * dx + dy * dy < need * need then ok = false; break end
                end
            end
            if ok then
                safe_create(p, FARM_ID, fx, fy)
                placed[#placed + 1] = { x = fx, y = fy, r = 1.45 }
                made = made + 1
            end
        end
    end

    -- Outer buildings (Mill + Farms, Houses, Blacksmith): placed organically around the village
    for _, id in ipairs(outer) do
        local put = false
        local px_used, py_used = nil, nil
        if id ~= 68 and style ~= 3 then
            for _ = 1, 20 do
                local bx, by = cx + (rnd() * 2 - 1) * in_rx, cy + (rnd() * 2 - 1) * in_ry
                if try_put(id, bx, by) then
                    put = true; px_used, py_used = placed[#placed].x, placed[#placed].y; break
                end
            end
        end
        if not put then
            for _ = 1, 40 do
                local ang = rnd() * 2 * math.pi
                local dist = (id == 68 and (R + 4.5) or (R + 3.0)) + rnd() * 2.5
                if try_put(id, cx + math.cos(ang) * dist, cy + math.sin(ang) * dist) then
                    put = true; px_used, py_used = placed[#placed].x, placed[#placed].y; break
                end
            end
        end
        if put and id == 68 and px_used then
            add_farms_around(px_used, py_used)
        end
    end

    -- Defenders stationed inside the courtyard and guarding outside the gates
    local pool = defender_pool(l)
    local n_units = math.floor(3 + l * 2.2)
    for i = 1, n_units do
        local e = pool[1 + math.floor(rnd() * #pool)]
        local x, y
        if i % 2 == 0 then
            local ang = rnd() * 2 * math.pi
            local d = 2.0 + rnd() * math.max(2.0, math.min(rx, ry) - 3.0)
            x, y = cx + math.cos(ang) * d, cy + math.sin(ang) * d
        else
            local ang = rnd() * 2 * math.pi
            x, y = cx + math.cos(ang) * (R + 2.2), cy + math.sin(ang) * (R + 2.2)
        end
        safe_create(p, e.id, x, y)
        power_u = power_u + e.cost
    end

    return {
        cx = cx, cy = cy, R = R + (style == 3 and 3 or 0), units = n_units,
        power_u = power_u, power_b = power_b, age = level_age(l), wall = wall,
    }
end

local function level_budget(v)
    local gen = GENEROSITY[S.difficulty or "Normal"] or 1.25
    return math.floor(gen * (1.0 * v.power_u + 0.7 * v.power_b) + 65)
end

-- ---------------------------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------------------------
-- Returns (remaining_buildings, tc_alive, destruction_pct)
local function village_status()
    local n, tc = 0, false
    for _, u in ipairs(defender:Units()) do
        if u.IsBuilding and u.Built and not WALL_TYPES[u.Type] and u.Type ~= FARM_ID then
            n = n + 1
            if TOWN_CENTERS[u.Type] then tc = true end
        end
    end
    if initial_bldgs < n then initial_bldgs = n end
    local pct = (initial_bldgs > 0) and math.max(0, math.min(100, math.floor((1 - n / initial_bldgs) * 100 + 0.5))) or 0
    return n, tc, pct
end

-- Keep the defender AI completely passive economically: 0 resources, no villagers, no AI-built foundations!
local function lock_defender_economy()
    if not defender then return end
    for r = 0, 3 do
        if (defender:GetResource(r) or 0) > 0 then pcall(defender.SetResource, defender, r, 0) end
    end
    for _, u in ipairs(defender:Units()) do
        if u.ClassName == "Villager" or (u.IsBuilding and not u.Built) then
            pcall(u.Delete, u)
        end
    end
end

local function army_count(p)
    local n = 0
    for _, u in ipairs(p:Units()) do
        if is_combat_unit(u) then n = n + 1 end
    end
    return n
end

-- Higher-tier units automatically replace their lower tier in the same upgrade line
-- (e.g. Archer -> Crossbow -> Arbalester, Militia -> M-at-Arms -> Longsword -> 2H Sword -> Champion)
local function unlocked_catalog(l)
    local out, line_idx = {}, {}
    for _, e in ipairs(CATALOG) do
        if e.lvl <= l then
            if e.line then
                local pos = line_idx[e.line]
                if pos then
                    out[pos] = e
                else
                    out[#out + 1] = e
                    line_idx[e.line] = #out
                end
            else
                out[#out + 1] = e
            end
        end
    end
    return out
end

local function min_cost(l)
    local best = 1e9
    for _, e in ipairs(CATALOG) do
        if e.lvl <= l and e.cost < best then best = e.cost end
    end
    return best
end

local function collect_obstacles()
    obstacles = {}
    local g = Game.Player(0)
    if g then
        for _, u in ipairs(g:Units()) do obstacles[#obstacles + 1] = { u.X, u.Y } end
    end
end

local function is_blocked(x, y)
    if not obstacles then return false end
    for i = 1, #obstacles do
        local dx, dy = x - obstacles[i][1], y - obstacles[i][2]
        if dx * dx + dy * dy < 1.3 then return true end
    end
    return false
end

-- ---------------------------------------------------------------------------------------------
-- Clash of Clans Bottom Troop Bar (icon above name + cost, clear of top-left chat!)
-- ---------------------------------------------------------------------------------------------
local bar, hdr_lbl, hint_lbl, mult_btn, prev_btn, next_btn, cancel_btn, hide_btn
local bar_bg_mode = nil
local cards = {}          -- fixed 8 card slots: { box, icon, name, cost, entry }
local load_panel, load_title, load_sub

local function set_selected(entry)
    selected_entry = entry
    if Game.SetMapClickCapture then
        Game.SetMapClickCapture(entry ~= nil and state == "fight")
    end
end

local function refresh_bar()
    if not bar then return end
    if attacker and (Game.Time or 0) - last_spend_at > 3 then
        budget_est = math.floor((attacker:GetResource(RES_ARMY) or 0) + 0.5)
    end
    local rem_b, tc_ok, pct = 0, true, 0
    if defender and state == "fight" then rem_b, tc_ok, pct = village_status() end
    local sel_tag = selected_entry and ("   |   " .. selected_entry.name .. " (Drag to Deploy)") or ""
    hdr_lbl.Text = "LEVEL " .. level .. "   |   ARMY: " .. budget_est .. " pts   |   " .. pct .. "%" .. sel_tag
    mult_btn.Text = "x" .. multiplier
    if selected_entry then cancel_btn:Show() else cancel_btn:Hide() end

    local list = unlocked_catalog(level)
    local total_pages = math.max(1, (#list + CARDS_PER_PAGE - 1) // CARDS_PER_PAGE)
    if page > total_pages then page = total_pages end
    if page < 1 then page = 1 end

    if collapsed then
        for i = 1, CARDS_PER_PAGE do cards[i].box:Hide() end
        prev_btn:Hide(); next_btn:Hide()
        hide_btn.Text = "Show"
        if bar_bg_mode ~= "mini" then
            bar.Image = "icons/ui/bar_mini.png"
            bar_bg_mode = "mini"
        end
        bar:SetSize(736, 38)
        return
    end

    hide_btn.Text = "Hide"
    if bar_bg_mode ~= "full" then
        bar.Image = "icons/ui/bar_bg.png"
        bar_bg_mode = "full"
    end
    bar:SetSize(736, 134)

    if total_pages > 1 then
        prev_btn:Show(); next_btn:Show()
        prev_btn.TextColor = (page > 1) and "#ffdf7e" or "#6e6250"
        next_btn.TextColor = (page < total_pages) and "#ffdf7e" or "#6e6250"
    else
        prev_btn:Hide(); next_btn:Hide()
    end

    local start_idx = (page - 1) * CARDS_PER_PAGE
    for i = 1, CARDS_PER_PAGE do
        local c = cards[i]
        local e = list[start_idx + i]
        c.entry = e
        if e then
            local is_sel = (selected_entry and selected_entry.id == e.id)
            local can_afford = (budget_est >= e.cost)
            local img_path = "icons/units/u" .. e.id .. ".png"
            if c.last_img ~= img_path then
                c.icon.Image = img_path
                c.last_img = img_path
            end
            local card_bg = is_sel and "icons/ui/card_sel.png" or "icons/ui/card.png"
            if c.last_bg ~= card_bg then
                c.box.Image = card_bg
                c.last_bg = card_bg
            end
            c.name.Text = e.name
            c.cost.Text = e.cost .. " pts"
            c.name.TextColor = can_afford and "#f7eed8" or "#8c7f6c"
            c.cost.TextColor = can_afford and "#ffd85c" or "#8c644c"
            c.box:Show()
        else
            c.box:Hide()
        end
    end
end

local function destroy_ui()
    set_selected(nil)
    if bar then pcall(bar.Destroy, bar) end
    if load_panel then pcall(load_panel.Destroy, load_panel) end
    bar, hdr_lbl, hint_lbl, mult_btn, prev_btn, next_btn, cancel_btn, hide_btn, bar_bg_mode = nil, nil, nil, nil, nil, nil, nil, nil, nil
    cards = {}
    load_panel, load_title, load_sub = nil, nil, nil
end

local function create_bar()
    if bar then refresh_bar(); return end
    -- Authentic AoE2 HD chiseled stone & bronze HUD deployment bar (1:1 pixel textures)
    bar = UI:CreateFrame({
        Anchor = "Bottom", Position = { 0, 192 }, Size = { 736, 134 },
        Image = "icons/ui/bar_bg.png", Color = false, BorderColor = false,
    })
    bar_bg_mode = "full"

    hdr_lbl = UI:CreateLabel({
        Parent = bar, Text = "", Position = { 14, 11 }, Size = { 515, 20 },
        TextColor = "#ffe28a", TextSize = 13, Bold = true,
    })
    cancel_btn = UI:CreateButton({
        Parent = bar, Text = "Cancel", Position = { 536, 8 }, Size = { 62, 24 },
        Color = "#6e2a22", BorderColor = "#c98574", TextColor = "#fff0e8", TextSize = 12,
    })
    cancel_btn:OnClick(function() pcall(Sound.Play, SFX_CANCEL); set_selected(nil); refresh_bar() end)
    cancel_btn:Hide()

    mult_btn = UI:CreateButton({
        Parent = bar, Text = "x1", Position = { 604, 8 }, Size = { 46, 24 },
        Color = "#5e4e3a", BorderColor = "#bfa472", TextColor = "#ffe28a", TextSize = 12, Bold = true,
    })
    mult_btn:OnClick(function()
        pcall(Sound.Play, SFX_CLICK)
        multiplier = (multiplier == 1) and 5 or ((multiplier == 5) and 10 or 1)
        refresh_bar()
    end)

    hide_btn = UI:CreateButton({
        Parent = bar, Text = "Hide", Position = { 656, 8 }, Size = { 66, 24 },
        Color = "#5e4e3a", BorderColor = "#bfa472", TextColor = "#f5ead4", TextSize = 12,
    })
    hide_btn:OnClick(function() pcall(Sound.Play, SFX_CLICK); collapsed = not collapsed; refresh_bar() end)

    prev_btn = UI:CreateButton({
        Parent = bar, Text = "<", Position = { 10, 58 }, Size = { 22, 48 },
        Color = "#574836", BorderColor = "#bfa472", TextColor = "#ffe28a", Bold = true,
    })
    prev_btn:OnClick(function()
        if page > 1 then pcall(Sound.Play, SFX_CLICK); page = page - 1; refresh_bar() end
    end)

    next_btn = UI:CreateButton({
        Parent = bar, Text = ">", Position = { 704, 58 }, Size = { 22, 48 },
        Color = "#574836", BorderColor = "#bfa472", TextColor = "#ffe28a", Bold = true,
    })
    next_btn:OnClick(function()
        local total_pages = math.max(1, (#unlocked_catalog(level) + CARDS_PER_PAGE - 1) // CARDS_PER_PAGE)
        if page < total_pages then pcall(Sound.Play, SFX_CLICK); page = page + 1; refresh_bar() end
    end)

    for i = 1, CARDS_PER_PAGE do
        local x = 38 + (i - 1) * 83
        local box = UI:CreateButton({
            Parent = bar, Text = "", Position = { x, 42 }, Size = { 79, 82 },
            Image = "icons/ui/card.png", Color = false, BorderColor = false,
        })
        local icon = UI:CreateButton({
            Parent = box, Text = "", Position = { 21, 5 }, Size = { 36, 36 },
            Color = false, BorderColor = false,
        })
        local name = UI:CreateLabel({
            Parent = box, Text = "", Position = { 4, 46 }, Size = { 71, 16 },
            Align = "Center", TextSize = 11, TextColor = "#f7eed8",
        })
        local cost = UI:CreateLabel({
            Parent = box, Text = "", Position = { 4, 61 }, Size = { 71, 15 },
            Align = "Center", TextSize = 11, Bold = true, TextColor = "#ffd85c",
        })
        local slot = { box = box, icon = icon, name = name, cost = cost, entry = nil, last_bg = "icons/ui/card.png" }
        local function on_pick()
            if not slot.entry or state ~= "fight" then return end
            if selected_entry and selected_entry.id == slot.entry.id then
                pcall(Sound.Play, SFX_CANCEL)
                set_selected(nil)
            else
                if budget_est < slot.entry.cost then
                    UI.Message("Not enough Army Points!")
                    pcall(Sound.Play, "CantDo")
                    return
                end
                pcall(Sound.Play, SFX_SELECT)
                set_selected(slot.entry)
            end
            refresh_bar()
        end
        box:OnClick(on_pick)
        icon:OnClick(on_pick)
        cards[i] = slot
    end
    refresh_bar()
end

local function show_loading_screen(title_text, sub_text)
    if bar then bar:Hide() end
    if load_panel then pcall(load_panel.Destroy, load_panel) end
    load_panel = UI:CreateFrame({
        Anchor = "Center", Position = { 0, 0 }, Size = { 480, 136 },
        Image = "icons/ui/load_bg.png", Color = false, BorderColor = false,
    })
    load_title = UI:CreateLabel({
        Parent = load_panel, Text = title_text, Position = { 20, 28 }, Size = { 440, 34 },
        Align = "Center", TextSize = 20, Bold = true, TextColor = "#ffe28a",
    })
    load_sub = UI:CreateLabel({
        Parent = load_panel, Text = sub_text, Position = { 20, 72 }, Size = { 440, 40 },
        Align = "Center", TextSize = 13, TextColor = "#eee2c8",
    })
end

-- ---------------------------------------------------------------------------------------------
-- Map-click deployment (Clash of Clans style!)
-- ---------------------------------------------------------------------------------------------
local DEPLOY_OFFSETS = {
    { 0, 0 }, { 1.1, 0 }, { -1.1, 0 }, { 0, 1.1 }, { 0, -1.1 },
    { 1.1, 1.1 }, { -1.1, 1.1 }, { 1.1, -1.1 }, { -1.1, -1.1 }, { 2.2, 0 },
}

Game.OnMapClick(function(tx, ty, btn)
    if not active or ended or state ~= "fight" or not selected_entry then return end
    if btn == "right" then
        pcall(Sound.Play, SFX_CANCEL)
        set_selected(nil)
        refresh_bar()
        return
    end
    -- Auto-detected red zone: 4 tiles outside the actual bounding box of all enemy buildings/walls
    if defender then
        local minx, maxx, miny, maxy = 1e9, -1e9, 1e9, -1e9
        local found = false
        for _, u in ipairs(defender:Units()) do
            if u.IsBuilding then
                if u.X < minx then minx = u.X end
                if u.X > maxx then maxx = u.X end
                if u.Y < miny then miny = u.Y end
                if u.Y > maxy then maxy = u.Y end
                found = true
            end
        end
        if found then
            local pad = 4.0
            if tx >= minx - pad and tx <= maxx + pad and ty >= miny - pad and ty <= maxy + pad then
                UI.Message("Too close to the enemy village! Deploy outside the village perimeter.")
                pcall(Sound.Play, "CantDo")
                return
            end
        end
    end

    local e = selected_entry
    local count = math.min(multiplier, math.floor(budget_est / e.cost))
    if count <= 0 then
        UI.Message("Not enough Army Points!")
        pcall(Sound.Play, "CantDo")
        set_selected(nil)
        refresh_bar()
        return
    end

    local total = count * e.cost
    budget_est = budget_est - total
    last_spend_at = Game.Time or 0
    last_spawn_at = last_spend_at
    attacker:AddResource(RES_ARMY, -total)

    for k = 1, count do
        local off = DEPLOY_OFFSETS[((k - 1) % #DEPLOY_OFFSETS) + 1]
        local sx, sy = tx + off[1], ty + off[2]
        if is_blocked(sx, sy) then
            for step = 1, 12 do
                local ang = step * 0.5236
                local rad = 1.2 + (step // 6) * 1.2
                local cx2, cy2 = tx + math.cos(ang) * rad, ty + math.sin(ang) * rad
                if not is_blocked(cx2, cy2) then sx, sy = cx2, cy2; break end
            end
        end
        safe_create(attacker, e.id, sx, sy)
    end
    -- Real Town Center villager spawn sound (5353.wav)
    pcall(Sound.Play, "Villager")

    if budget_est < min_cost(level) then
        set_selected(nil)
    end
    refresh_bar()
end)

-- ---------------------------------------------------------------------------------------------
-- Match lifecycle
-- ---------------------------------------------------------------------------------------------
-- Clash of Clans auto-attack AI: zero units left behind!
-- 1) Spreads troops across all village buildings (bldg_load) so they don't crowd one house.
-- 2) Never freezes a unit forever on an unreachable defender (busy_fighting expires after 3s).
-- 3) 3-stage anti-stall: if a unit hasn't moved for 2s while > 2.5 tiles from target:
--    stall 1 -> pick a different uncrowded building (or nearest wall if walled) + Attack
--    stall 2+ -> MoveTo an open waypoint 5 tiles toward target (with lateral jitter) to walk
--                around trees/crowds, then Attack on the next tick!
local function auto_attack_ai(now)
    if not attacker or not defender then return end
    local bldgs, walls, defs = {}, {}, {}
    local target_by_id = {}
    for _, u in ipairs(defender:Units()) do
        if u.IsBuilding then
            if WALL_TYPES[u.Type] then
                walls[#walls + 1] = u
                target_by_id[u.Id] = u
            elseif u.Type ~= FARM_ID then
                bldgs[#bldgs + 1] = u
                target_by_id[u.Id] = u
            end
        elseif is_combat_unit(u) then
            defs[#defs + 1] = u
            target_by_id[u.Id] = u
        end
    end
    if #bldgs == 0 and #defs == 0 then return end

    local function nearest_in(list, ux, uy, skip_id, load_tbl)
        local best, best_score = nil, 1e18
        for _, t in ipairs(list) do
            if not skip_id or t.Id ~= skip_id or #list == 1 then
                local dx, dy = t.X - ux, t.Y - uy
                local score = dx * dx + dy * dy + (load_tbl and ((load_tbl[t.Id] or 0) * 28.0) or 0)
                if score < best_score then best, best_score = t, score end
            end
        end
        return best, best_score
    end

    local bldg_load = {}
    for _, rec in pairs(unit_orders) do
        if rec.target_id and not rec.target_is_unit then
            bldg_load[rec.target_id] = (bldg_load[rec.target_id] or 0) + 1
        end
    end

    local alive = {}
    for _, u in ipairs(attacker:Units()) do
        if is_combat_unit(u) then
            alive[u.Id] = true
            local rec = unit_orders[u.Id]
            local is_breaker = RAM_TYPES[u.Type] and true or false
            local is_ranged = (u.ClassName == "Archer" or u.ClassName == "CavalryArcher" or u.ClassName == "Siege")
            local cur_t = rec and rec.target_id and target_by_id[rec.target_id] or nil

            -- Nearby enemy defender within 6 tiles (non-siege/non-petard units fight back!)
            local near_def = nil
            if u.ClassName ~= "Siege" and not is_breaker and #defs > 0 then
                local d, d2 = nearest_in(defs, u.X, u.Y)
                if d and d2 < 36.0 then near_def = d end
            end

            -- Only treat as busy_fighting if actively close to target AND recently updated (< 3s)
            local busy_fighting = false
            if rec and rec.target_is_unit and cur_t and (now - rec.at < 3.0) then
                local dx, dy = cur_t.X - u.X, cur_t.Y - u.Y
                local engage_r2 = is_ranged and 36.0 or 6.25
                if dx * dx + dy * dy <= engage_r2 then
                    busy_fighting = true
                    rec.x, rec.y = u.X, u.Y
                end
            end

            local need_order = false
            local stalls = rec and rec.stalls or 0
            if not busy_fighting then
                if not rec or not cur_t then
                    need_order = true
                    stalls = 0
                elseif rec.nudging then
                    -- Finished 1-second unstuck MoveTo nudge: immediately switch to Attack!
                    need_order = true
                elseif near_def and not rec.target_is_unit then
                    need_order = true
                    stalls = 0
                else
                    local tx, ty = cur_t.X - u.X, cur_t.Y - u.Y
                    local td2 = tx * tx + ty * ty
                    local dx, dy = u.X - rec.x, u.Y - rec.y
                    local moved2 = dx * dx + dy * dy
                    local attack_r2 = is_ranged and 36.0 or 9.0
                    if moved2 >= 0.20 then
                        rec.x, rec.y = u.X, u.Y
                        rec.at = now
                        rec.stalls = 0
                    elseif td2 > attack_r2 and now - rec.at >= 2.0 then
                        -- Not moving and too far to hit target -> stalled!
                        need_order = true
                        stalls = stalls + 1
                    elseif now - rec.at >= 5.0 then
                        need_order = true
                        stalls = stalls + 1
                    end
                end
            end

            if need_order then
                local best, best_is_unit = nil, false
                if is_breaker and #walls > 0 then
                    best = nearest_in(walls, u.X, u.Y)
                elseif near_def and stalls < 2 then
                    best, best_is_unit = near_def, true
                elseif stalls == 1 and #walls > 0 then
                    best = nearest_in(walls, u.X, u.Y)
                else
                    local skip = (stalls >= 1 and rec) and rec.target_id or nil
                    best = nearest_in(bldgs, u.X, u.Y, skip, bldg_load)
                    if not best then
                        best = nearest_in(defs, u.X, u.Y)
                        if best then best_is_unit = true end
                    end
                end

                if best then
                    if stalls >= 2 then
                        -- Unstuck nudge: walk 5 tiles toward target with slight lateral offset to clear crowds/trees
                        local dx, dy = best.X - u.X, best.Y - u.Y
                        local dist = math.sqrt(dx * dx + dy * dy)
                        if dist > 1.5 then
                            local nx, ny = dx / dist, dy / dist
                            local side = ((u.Id % 2 == 0) and 1 or -1) * 2.0
                            local wx = math.max(2, math.min(map_tiles - 3, u.X + nx * 5.0 - ny * side))
                            local wy = math.max(2, math.min(map_tiles - 3, u.Y + ny * 5.0 + nx * side))
                            pcall(u.MoveTo, u, wx, wy)
                            unit_orders[u.Id] = { x = u.X, y = u.Y, at = now, target_id = best.Id,
                                                  target_is_unit = best_is_unit, stalls = 0, nudging = true }
                        else
                            pcall(u.Attack, u, best)
                            unit_orders[u.Id] = { x = u.X, y = u.Y, at = now, target_id = best.Id,
                                                  target_is_unit = best_is_unit, stalls = 0, nudging = false }
                        end
                    else
                        if not best_is_unit then
                            bldg_load[best.Id] = (bldg_load[best.Id] or 0) + 1
                        end
                        pcall(u.Attack, u, best)
                        unit_orders[u.Id] = { x = u.X, y = u.Y, at = now, target_id = best.Id,
                                              target_is_unit = best_is_unit, stalls = stalls, nudging = false }
                    end
                end
            end
        end
    end
    for id in pairs(unit_orders) do
        if not alive[id] then unit_orders[id] = nil end
    end
end

-- Keep Player 2's defenders strictly inside/around the village (overrides AoE2 AI scouting).
-- Defenders only engage attackers that come within R+5 of the village; otherwise any defender
-- > 2 tiles from its guard post (or outside R+2.5 of village center) is immediately recalled.
local function defender_guard_ai(now)
    if not defender or not village then return end
    local cx, cy, R = village.cx, village.cy, (village.R or 9)
    local defend_r2 = (R + 5.0) * (R + 5.0)
    local leash_r2 = (R + 2.5) * (R + 2.5)

    local threats = {}
    if attacker then
        for _, u in ipairs(attacker:Units()) do
            if is_combat_unit(u) then
                local dx, dy = u.X - cx, u.Y - cy
                if dx * dx + dy * dy <= defend_r2 then
                    threats[#threats + 1] = u
                end
            end
        end
    end

    local alive = {}
    for idx, d in ipairs(defender:Units()) do
        if is_combat_unit(d) then
            alive[d.Id] = true
            local home = def_home[d.Id]
            if not home then
                local dx, dy = d.X - cx, d.Y - cy
                if dx * dx + dy * dy <= (R + 1.5) * (R + 1.5) then
                    home = { x = d.X, y = d.Y }
                else
                    local ang = (idx * 2.39996) % (2 * math.pi)
                    home = { x = cx + math.cos(ang) * math.max(2.5, R - 2.0),
                             y = cy + math.sin(ang) * math.max(2.5, R - 2.0) }
                end
                def_home[d.Id] = home
            end

            local cdx, cdy = d.X - cx, d.Y - cy
            local outside_village = (cdx * cdx + cdy * cdy > leash_r2)

            if #threats > 0 and not outside_village then
                local best, best_d2 = nil, 1e18
                for _, a in ipairs(threats) do
                    local ex, ey = a.X - d.X, a.Y - d.Y
                    local d2 = ex * ex + ey * ey
                    if d2 < best_d2 then best, best_d2 = a, d2 end
                end
                if best and (now % 2 == 0 or best_d2 > 12) then
                    pcall(d.Attack, d, best)
                end
            else
                local hx, hy = d.X - home.x, d.Y - home.y
                if outside_village or hx * hx + hy * hy > 4.0 then
                    pcall(d.MoveTo, d, home.x, home.y)
                end
            end
        end
    end
    for id in pairs(def_home) do
        if not alive[id] then def_home[id] = nil end
    end
end

local function finish_building(now)
    village = build_village(level)
    local carry = math.floor(tonumber(Game.GetSession and Game.GetSession("coe_army") or 0) or 0)
    local budget = level_budget(village)
    local total = budget + carry
    attacker:SetResource(RES_ARMY, total)
    attacker:SetResource(RES_LEVEL, level)
    budget_est = total
    last_spend_at = now
    initial_bldgs = 0
    lock_defender_economy()
    state = "fight"
    state_until = now + 5

    if load_panel then pcall(load_panel.Destroy, load_panel); load_panel = nil end
    create_bar()

    local age_name = ({ [0] = "Dark", "Feudal", "Castle", "Imperial" })[village.age]
    local wall_name = village.wall and ({ [72] = "palisade walls", [117] = "stone walls", [155] = "fortified walls" })[village.wall] or "no walls"
    chat("Level " .. level .. ": " .. age_name .. " Age, " .. village.units .. " defenders, " .. wall_name ..
         ". Army Points: " .. total .. (carry > 0 and (" (+" .. carry .. " carried over)") or "") ..
         ". Pick a unit below and click the map to deploy!")
end

local function game_over(win)
    ended = true
    set_selected(nil)
    if Game.SetUnitControlDisabled then Game.SetUnitControlDisabled(false) end
    if Game.ClearSession then Game.ClearSession() end
    if win then pcall(attacker.Win, attacker, true) else pcall(attacker.Defeat, attacker) end
end

Game.OnStart(function()
    active = (Game.Mode() == MODE)
    ended = false
    attacker, defender, village = nil, nil, nil
    state, state_until = "idle", 0
    budget_est, obstacles, multiplier, page, collapsed, researched, unit_orders, def_home, initial_bldgs = 0, nil, 1, 1, false, {}, {}, {}, 0
    destroy_ui()
    if not active then
        if Game.SetUnitControlDisabled then Game.SetUnitControlDisabled(false) end
        return
    end
    if Game.SetUnitControlDisabled then Game.SetUnitControlDisabled(true) end
    if not Game.LocalPlayer then ended = true; return end

    map_tiles = (Game.MapSize and Game.MapSize()) or 144
    attacker = Game.Player(1)
    defender = (Game.ModeEnemy and Game.ModeEnemy()) or Game.Player(2)
    if not attacker or not defender or attacker.Id == defender.Id then
        chat("Clash of Empires requires 2 players (Player 1: Attacker, Player 2: Defender).")
        ended = true
        return
    end

    -- Saved game check
    local saved_state = math.floor((attacker:GetResource(RES_STATE) or 0) + 0.5)
    local saved_level = math.floor((attacker:GetResource(RES_LEVEL) or 0) + 0.5)
    if saved_state > 0 and saved_level > 0 then
        if Game.SetModeEnemy then Game.SetModeEnemy(defender.Id) end
        level = saved_level
        budget_est = math.floor((attacker:GetResource(RES_ARMY) or 0) + 0.5)
        village = { cx = map_tiles / 2, cy = map_tiles / 2, R = (level <= 3 and 8 or 12.5) }
        state = "fight"
        state_until = (Game.Time or 0) + 4
        collect_obstacles()
        create_bar()
        chat("Clash of Empires: Resuming Level " .. level .. ".")
        return
    end

    -- Read session level if we just came from Game.Restart() after beating a level!
    local sess_lvl = Game.GetSession and Game.GetSession("coe_level")
    level = (sess_lvl and tonumber(sess_lvl) and tonumber(sess_lvl) >= 1) and math.floor(tonumber(sess_lvl)) or cfg_start_level()

    -- Remove ALL starting units & buildings for BOTH players: Attacker has NO village!
    for _, u in ipairs(attacker:Units()) do pcall(u.Delete, u) end
    for _, u in ipairs(defender:Units()) do pcall(u.Delete, u) end

    attacker:SetResource(RES_STATE, 1 + defender.Id)
    defender:SetResource(RES_STATE, 1 + defender.Id)
    collect_obstacles()

    state = "building"
    state_until = (Game.Time or 0) + 2
    show_loading_screen("PREPARING LEVEL " .. level, "Building enemy village in the center of the map...")
end)

Game.OnSecond(function()
    if not active or ended or not attacker or not defender then return end
    local now = Game.Time or 0
    if state == "building" then
        if now >= state_until then finish_building(now) end
    elseif state == "fight" then
        refresh_bar()
        lock_defender_economy()
        auto_attack_ai(now)
        defender_guard_ai(now)
        if now < state_until then return end
        local rem_b, tc_alive, pct = village_status()
        if rem_b == 0 or (not tc_alive and pct >= 75) then
            set_selected(nil)
            pcall(Sound.Play, "Objective")
            local goal = math.floor(tonumber(S.levels) or 0)
            if goal > 0 and level >= goal then
                chat("VICTORY! Level " .. level .. " village destroyed! You conquered every level!")
                game_over(true)
                return
            end
            -- Transition to next level with a brand-new random map!
            local next_lvl = level + 1
            if Game.SetSession then
                Game.SetSession("coe_level", next_lvl)
                Game.SetSession("coe_army", budget_est)
            end
            state = "loading"
            state_until = now + 2
            show_loading_screen(
                "LEVEL " .. level .. " CLEARED!",
                "Generating a new random map... Loading Level " .. next_lvl .. "..."
            )
            chat("Level " .. level .. " cleared! Generating new map for Level " .. next_lvl .. "...")
            return
        end
        -- Lose when all attacker units are dead AND cannot afford even the cheapest unit
        if now - last_spawn_at > 3 and army_count(attacker) == 0 and budget_est < min_cost(level) then
            chat("All your troops were defeated! Attack failed at Level " .. level .. ".")
            game_over(false)
        end
    elseif state == "loading" then
        if now >= state_until then
            state = "idle"
            if not (Game.Restart and Game.Restart()) then
                -- Fallback if restart unavailable (e.g. multiplayer): rebuild in place
                level = level + 1
                for _, u in ipairs(defender:Units()) do pcall(u.Delete, u) end
                state = "building"
                state_until = now + 2
            end
        end
    end
end)
