-- lift.lua: moves the lift up and down between two heights.
--
-- The lift has a kinematic Rigid Body: gravity does not pull it, nothing
-- pushes it, and it moves at the velocity a script gives it, carrying
-- whatever stands on it.

local SPEED = 90       -- points per second
local RISE = 200       -- how far up it goes

function start(self)
    local x, y = self.object:position()
    self.bottom = y
    self.direction = 1
end

function update(self, dt)
    local x, y = self.object:position()
    if y > self.bottom + RISE then self.direction = -1 end
    if y < self.bottom then self.direction = 1 end
    self.object:setVelocity(0, SPEED * self.direction)
end
