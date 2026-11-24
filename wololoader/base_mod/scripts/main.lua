-- Wololoader Base scripts: runs automatically when WololoaderBase is enabled.
if wl.feature and not wl.feature("dev_panel") then return end
require("devpanel")
