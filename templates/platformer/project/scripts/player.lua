-- player.lua: the hero runs and jumps. Physics does the rest.
--
-- The Player has a Rigid Body (physics moves it, gravity pulls it down) and
-- a capsule Collider (it slides over the edges of tiles). This script only
-- says how fast it should go: sideways from the arrow keys, and up when
-- you jump. Try other numbers, save, and press Play again.

local SPEED = 280     -- running speed, points per second
local JUMP = 640      -- jump speed, points per second upwards

function start(self)
    -- "jump" is an action: the keys and buttons that do it. Players can
    -- change them in the Controls screen (scripts/controls.lua).
    input.action("jump", {"space", "w", "up", "pad a"})
    self.startX, self.startY = self.object:position()
    self.facing = "right"
    self.fallSpeed = 0
end

function update(self, dt)
    local vx, vy = self.object:velocity()
    local onGround = self.object:isOnGround()

    -- Landing after a long fall: a little rumble on a gamepad.
    if onGround and self.fallSpeed < -700 then
        input.rumble(0.4, 0.15)
    end
    self.fallSpeed = vy

    -- While a menu is open (pause or controls), the hero stands still.
    if game.get("menu") ~= "" then
        self.object:setVelocity(0, vy)
        self.object:play("idle_" .. self.facing)
        return
    end

    -- Run: set the sideways speed and keep the up-and-down speed (falling).
    local dx = input.direction()          -- -1 left to 1 right (keys, d-pad or stick)
    self.object:setVelocity(dx * SPEED, vy)

    -- Jump: only from the ground.
    if onGround and input.wasPressed("jump") then
        self.object:setVelocity(dx * SPEED, JUMP)
        audio.play("jump")
    end

    -- Face where it runs, and walk or stand.
    if dx < 0 then self.facing = "left" elseif dx > 0 then self.facing = "right" end
    if dx ~= 0 and onGround then
        self.object:play("walk_" .. self.facing)
    else
        self.object:play("idle_" .. self.facing)
    end

    -- Fell into a pit: back to the start.
    local x, y = self.object:position()
    if y < -200 then
        self.object:setPosition(self.startX, self.startY)
        self.object:setVelocity(0, 0)
    end
end
