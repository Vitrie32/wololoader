-- Crystal Mines: example gatherable custom resource mod.
-- At match start, places 2 Crystal Deposits on Gaia about 8-10 tiles from each
-- player's Town Center at deterministic positions (no math.random, MP-safe).

-- Deterministic offset pairs per player (distances ~8.94..9.06 tiles, 90 degrees apart).
local OFFSETS = {
    { {  9,  1 }, { -1,  9 } },
    { { -9,  1 }, {  1, -9 } },
    { {  8,  4 }, { -4,  8 } },
    { { -8, -4 }, {  4, -8 } },
}

local function find_anchor(p)
    local tcs = p:UnitsOfType(109)
    if #tcs > 0 then return tcs[1] end
    local vills = p:Units("Villager")
    if #vills > 0 then return vills[1] end
    return nil
end

local function offset_tile(base, delta)
    local v = math.floor(base) + delta
    if v < 2 then v = math.floor(base) - delta end
    if v < 2 then v = 2 end
    return v
end

Game.OnStart(function()
    local deposit = Game.ResourceDeposit("crystal")
    if not deposit then return end

    local gaia = Game.Player(0)
    for _, p in ipairs(Game.Players()) do
        local anchor = find_anchor(p)
        if anchor then
            local pair = OFFSETS[((p.Id - 1) % #OFFSETS) + 1]
            for _, d in ipairs(pair) do
                local x = offset_tile(anchor.X, d[1])
                local y = offset_tile(anchor.Y, d[2])
                gaia:CreateUnit(deposit, x, y)
            end
        end
    end
end)
