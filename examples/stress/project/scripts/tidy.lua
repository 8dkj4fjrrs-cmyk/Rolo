-- tidy.lua: does the same maths as garbage.lua and moves the same way,
-- but keeps numbers in local variables instead of making new tables and
-- strings, so it leaves no garbage. Compare the two scenes.

function start(self)
    self.x, self.y = self.object:position()
    self.t = (self.x + self.y) * 0.01
end

function update(self, dt)
    self.t = self.t + dt
    local firstX, firstY = 0, 0
    for i = 1, 8 do
        local t = self.t - i * 0.1
        local x, y = math.cos(t) * 30, math.sin(t) * 30
        if i == 1 then firstX, firstY = x, y end
    end
    self.labelX, self.labelY = math.floor(firstX), math.floor(firstY)
    self.object:setPosition(self.x + firstX, self.y + firstY)
end
