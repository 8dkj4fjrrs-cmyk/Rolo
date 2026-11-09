#include "Rumble.hpp"

namespace rolo::detail {

namespace {
class NoRumble final : public Rumble {
public:
    bool set(int, float, float) override { return false; }
};
} // namespace

std::unique_ptr<Rumble> Rumble::create(const Logger&)
{
    return std::make_unique<NoRumble>();
}

} // namespace rolo::detail
