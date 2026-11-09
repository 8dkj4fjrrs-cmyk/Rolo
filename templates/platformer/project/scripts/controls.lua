-- controls.lua: a Controls screen where players change their keys and buttons.
--
-- Tab (or Start on a gamepad) opens and closes it. Up and down choose an
-- action, Enter (or A) changes it: the next key or gamepad button pressed
-- becomes its key or button. R (or Y) puts everything back as it was.
-- Players' choices are kept on their computer, beside their saved games.
-- Copy this script into your own game and list your own actions below.

local ACTIONS = {
    {"jump", "Jump"},
    {"move left", "Run left"},
    {"move right", "Run right"},
    {"interact", "Interact"},
}

function start(self)
    input.action("controls", {"tab", "pad start"})
    self.panel = game.find("Controls Panel")
    self.words = game.find("Controls Text")
    self.choice = 1
    game.set("menu_open", false)
end

-- The pictures of an action's keys and buttons: "{button:space} {button:pad a}".
local function pictures(action)
    local out = {}
    for _, name in ipairs(input.bindings(action)) do
        out[#out + 1] = "{button:" .. name .. "}"
    end
    return table.concat(out, " ")
end

local function show(self)
    local lines = {"CONTROLS", ""}
    for i, entry in ipairs(ACTIONS) do
        local marker = (i == self.choice) and "> " or "   "
        local keys = pictures(entry[1])
        if input.isRebinding() and i == self.choice then
            keys = "press a key or button...  (Backspace: keep)"
        end
        lines[#lines + 1] = marker .. entry[2] .. "    " .. keys
    end
    lines[#lines + 1] = ""
    lines[#lines + 1] = "{button:move up} {button:move down} choose   {button:enter} / {button:pad a} change   "
                        .. "{button:r} / {button:pad y} reset   {button:controls} close"
    self.words:setText(table.concat(lines, "\n"))
end

function update(self, dt)
    local open = game.get("menu_open")
    if input.wasPressed("controls") and not input.isRebinding() then
        open = not open
        game.set("menu_open", open)
        self.panel:setActive(open)
    end
    if not open then
        return
    end
    if not input.isRebinding() then
        if input.wasPressed("move down") then self.choice = self.choice % #ACTIONS + 1 end
        if input.wasPressed("move up") then self.choice = (self.choice - 2) % #ACTIONS + 1 end
        if input.wasPressed("enter") or input.wasPressed("pad a") then
            input.rebind(ACTIONS[self.choice][1])
        elseif input.wasPressed("r") or input.wasPressed("pad y") then
            input.resetControls()
        end
    end
    show(self)
end
