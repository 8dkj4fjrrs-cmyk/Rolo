-- player.lua: the hero runs and jumps. Physics does the rest.
--
-- The Player has a Rigid Body (physics moves it, gravity pulls it down) and
-- a capsule Collider (it slides over the edges of tiles). This script only
-- says how fast it should go: sideways from the arrow keys, and up when
-- you jump. Try other numbers, save, and press Play again.

local SPEED = 280     -- running speed, points per second
local JUMP = 640      -- jump speed, points per second upwards

function start(self)
    self.startX, self.startY = self.object:position()
    self.facing = "right"
end

function update(self, dt)
    local dx = input.direction()          -- -1 left, 0, 1 right
    local vx, vy = self.object:velocity()
    local onGround = self.object:isOnGround()

    -- Run: set the sideways speed and keep the up-and-down speed (falling).
    self.object:setVelocity(dx * SPEED, vy)

    -- Jump: only from the ground.
    if onGround and (input.wasPressed("space") or input.wasPressed("up") or input.wasPressed("w")) then
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
