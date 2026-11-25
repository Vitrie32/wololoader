-- Faith and Relics: example custom resource mod.
-- Every 10 seconds of game time, each player gains +1 Faith per Monk (including
-- Monks carrying Relics). AddResource sends a lockstep game command, so every
-- machine in a multiplayer match stays in sync.

Game.OnStart(function()
    Time.Every(10, function()
        for _, p in ipairs(Game.Players()) do
            local monks = #p:Units("Monk") + #p:Units("MonkWithRelic")
            if monks > 0 then
                p:AddResource("faith", monks)
            end
        end
    end)
end)
