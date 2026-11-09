-- spawner.lua: fires bullets from a pool of objects made in the editor
-- ("Bullet 1", "Bullet 2", ...): a resting bullet is put here and switched
-- on; bullet.lua flies it and switches it off again.

local PER_SECOND = 1200

function start(self)
    self.x, self.y = self.object:position()
    self.pool = {}
    local i = 1
    while true do
        local bullet = game.find("Bullet " .. i)
        if bullet == nil then break end
        self.pool[#self.pool + 1] = bullet
        i = i + 1
    end
    self.next = 1
    self.owed = 0
end

function update(self, dt)
    self.owed = self.owed + PER_SECOND * dt
    while self.owed >= 1 do
        self.owed = self.owed - 1
        local bullet = self.pool[self.next]
        self.next = self.next % #self.pool + 1
        if not bullet:isActive() then
            bullet:setPosition(self.x, self.y)
            bullet:setActive(true)
        end
    end
end
