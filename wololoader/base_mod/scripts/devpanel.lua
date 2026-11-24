-- Dev Panel & standalone Lua console (part of Wololoader Base).
-- Dev Panel toggle: DEV button (top left) or Ctrl+Shift+D.
-- Lua Console toggle: Ctrl+Shift+C or the Console button inside Dev Panel (Esc closes).
-- Available in single player, and in multiplayer only when cheats are enabled.

local W = 300
local ROW, GAP = 24, 4
local FOOD, WOOD, STONE, GOLD = "#E07050", "#B08850", "#B0B0B0", "#E8C040"
local LINE_H = 16
local MAX_OUT_ROWS = 120
local CONSOLE_MAX_ENTRIES = 400
local HISTORY_MAX = 50
local CLEAR_SENTINEL = "__WL_CLEAR__"

local function dev_allowed()
    return not Game:IsMultiplayer() or Game:CheatsEnabled()
end

local toggle = UI:CreateButton({ Text = "DEV", Anchor = "TopLeft", Position = { 6, 34 }, Size = { 46, 20 },
    TextSize = 12, Hotkey = "Ctrl+Shift+D", Visible = false })
local con_hotkey = UI:CreateButton({ Text = "", Anchor = "TopLeft", Position = { 0, 0 }, Size = { 0, 0 },
    Color = false, BorderColor = false, Hotkey = "Ctrl+Shift+C", Visible = false })
local panel = UI:CreateFrame({ Anchor = "TopLeft", Position = { 6, 56 }, Size = { W, 10 }, Visible = false })

local y = 6
local function label(text, opts)
    opts = opts or {}
    local l = UI:CreateLabel({ Parent = panel, Text = text, Position = { 8, y }, Size = { W - 16, opts.h or 15 },
        TextSize = opts.size or 11, Bold = opts.bold, TextColor = opts.color or "#E8C878" })
    y = y + (opts.h or 15)
    return l
end

local status
local function say(text, color)
    if status then
        status.TextColor = color or "#A0FFA0"
        status.Text = text
    end
end

local con_panel, console_box, btn_console

local function close_console()
    if con_panel then con_panel.Visible = false end
    if console_box then wl.raw_ui_set(console_box._id, "Focus", false) end
    if btn_console then btn_console.TextColor = "#FFFFFF" end
end

local function guard_action()
    if dev_allowed() then return true end
    panel.Visible = false
    toggle.Visible = false
    con_hotkey.Visible = false
    toggle.TextColor = "#FFFFFF"
    close_console()
    say("Disabled in MP without cheats", "#FF8080")
    return false
end

-- A row of equal buttons: { {text, fn, color}, ... }
local function row(items)
    local n = #items
    local bw = math.floor((W - 16 - GAP * (n - 1)) / n)
    for i, it in ipairs(items) do
        local b = UI:CreateButton({ Parent = panel, Text = it[1], Position = { 8 + (i - 1) * (bw + GAP), y },
            Size = { bw, ROW - 4 }, TextSize = 11, TextColor = it[3] })
        local fn = it[2]
        b:OnClick(function()
            if not guard_action() then return end
            fn()
        end)
    end
    y = y + ROW
end

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
        local n, cols = count, math.ceil(math.sqrt(count))
        say("Spawning " .. n .. " " .. name .. "...")
        for i = 1, n do
            if not guard_action() then return end
            local dx, dy = (i - 1) % cols, (i - 1) // cols
            me():CreateUnit(id, x + dx, y0 + dy)
            if i % 90 == 0 then Time.Wait(1) end                 -- at most 100 commands a second
        end
        say("Spawned " .. n .. " " .. name)
    end
end

-- ---- Dev Panel content ----
label("Wololoader Dev Panel", { bold = true, size = 13, h = 18 })
local info = label("", { color = "#FFFFFF", h = 16 })

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
row({
    { "Villager", spawn(83, "Villager") }, { "Monk", spawn(125, "Monk") }, { "Knight", spawn(38, "Knight") },
})
row({
    { "Archer", spawn(4, "Archer") }, { "Trebuchet", spawn(331, "Trebuchet") }, { "Siege Ram", spawn(548, "Siege Ram") },
})
UI:CreateLabel({ Parent = panel, Text = "Count (1-1000):", Position = { 8, y + 2 }, Size = { 110, 18 },
    TextSize = 11, TextColor = "#E8C878" })
