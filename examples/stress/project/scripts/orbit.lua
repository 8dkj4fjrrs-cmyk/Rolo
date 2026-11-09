-- orbit.lua: circles the point where the object starts, turning as it goes.
-- The kind of small script a game has hundreds of.

local RADIUS = 40

function start(self)
    self.x, self.y = self.object:position()
    self.phase = (self.x * 0.013 + self.y * 0.007) % 6.283
    self.speed = 1 + (self.x % 7) * 0.2
end

function update(self, dt)
    self.phase = self.phase + self.speed * dt
    self.object:setPosition(self.x + math.cos(self.phase) * RADIUS, self.y + math.sin(self.phase) * RADIUS)
    self.object:setRotation(self.phase * 57.3)
end
