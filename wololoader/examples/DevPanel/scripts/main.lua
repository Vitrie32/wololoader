-- Dev Panel: a testing tool built only on the Wololoader Lua API (see MODDING.md).
-- Toggle: the DEV button (top left) or Ctrl+Shift+D. Meant for single player tests:
-- in multiplayer the buttons only act on your own player.

local W = 236                        -- panel width
local ROW, GAP = 26, 4
local FOOD, WOOD, STONE, GOLD = "#E07050", "#B08850", "#B0B0B0", "#E8C040"

local toggle = UI:CreateButton({ Text = "DEV", Anchor = "TopLeft", Position = { 6, 34 }, Size = { 46, 22 },
    TextSize = 13, Hotkey = "Ctrl+Shift+D" })
local panel = UI:CreateFrame({ Anchor = "TopLeft", Position = { 6, 60 }, Size = { W, 10 }, Visible = false })

local y = 6
local function label(text, opts)
    opts = opts or {}
    local l = UI:CreateLabel({ Parent = panel, Text = text, Position = { 8, y }, Size = { W - 16, opts.h or 18 },
        TextSize = opts.size or 13, Bold = opts.bold, TextColor = opts.color or "#E8C878" })
    y = y + (opts.h or 18)
    return l
end
-- A row of equal buttons: { {text, fn, color}, ... }
local function row(items)
    local n = #items
    local bw = math.floor((W - 16 - GAP * (n - 1)) / n)
    for i, it in ipairs(items) do
        local b = UI:CreateButton({ Parent = panel, Text = it[1], Position = { 8 + (i - 1) * (bw + GAP), y },
            Size = { bw, ROW - 4 }, TextSize = 12, TextColor = it[3] })
        b:OnClick(it[2])
    end
    y = y + ROW
end

local status
local function say(text) status.Text = text end
local function me() return Game.LocalPlayer end

-- Where new units appear: next to the first selected unit, else next to the Town Center (109).
local function spawn_point()
    local sel = Game.Selected()
    if sel[1] then return sel[1].X + 2, sel[1].Y + 2 end
    local p = me()
    local tc = p and (p:Units(function(u) return u.Type == 109 end)[1] or p:Units("Building")[1])
    if tc then return tc.X + 4, tc.Y + 4 end
end

local count = 1
local function spawn(id, name)
    return function()
        local x, y0 = spawn_point()
        if not x then say("Nothing selected and no Town Center") return end
        local n, cols = count, math.ceil(math.sqrt(count))        -- a square block of units
        say("Spawning " .. n .. " " .. name .. "...")
        for i = 1, n do
            local dx, dy = (i - 1) % cols, (i - 1) // cols
            me():CreateUnit(id, x + dx, y0 + dy)
            if i % 90 == 0 then Time.Wait(1) end                 -- at most 100 commands a second
        end
        say("Spawned " .. n .. " " .. name)
    end
end

-- Many commands: in chunks (the game takes at most 100 Lua commands a second).
local function in_chunks(list, fn)
    for i, v in ipairs(list) do
        fn(v)
        if i % 90 == 0 then Time.Wait(1) end
    end
end

-- ---- panel content ----
label("Wololoader Dev Panel", { bold = true, size = 14, h = 20 })
local info = label("", { color = "#FFFFFF", h = 18 })

label("Resources")
row({
    { "+1k F", function() me():AddFood(1000) say("+1000 food") end, FOOD },
    { "+1k W", function() me():AddWood(1000) say("+1000 wood") end, WOOD },
    { "+1k S", function() me():AddStone(1000) say("+1000 stone") end, STONE },
    { "+1k G", function() me():AddGold(1000) say("+1000 gold") end, GOLD },
})
row({
    { "All +10k", function() local p = me()
        p:AddFood(10000) p:AddWood(10000) p:AddStone(10000) p:AddGold(10000) say("+10000 of everything") end },
    { "Reset 0", function() local p = me()
        p:AddFood(-p.Food) p:AddWood(-p.Wood) p:AddStone(-p.Stone) p:AddGold(-p.Gold) say("Resources set to 0") end },
})

