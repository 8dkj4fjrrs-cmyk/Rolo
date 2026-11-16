-- lift.lua: moves the lift up and down between two heights.
--
-- The lift has a kinematic Rigid Body: gravity does not pull it, nothing
-- pushes it, and it moves at the velocity a script gives it, carrying
-- whatever stands on it.

-- Properties: every lift can have its own. Select a lift and look at its
-- Script in the Inspector; the numbers here are what a lift gets until you
-- change them there.
properties = {
    speed = 90,        -- points per second
    rise = 200,        -- how far up it goes
}

function start(self)
    local x, y = self.object:position()
    self.bottom = y
    self.direction = 1
end

function update(self, dt)
    local x, y = self.object:position()
    if y > self.bottom + self.rise then self.direction = -1 end
    if y < self.bottom then self.direction = 1 end
    self.object:setVelocity(0, self.speed * self.direction)
end
