-- Merchant Caravans: every few minutes a merchant's trade cart (Gaia's, never captured) travels
-- from town to town. While it waits in front of your Town Center, the Town Center's own command
-- panel has a "Trade with the merchant" button: press it to get resources. Each player trades
-- once with each caravan; after the last town it leaves the map.
--
-- Settings (Mods screen -> Merchant Caravans -> Settings) come in as MOD.settings.
-- Multiplayer: the caravan is spawned and steered by Gaia commands, which only one machine sends
-- (gamecmd.c); the trade gives resources only to the player who pressed the button.
local S = MOD.settings or {}

local CART = 204                  -- Trade Cart (full): the merchant's cart
local TOWN_CENTER = 109           -- the Town Center keeps id 109 in every age (buildpage.c)
local TOWN_CENTERS = { [109] = true, [71] = true, [141] = true, [142] = true }
local TRADE_RANGE = 10            -- tiles between the cart and your Town Center
local ARRIVED = 4                 -- tiles from a stop that count as "arrived"
local WAIT = 25                   -- seconds the merchant waits at each town

local GAIA = Game.Player(0)
local caravans = {}               -- [unit id] = { route, stop, traded, ordered_at, wait_until, x, y }
local count = 0                   -- caravans so far: picks where the next one starts

local function interval() return (S.minutes or 3) * 60 end
local function resource() return S.resource or "Gold" end

local function reward(me)
    local amount = S.amount or 60
    if S.grow ~= false and me and me.Age then amount = math.floor(amount * (1 + me.Age * 0.5)) end
    return amount
end

local function say(text)
    if S.announce ~= false then UI.Chat(text, { From = "Merchant", Color = 7 }) end
end

-- One stop in front of every player's Town Center, in player order starting at `first`.
-- The same on every machine (no random numbers), so all players agree on the route.
local function route(first)
    local towns = {}
    for _, p in ipairs(Game.Players()) do
        local tc = p:Units(function(u) return TOWN_CENTERS[u.Type] end)[1]
        if tc then towns[#towns + 1] = { tc.X + 4, tc.Y + 4 } end
    end
    local out = {}
    for i = 0, #towns - 1 do out[#out + 1] = towns[(first + i - 1) % #towns + 1] end
    return out
end

-- The caravan this Town Center can trade with: close to it, not traded yet.
local function caravan_near(tc)
    for _, c in pairs(caravans) do
        if not c.traded and c.x and tc.X then
            local dx, dy = c.x - tc.X, c.y - tc.Y
            if dx * dx + dy * dy <= TRADE_RANGE * TRADE_RANGE then return c end
        end
    end
    return nil
end

-- ---- the Town Center button (the game's own command panel; Game.AddCommand, buildpage.c) ----
if Game.AddCommand then
    Game.AddCommand({ building = TOWN_CENTER, button = 4, icon = "trade", name = "MerchantTrade",
        text = "Trade with the merchant",
        help = "When a merchant caravan waits in front of this Town Center, trade with it for resources." })
    Game.CommandCondition("MerchantTrade", function(tc) return caravan_near(tc) ~= nil end)
    Game.OnCommand("MerchantTrade", function(tc)
        local c = caravan_near(tc)
        local me = Game.LocalPlayer
        if not c or not me then return end
        c.traded = true
        local amount = reward(me)
        me:AddResource(resource(), amount)
        UI.Chat("Pleasure doing business! +" .. amount .. " " .. resource(), { From = "Merchant", Color = 7 })
        Sound.Play("Tribute")
    end)
else
    print("this Wololoader has no Game.AddCommand: update Wololoader to trade with merchants")
end

-- ---- caravans on the map ----
Game.OnSecond(function()
    local positions = {}
    for _, u in ipairs(GAIA:UnitsOfType(CART)) do positions[u.Id] = u end

    for id in pairs(positions) do
        if not caravans[id] then                      -- a new caravan appeared
            count = count + 1
            caravans[id] = { route = route(count), stop = 1, traded = false, ordered_at = -100, wait_until = 0 }
            say("My caravan is on the road. Wait for me at your Town Center to trade!")
        end
    end

    local now = Game.Time or 0
    for id, c in pairs(caravans) do
        local pos = positions[id]
        if not pos then
            caravans[id] = nil                          -- left the map or was destroyed
        else
            c.x, c.y = pos.X, pos.Y
            local stop = c.route[c.stop]
            if not stop then
                pcall(pos.Delete, pos)                  -- last town visited: the merchant leaves
                caravans[id] = nil
            elseif now >= c.wait_until then
                local dx, dy = pos.X - stop[1], pos.Y - stop[2]
                if dx * dx + dy * dy <= ARRIVED * ARRIVED then
                    if c.wait_until == 0 then
                        c.wait_until = now + WAIT        -- arrived: wait (Trade button on), then go on
                    else
                        c.stop = c.stop + 1
                        c.wait_until = 0
                        c.ordered_at = -100
                    end
                elseif now - c.ordered_at >= 20 then     -- (re)send the order every 20 s
                    pcall(pos.MoveTo, pos, stop[1], stop[2])
                    c.ordered_at = now
                end
            end
        end
    end
end)

-- A new caravan every few minutes, starting at the next player's town. Capturable = false: the
-- game would otherwise give the Gaia cart to the first player who sees it.
Game.OnStart(function()
    Time.Every(interval(), function()
        local stops = route(count + 1)
        if #stops == 0 then return end
        local ok, err = pcall(GAIA.CreateUnit, GAIA, CART, stops[1][1] + 2, stops[1][2] + 2, { Capturable = false })
        if not ok then print("could not send a caravan: " .. tostring(err)) end
    end)
    print("caravans every " .. interval() .. " s, " .. (S.amount or 60) .. " " .. resource())
end)