label("Ages (going back keeps what was unlocked)")
row({
    { "Dark", function() me():SetAge("Dark") say("Dark Age") end },
    { "Feudal", function() me():SetAge("Feudal") say("Feudal Age") end },
    { "Castle", function() me():SetAge("Castle") say("Castle Age") end },
    { "Imperial", function() me():SetAge("Imperial") say("Imperial Age") end },
})
label("Techs")
row({
    -- Loom, Wheelbarrow, Hand Cart, Double-Bit Axe, Bow Saw, Horse Collar, Heavy Plow,
    -- Gold Mining, Stone Mining, Town Watch
    { "Eco techs", function()
        for _, t in ipairs({ 22, 213, 249, 202, 203, 14, 13, 55, 278, 8 }) do me():Research(t) end
        say("Economy techs done") end },
    -- Blacksmith line: Forging, Iron Casting, Blast Furnace, Scale/Chain/Plate (both lines), Fletching..
    { "Army techs", function()
        for _, t in ipairs({ 67, 68, 75, 74, 76, 77, 81, 82, 80, 199, 200, 201, 211, 212, 219 }) do me():Research(t) end
        say("Blacksmith techs done") end },
})

label("Spawn (at selection or Town Center)")
local count_btn
row({
    { "Villager", spawn(83, "Villager") }, { "Monk", spawn(125, "Monk") }, { "Knight", spawn(38, "Knight") },
})
row({
    { "Archer", spawn(4, "Archer") }, { "Trebuchet", spawn(331, "Trebuchet") }, { "Siege Ram", spawn(548, "Siege Ram") },
})
UI:CreateLabel({ Parent = panel, Text = "Count (1-1000):", Position = { 8, y + 2 }, Size = { 110, 20 },
    TextSize = 12, TextColor = "#E8C878" })
count_btn = UI:CreateTextBox({ Parent = panel, Text = "1", Position = { 122, y }, Size = { W - 130, ROW - 4 },
    TextSize = 13, Numeric = true, MaxLength = 4, Placeholder = "1" })
count_btn:OnChanged(function(box, text)
    local n = tonumber(text)
    if n and n > 1000 then box.Text = "1000" n = 1000 end
    count = (n and n >= 1) and math.floor(n) or 1
end)
count_btn:OnSubmit(function(box) box.Text = tostring(count) say("Count: " .. count) end)
y = y + ROW

label("Selected units")
row({
    { "Clone", function()
        local sel = Game.Selected()
        if #sel == 0 then say("Select units first") return end
        for _, u in ipairs(sel) do
            if not u.IsBuilding and u.Owner then u.Owner:CreateUnit(u.Type, u.X + 1, u.Y + 1) end
        end
        say("Cloned " .. #sel .. " unit(s)") end },
    { "Delete", function()
        local sel = Game.Selected()
        for _, u in ipairs(sel) do u:Delete() end
        say("Deleted " .. #sel .. " unit(s)") end, "#FF8080" },
})

label("Enemies")
row({
    { "Kill army", function()
        local list = {}
        for _, p in ipairs(Game.Players()) do
            if me():IsEnemy(p) then
                for _, u in ipairs(p:Units(function(u) return not u.IsBuilding end)) do list[#list + 1] = u end
            end
        end
        say("Killing " .. #list .. " enemy unit(s)...")
        in_chunks(list, function(u) u:Delete() end)
        say("Killed " .. #list .. " enemy unit(s)") end, "#FF8080" },
    { "Raze all", function()
        local list = {}
        for _, p in ipairs(Game.Players()) do
            if me():IsEnemy(p) then
                for _, u in ipairs(p:Units()) do list[#list + 1] = u end
            end
        end
        say("Razing " .. #list .. " enemy object(s)...")
        in_chunks(list, function(u) u:Delete() end)
        say("Razed " .. #list .. " enemy object(s)") end, "#FF8080" },
})

status = label("Ctrl+Shift+D: show/hide", { color = "#A0FFA0", h = 20 })
panel.Size = { W, y + 6 }

toggle:OnClick(function()
    panel.Visible = not panel.Visible
    toggle.TextColor = panel.Visible and "#FFFF80" or "#FFFFFF"
end)

-- Live info line: game time, population, selection.
local function clock(t)
    t = math.floor(t or 0)
    return string.format("%d:%02d:%02d", t // 3600, t // 60 % 60, t % 60)
end
-- (timers are cleared when a game starts, so start this one from OnStart)
Game.OnStart(function() Time.Every(0.5, function()
    if not panel.Visible then return end
    local p = me()
    if not p then return end
    local sel = Game.Selected()
    local s = clock(Game.Time) .. "   Pop " .. (p.Population or 0) .. "   " .. (p.AgeName or "?")
    if sel[1] then s = s .. "   Sel " .. #sel .. " (" .. (sel[1].ClassName or ("type " .. sel[1].Type)) .. ")" end
    info.Text = s
end) end)
