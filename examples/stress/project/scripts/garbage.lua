-- garbage.lua: moves like orbit.lua, but the way a lot of real code is
-- written: a new table for every point and a new string every frame. All of
-- it is garbage a moment later, which the collector must clear up.

local function offset(t)
    return {x = math.cos(t) * 30, y = math.sin(t) * 30}
end

function start(self)
    self.x, self.y = self.object:position()
    self.t = (self.x + self.y) * 0.01
end

function update(self, dt)
    self.t = self.t + dt
    local trail = {}
    for i = 1, 8 do
        trail[i] = offset(self.t - i * 0.1)
    end
    self.label = "at " .. math.floor(trail[1].x) .. ", " .. math.floor(trail[1].y)
    self.object:setPosition(self.x + trail[1].x, self.y + trail[1].y)
end
