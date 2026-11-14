-- vectors.lua: the same maths as garbage.lua, with each point a Luau vector
-- instead of a table. A vector is a value, like a number: making one leaves
-- no garbage, and vectors add and multiply (home + offset). Rolo's functions
-- take a vector wherever they take x, y.

local function offset(t)
    return vector.create(math.cos(t) * 30, math.sin(t) * 30, 0)
end

function start(self)
    local x, y = self.object:position()
    self.home = vector.create(x, y, 0)
    self.t = (x + y) * 0.01
end

function update(self, dt)
    self.t = self.t + dt
    local first = offset(self.t - 0.1)
    for i = 2, 8 do
        local point = offset(self.t - i * 0.1)
    end
    self.object:setPosition(self.home + first)
end
