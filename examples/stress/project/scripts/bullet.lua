-- bullet.lua: flies straight out from where the spawner put it, and rests
-- (switched off) once it is off the screen.

local SPEED = 500
local fired = 0   -- shared by every bullet (one file, one set of globals)

local function aim(self)
    fired = fired + 1
    local angle = fired * 0.37
    self.vx, self.vy = math.cos(angle) * SPEED, math.sin(angle) * SPEED
    self.fromX, self.fromY = self.object:position()
end

function update(self, dt)
    if not self.flying then   -- just fired (or fired again)
        aim(self)
        self.flying = true
    end
    local x, y = self.object:position()
    x, y = x + self.vx * dt, y + self.vy * dt
    self.object:setPosition(x, y)
    local dx, dy = x - self.fromX, y - self.fromY
    if dx * dx + dy * dy > 700 * 700 then
        self.flying = false
        self.object:setActive(false)
    end
end