local count_btn = UI:CreateTextBox({ Parent = panel, Text = "1", Position = { 122, y }, Size = { W - 130, ROW - 4 },
    TextSize = 12, Numeric = true, MaxLength = 4, Placeholder = "1" })
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
        -- gamecmd.c queues up to 4096 commands (100/s), keeping local UI context for all enemies.
        for _, u in ipairs(list) do u:Delete() end
        say("Killed " .. #list .. " enemy unit(s)") end, "#FF8080" },
    { "Raze all", function()
        local list = {}
        for _, p in ipairs(Game.Players()) do
            if me():IsEnemy(p) then
                for _, u in ipairs(p:Units()) do list[#list + 1] = u end
            end
        end
        for _, u in ipairs(list) do u:Delete() end
        say("Razed " .. #list .. " enemy object(s)") end, "#FF8080" },
})

y = y + 4
btn_console = UI:CreateButton({ Parent = panel, Text = "Console", Position = { 8, y }, Size = { 96, ROW - 2 },
    TextSize = 11 })
UI:CreateLabel({ Parent = panel, Text = "Ctrl+Shift+C", Position = { 112, y + 3 }, Size = { W - 120, 16 },
    TextSize = 11, TextColor = "#98B898" })
y = y + ROW + 2

status = label("Ctrl+Shift+D: panel | Ctrl+Shift+C: console", { color = "#A0FFA0", h = 16 })
panel.Size = { W, y + 4 }

-- ---- Standalone Lua Console Window (docked right) ----
con_panel = UI:CreateFrame({ Anchor = "TopRight", Position = { 8, 36 }, Size = { 512, 500 },
    Color = "#0E1116", BorderColor = "#586878", Transparency = 0.28, Visible = false })
local con_title = UI:CreateLabel({ Parent = con_panel, Text = "Lua Console", Position = { 10, 6 },
    Size = { 110, 18 }, TextSize = 13, Bold = true, TextColor = "#E8C878" })
local con_hint = UI:CreateLabel({ Parent = con_panel, Text = "Wheel / PgUp / PgDn | Esc: close", Position = { 116, 7 },
    Size = { 230, 16 }, TextSize = 11, TextColor = "#8FA0B0" })

local btn_close = UI:CreateButton({ Parent = con_panel, Text = "X", Anchor = "TopRight", Position = { 6, 5 },
    Size = { 24, 20 }, TextSize = 11, TextColor = "#FFB0B0" })
wl.raw_ui_set(btn_close._id, "Hotkey", 0x1B) -- VK_ESCAPE closes console while open
local btn_clr = UI:CreateButton({ Parent = con_panel, Text = "Clr", Anchor = "TopRight", Position = { 34, 5 },
    Size = { 36, 20 }, TextSize = 11 })
local btn_dn = UI:CreateButton({ Parent = con_panel, Text = "v", Anchor = "TopRight", Position = { 74, 5 },
    Size = { 22, 20 }, TextSize = 11 })
local btn_up = UI:CreateButton({ Parent = con_panel, Text = "^", Anchor = "TopRight", Position = { 100, 5 },
    Size = { 22, 20 }, TextSize = 11 })
local btn_help = UI:CreateButton({ Parent = con_panel, Text = "Help", Anchor = "TopRight", Position = { 126, 5 },
    Size = { 42, 20 }, TextSize = 11, TextColor = "#A8D8FF" })

local out_frame = UI:CreateFrame({ Parent = con_panel, Position = { 8, 30 }, Size = { 496, 430 },
    Color = "#000000", Transparency = 1, BorderColor = "#2C3642" })
local out_labels = {}

console_box = UI:CreateTextBox({ Parent = con_panel, Text = "", Position = { 8, 466 }, Size = { 496, 24 },
    TextSize = 13, MaxLength = 255, Placeholder = "> Lua code or = expr (Enter) | help() | Esc",
    Color = "#080A0E", BorderColor = "#4C5E70", TextColor = "#F0F4F8" })
wl.raw_ui_set(console_box._id, "Mono", 1)

-- Console output, wrapping, scrolling, and history
local con_entries = {}
local con_lines = {}
local con_rows = 24
local con_line_chars = 66
local scroll_back = 0
local history = {}
local hist_pos = 1
local hist_draft = ""
local scr_w, scr_h = 1280, 720

local COLOR_REF = {
    ["#E6EAF0"] = 0xF0EAE6,
    ["#E8C878"] = 0x78C8E8,
    ["#FF7878"] = 0x7878FF,
    ["#98B898"] = 0x98B898,
}
local lbl_ids, lbl_text, lbl_color = {}, {}, {}
local rendered_rows = 0
local raw_ui_set = wl.raw_ui_set

local function wrap_into(raw, color, dst)
    raw = tostring(raw or "")
    if #raw <= con_line_chars and not raw:find("[\r\n]") then
        dst[#dst + 1] = { text = raw, color = color }
        return
    end
    raw = raw:gsub("\r\n", "\n"):gsub("\r", "\n")
    for seg in (raw .. "\n"):gmatch("([^\n]*)\n") do
        if seg == "" then
            dst[#dst + 1] = { text = "", color = color }
        else
            local s = seg
            while #s > con_line_chars do
                local cut = s:sub(1, con_line_chars):match("^(.*)%s+")
                if cut and #cut >= 12 then
                    dst[#dst + 1] = { text = cut, color = color }
                    s = s:sub(#cut + 1):gsub("^%s+", "")
                else
                    dst[#dst + 1] = { text = s:sub(1, con_line_chars), color = color }
                    s = s:sub(con_line_chars + 1)
                end
            end
            if #s > 0 then
                dst[#dst + 1] = { text = s, color = color }
            end
        end
    end
end

local function rewrap_all()
    local lines = {}
    for _, e in ipairs(con_entries) do
        wrap_into(e.text, e.color, lines)
    end
    con_lines = lines
end

local function render_console(max_rows_budget)
    local total = #con_lines
    local max_scroll = math.max(0, total - con_rows)
    if scroll_back > max_scroll then scroll_back = max_scroll end
    if scroll_back < 0 then scroll_back = 0 end
    local start_idx = math.max(1, total - con_rows + 1 - scroll_back)
    local need = math.min(con_rows, total - start_idx + 1)
    local limit = math.max(rendered_rows, need)
    if max_rows_budget and limit > max_rows_budget then limit = max_rows_budget end
    local new_rendered = 0
    for i = 1, limit do
        local id = lbl_ids[i]
        if id then
            local item = con_lines[start_idx + i - 1]
            local txt = item and item.text or ""
            local col = item and (COLOR_REF[item.color] or 0xF0EAE6) or 0xF0EAE6
            if item then
                new_rendered = i
                if lbl_color[i] ~= col then
                    lbl_color[i] = col
                    raw_ui_set(id, "TextColor", col)
                end
            end
            if lbl_text[i] ~= txt then
                lbl_text[i] = txt
                raw_ui_set(id, "Text", txt)
            end
        end
    end
    rendered_rows = new_rendered
end

local function push_wrapped(raw, color)
    con_entries[#con_entries + 1] = { text = tostring(raw or ""), color = color }
    while #con_entries > CONSOLE_MAX_ENTRIES do
        table.remove(con_entries, 1)
    end
    wrap_into(raw, color, con_lines)
    while #con_lines > CONSOLE_MAX_ENTRIES * 2 do
        table.remove(con_lines, 1)
    end
end

local function console_print(raw, color)
    push_wrapped(raw, color)
    scroll_back = 0
    render_console()
end

local function scroll_by(delta)
    scroll_back = scroll_back + delta
    render_console()
end

local function clear_console()
    con_entries = {}
    con_lines = {}
    scroll_back = 0
    render_console()
end

local function layout_console(w, h)
    if not w or not h or w < 320 or h < 240 then return end
    scr_w, scr_h = w, h
    local con_w = math.min(w - 16, math.max(380, math.floor(w * 0.40)))
    local top_y = 36
    local bot_margin = math.min(210, math.max(150, math.floor(h * 0.18)))
    local con_h = math.max(220, h - top_y - bot_margin)
    con_panel.Position = { 8, top_y }
    con_panel.Size = { con_w, con_h }

    local out_w = con_w - 16
    local out_h = con_h - 30 - 36
    out_frame.Position = { 8, 30 }
    out_frame.Size = { out_w, out_h }
    console_box.Position = { 8, con_h - 30 }
    console_box.Size = { out_w, 24 }
    con_hint.Visible = (con_w >= 500)

    con_rows = math.min(MAX_OUT_ROWS, math.max(6, math.floor((out_h - 8) / LINE_H)))
    con_line_chars = math.max(36, math.floor((out_w - 14) / 7))

    for i = 1, con_rows do
        local lbl = out_labels[i]
        if not lbl then
            lbl = UI:CreateLabel({ Parent = out_frame, Text = "", Position = { 6, 4 + (i - 1) * LINE_H },
                Size = { out_w - 12, LINE_H }, TextSize = 13, TextColor = "#E6EAF0" })
            raw_ui_set(lbl._id, "Mono", 1)
            out_labels[i] = lbl
            lbl_ids[i] = lbl._id
        else
            lbl.Position = { 6, 4 + (i - 1) * LINE_H }
            lbl.Size = { out_w - 12, LINE_H }
            lbl.Visible = true
        end
    end
    for i = con_rows + 1, #out_labels do
        out_labels[i].Visible = false
    end
    rewrap_all()
    render_console()
end

layout_console(scr_w, scr_h)

wl.on("ui_resize", function(packed)
    local w, h = packed >> 16, packed & 0xffff
    layout_console(w, h)
end)

-- Console environment bootstrap: runs inside the dedicated wl.dev_eval sandbox env.
local CON_BOOTSTRAP = [=[
if not _G._wl_con_init then
    _G._wl_con_init = true
    local _raw_print = print
    local _mark_seq = 0

    local function fmt_player(p)
        local age = p.AgeName
        if not age then return "Player " .. tostring(p.Id) end
        return string.format("Player %d%s (%s, Pop %d | F:%d W:%d S:%d G:%d)",
            p.Id, p.IsLocal and " [me]" or "", age, p.Population or 0,
            math.floor(p.Food or 0), math.floor(p.Wood or 0),
            math.floor(p.Stone or 0), math.floor(p.Gold or 0))
    end

    local function fmt_unit(u)
        local cls = u.ClassName or ("class " .. tostring(u.Class))
        local pos = (u.X and u.Y) and string.format("(%.1f, %.1f)", u.X, u.Y) or "(?, ?)"
        local own = u.Owner and ("P" .. tostring(u.Owner.Id)) or "?"
        local extra = u.IsBuilding and (u.Built and " built" or " foundation") or ""
        return string.format("Unit#%s type=%s %s @ %s %s%s",
            tostring(u.Id), tostring(u.Type), cls, pos, own, extra)
    end

    local p1 = Game.Player(1)
    if p1 then
        local pmt = getmetatable(p1)
        if type(pmt) == "table" then
            pmt.__tostring = fmt_player
            local orig_units = pmt.Units
            pmt.Units = function(self, filter)
                local list = orig_units(self, filter)
                if type(list) == "table" and list[1] then
                    local umt = getmetatable(list[1])
                    if type(umt) == "table" and not umt._wl_pretty then
                        umt._wl_pretty = true
                        umt.__tostring = fmt_unit
                    end
                end
                return list
            end
        end
    end

    local orig_selected = Game.Selected
    Game.Selected = function()
        local list = orig_selected()
        if type(list) == "table" and list[1] then
            local umt = getmetatable(list[1])
            if type(umt) == "table" and not umt._wl_pretty then
                umt._wl_pretty = true
                umt.__tostring = fmt_unit
            end
        end
        return list
    end

    local function fmt_key(k)
        if type(k) == "string" and k:match("^[%a_][%w_]*$") then return k end
        if type(k) == "string" then return "[" .. string.format("%q", k) .. "]" end
        return "[" .. tostring(k) .. "]"
    end

    local function fmt_val(v)
        local tv = type(v)
        if tv == "string" then return string.format("%q", v) end
        if tv == "table" then
            local mt = getmetatable(v)
            if type(mt) == "table" and mt.__tostring then return tostring(v) end
            return "{...}"
        end
        return tostring(v)
    end

    local function pp(v)
        if type(v) ~= "table" then return tostring(v) end
        local mt = getmetatable(v)
        if type(mt) == "table" and mt.__tostring then return tostring(v) end
        if next(v) == nil then return "{}" end

        local entries, seen = {}, {}
        local max_show = 40
        local total = 0
        for i = 1, #v do
            seen[i] = true
            total = total + 1
            if #entries < max_show then
                entries[#entries + 1] = fmt_key(i) .. " = " .. fmt_val(v[i])
            end
        end
        local extra_keys = {}
        for k in pairs(v) do
            if not seen[k] then
                total = total + 1
                extra_keys[#extra_keys + 1] = k
            end
        end
        table.sort(extra_keys, function(a, b) return tostring(a) < tostring(b) end)
        for _, k in ipairs(extra_keys) do
            if #entries < max_show then
                entries[#entries + 1] = fmt_key(k) .. " = " .. fmt_val(v[k])
            end
        end
        if total > max_show then
            entries[#entries + 1] = "... (+" .. (total - max_show) .. " more)"
        end
        local single = "{ " .. table.concat(entries, ", ") .. " }"
        if #entries <= 3 and #single <= 72 then return single end
        return "{\n  " .. table.concat(entries, ",\n  ") .. "\n}"
    end

    function print(...)
        local n = select("#", ...)
        if n == 0 then _raw_print() return end
        local args = {}
        for i = 1, n do
            local v = select(i, ...)
            args[i] = (type(v) == "table") and pp(v) or tostring(v)
        end
        _raw_print(table.unpack(args, 1, n))
    end
    wl.log = print

    local _dyn = {
        me = function() return Game.LocalPlayer end,
        sel = function() return Game.Selected() end,
        players = function() return Game.Players() end,
    }
    setmetatable(_G, {
        __index = function(_, k)
            local fn = _dyn[k]
            if fn then return fn() end
        end
    })

    function clear()
        _raw_print("__WL_CLEAR__")
    end

    function reload(mod)
        if mod ~= nil then
            local ok, err = wl.reload(tostring(mod))
            if ok then return "Reloaded " .. tostring(mod) end
            error(err or ("Reload failed: " .. tostring(mod)), 2)
        end
        local reloaded, errors = wl.reload()
        local parts = {}
        if reloaded and #reloaded > 0 then
            parts[#parts + 1] = "Reloaded: " .. table.concat(reloaded, ", ")
        end
        if errors and #errors > 0 then
            for _, e in ipairs(errors) do
                _raw_print("Reload failed: " .. tostring(e))
            end
            if #parts == 0 then
                error("Reload failed (" .. #errors .. " mod(s))", 2)
            end
        end
        if #parts == 0 then return "No changed mods." end
        return table.concat(parts, "; ")
    end

    function reload_mods()
        local ok, msg = wl.reload_mods()
        if ok then return msg end
        error(msg or "Mod reload failed", 2)
    end

    function help()
        _raw_print("--- Game ---")
        _raw_print("  Game.LocalPlayer / Player(id) / Players()   players (0=Gaia, 1..8)")
        _raw_print("  Game.Selected() / Game.Time                 selected Units (<=60); game sec")
        _raw_print("  Game.IsMultiplayer() / CheatsEnabled()      match mode and cheats flag")
        _raw_print("--- Player ---")
        _raw_print("  p.Id, p.IsLocal, p.Age, p.AgeName, p.Population")
        _raw_print("  p.Food / Wood / Stone / Gold                current resource stockpiles")
        _raw_print("  p:IsEnemy(other) / p:Units([filter])        enemy check; units (class/fn)")
        _raw_print("  p:AddFood/Wood/Stone/Gold(n), AddResource(name, n)")
        _raw_print("  p:CreateUnit(id, x, y), Research(id), SetAge(age)  0..3 or Dark..Imperial")
        _raw_print("--- Unit ---")
        _raw_print("  u.Id, u.Type, u.X, u.Y, u.Owner             id, dat type, tile pos, Player")
        _raw_print("  u.Class, u.ClassName, u.IsBuilding, u.Built class and building state")
        _raw_print("  u:Delete()                                  delete the unit or building")
        _raw_print("--- UI (and Sound / Time) ---")
        _raw_print("  UI.Message(t [,opts]) / UI.Chat(t [,opts])  notify ({Color}); chat ({From,Color})")
        _raw_print("  UI.Dialog(t) / UI.Ask(t [,fn])              OK / Yes-No dialog (MP: msg/nil)")
        _raw_print("  UI:CreateCounter(text)                      countdown (:SetText/:Show/:Hide/:Destroy)")
        _raw_print("  UI:CreateFrame/Label/Button/TextBox(props)  on-screen UI elements")
        _raw_print("  Sound.Play(nameOrId)                        \"Chat\",\"Attack\",\"Relic\",... or 0..50")
        _raw_print("  Time.After/Every(s, fn) / Cancel(h) / Wait(s)")
        _raw_print("--- Events ---")
        _raw_print("  Game.OnStart(fn) / OnSecond(fn)             match start; every game second")
        _raw_print("  Game.OnAgeUp(fn(p, age)) / OnResearch(fn(p, id))        polled 1/s")
        _raw_print("  Game.OnUnitCreated(fn(u)) / OnUnitDied(fn(id, p))       polled 1/s")
        _raw_print("  Game.OnBuildingDone(fn(u))                              polled 1/s")
        _raw_print("  Game.OnCommand(name, fn(b)) / CommandCondition(name, fn(b))")
        _raw_print("--- Console ---")
        _raw_print("  me, sel, players   shortcuts;  print(...), clear(), help()")
        _raw_print("  reload([mod])      hot-reload one mod or all changed mods")
        _raw_print("  reload_mods()      full main-menu mod reload (dat, civs, assets, scripts)")
    end

    function _wl_mark(seq)
        _mark_seq = seq
    end

    function _wl_ran(seq)
        return _mark_seq == seq
    end

    function _wl_ret(...)
        local n = select("#", ...)
        if n == 0 then return end
        local out = {}
        for i = 1, n do
            out[i] = pp(select(i, ...))
        end
        return table.concat(out, ", ")
    end
end
]=]

local con_bootstrapped = false
local eval_seq = 0
local function ensure_console_env()
    if con_bootstrapped or not dev_allowed() then return end
    local ok, _, err = wl.dev_eval(CON_BOOTSTRAP)
    if ok then
        con_bootstrapped = true
    elseif err then
        push_wrapped("Console init error: " .. tostring(err), "#FF7878")
        render_console()
    end
end

local function eval_console(trimmed)
    ensure_console_env()
    eval_seq = eval_seq + 1
    local seq = eval_seq
    local is_eq = trimmed:sub(1, 1) == "="
    local expr = is_eq and trimmed:sub(2) or trimmed
    local wrapped = "local _ = _wl_mark(" .. seq .. "); return _wl_ret(" .. expr .. "\n)"
    local ok, lines, err = wl.dev_eval(wrapped)
    if not ok and not is_eq and dev_allowed() then
        local _, ran_lines = wl.dev_eval("return _wl_ran(" .. seq .. ")")
        if not ran_lines or ran_lines[1] ~= "true" then
            ok, lines, err = wl.dev_eval(trimmed)
        end
    end
    return ok, lines, err
end

local function append_eval_output(ok, lines, err)
    if lines then
        for _, line in ipairs(lines) do
            if line == CLEAR_SENTINEL then
                con_entries = {}
                con_lines = {}
                scroll_back = 0
            else
                push_wrapped(line, "#E6EAF0")
            end
        end
    end
    local budget
    if not ok and err then
        push_wrapped(err, "#FF7878")
        if err:find("instruction limit", 1, true) then budget = 16 end
        say("Console error", "#FF8080")
    else
        say("OK")
    end
    scroll_back = 0
    render_console(budget)
    if budget then Time.After(0.05, function() render_console() end) end
end

local function open_console()
    if not guard_action() then return end
    ensure_console_env()
    local sz = wl.raw_ui_text(-1)
    if sz then
        local w, h = sz:match("^(%d+)x(%d+)$")
        if w and h then layout_console(tonumber(w), tonumber(h)) end
    end
    con_panel.Visible = true
    btn_console.TextColor = "#FFFF80"
    console_box:Focus()
end

local function toggle_console()
    if not guard_action() then return end
    if con_panel.Visible then
        close_console()
    else
        open_console()
    end
end

console_print("Wololoader Lua Console — Ctrl+Shift+C to toggle, Esc to close", "#98B898")
console_print("Type help() for API, clear() to clear, = expr to inspect.", "#98B898")

btn_console:OnClick(toggle_console)
con_hotkey:OnClick(toggle_console)
btn_close:OnClick(close_console)

btn_up:OnClick(function()
    if not guard_action() then return end
    scroll_by(3)
    console_box:Focus()
end)
btn_dn:OnClick(function()
    if not guard_action() then return end
    scroll_by(-3)
    console_box:Focus()
end)
btn_clr:OnClick(function()
    if not guard_action() then return end
    clear_console()
    console_box:Focus()
end)
btn_help:OnClick(function()
    if not guard_action() then return end
    push_wrapped("> help()", "#E8C878")
    local ok, lines, err = eval_console("help()")
    append_eval_output(ok, lines, err)
    console_box:Focus()
end)

local function on_wheel(_, dir)
    if not guard_action() then return end
    scroll_by(dir > 0 and 3 or -3)
end
out_frame:OnScroll(on_wheel)
con_panel:OnScroll(on_wheel)
console_box:OnScroll(on_wheel)

console_box:OnKey(function(box, key)
    if not guard_action() then return end
    if key == "Up" then
        if #history == 0 then return end
        if hist_pos > #history then hist_draft = box.Text or "" end
        if hist_pos > 1 then
            hist_pos = hist_pos - 1
            box.Text = history[hist_pos]
        end
    elseif key == "Down" then
        if hist_pos <= #history then
            hist_pos = hist_pos + 1
            box.Text = (hist_pos <= #history) and history[hist_pos] or hist_draft
        end
    elseif key == "PageUp" then
        scroll_by(con_rows)
    elseif key == "PageDown" then
        scroll_by(-con_rows)
    elseif key == "27" or key == "Escape" then
        close_console()
    end
end)

console_box:OnSubmit(function(box, text)
    if not guard_action() then return end
    local trimmed = (text or ""):gsub("^%s*(.-)%s*$", "%1")
    if trimmed == "" then
        box:Focus()
        return
    end
    if history[#history] ~= trimmed then
        history[#history + 1] = trimmed
        if #history > HISTORY_MAX then table.remove(history, 1) end
    end
    hist_pos = #history + 1
    hist_draft = ""

    push_wrapped("> " .. trimmed, "#E8C878")
    local ok, lines, err = eval_console(trimmed)
    append_eval_output(ok, lines, err)
    box.Text = ""
    if con_panel.Visible then box:Focus() end
end)

local function sync_availability()
    if dev_allowed() then
        toggle.Visible = true
        con_hotkey.Visible = true
    else
        toggle.Visible = false
        con_hotkey.Visible = false
        panel.Visible = false
        toggle.TextColor = "#FFFFFF"
        close_console()
    end
end

toggle:OnClick(function()
    if not guard_action() then return end
    panel.Visible = not panel.Visible
    toggle.TextColor = panel.Visible and "#FFFF80" or "#FFFFFF"
end)

-- Live info line: game time, population, selection.
local function clock(t)
    t = math.floor(t or 0)
    return string.format("%d:%02d:%02d", t // 3600, t // 60 % 60, t % 60)
end

-- (timers are cleared when a game starts, so start this one from OnStart)
Game.OnStart(function()
    sync_availability()
    if not dev_allowed() then return end
    ensure_console_env()
    Time.Every(0.5, function()
        if not dev_allowed() then
            sync_availability()
            return
        end
        if not panel.Visible then return end
        local p = me()
        if not p then return end
        local sel = Game.Selected()
        local s = clock(Game.Time) .. "   Pop " .. (p.Population or 0) .. "   " .. (p.AgeName or "?")
        if sel[1] then s = s .. "   Sel " .. #sel .. " (" .. (sel[1].ClassName or ("type " .. sel[1].Type)) .. ")" end
        info.Text = s
    end)
end)
