-- pause.lua: a pause menu. Escape (or Start on a gamepad) opens and closes it.
--
-- Rolo games quit when Escape is pressed, unless the project turns that off.
-- This one does (the scene's Inspector, Game section: Escape Quits), so
-- Escape is free for this menu, and its Quit closes the game with
-- game.quit(). In the editor, Quit stops playing.
--
-- Up and down choose, Enter (or A) picks. The hero waits while it is open
-- (scripts/player.lua checks the "menu" variable).

local ITEMS = {"Resume", "Controls", "Quit"}

function start(self)
    input.action("pause", {"escape", "pad start"})
    self.panel = game.find("Pause Panel")
    self.words = game.find("Pause Text")
    self.choice = 1
end

local function show(self)
    local lines = {"PAUSED", ""}
    for i, item in ipairs(ITEMS) do
        lines[#lines + 1] = ((i == self.choice) and "> " or "   ") .. item
    end
    lines[#lines + 1] = ""
    lines[#lines + 1] = "{button:move up} {button:move down} choose   {button:enter} / {button:pad a} pick   "
                        .. "{button:pause} resume"
    self.words:setText(table.concat(lines, "\n"))
end

function update(self, dt)
    local menu = game.get("menu")
    -- (The Escape that cancels changing a control is not a press, so it does not pause.)
    if input.wasPressed("pause") then
        if menu == "" then
            menu = "pause"
            self.choice = 1
        elseif menu == "pause" then
            menu = ""        -- resume
        elseif menu == "controls" then
            menu = "pause"   -- back from the Controls screen
        end
        game.set("menu", menu)
    end
    local open = menu == "pause"
    self.panel:setActive(open)
    if not open then
        return
    end
    if input.wasPressed("move down") then self.choice = self.choice % #ITEMS + 1 end
    if input.wasPressed("move up") then self.choice = (self.choice - 2) % #ITEMS + 1 end
    if input.wasPressed("enter") or input.wasPressed("pad a") then
        local item = ITEMS[self.choice]
        if item == "Resume" then
            game.set("menu", "")
        elseif item == "Controls" then
            game.set("menu", "controls")
        else
            game.quit()
        end
    end
    show(self)
end
