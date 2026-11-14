// Gamepads where Rolo cannot read them yet (Android: ADR 0028 leaves
// controllers for a later step). Games see no gamepad, and play with the
// on-screen controls.

#include "rolo/runtime/Gamepads.hpp"

#include "Rumble.hpp"

namespace rolo {

GamepadReader::GamepadReader(const Logger& log)
    : m_log(log)
{
}

GamepadReader::~GamepadReader() = default;

void GamepadReader::stopRumble() {}

void GamepadReader::updateRumble(Input& input)
{
    static_cast<void>(input.takeRumbles());   // asked for, and dropped: nothing can rumble
}

void GamepadReader::read(Input& input)
{
    updateRumble(input);
}

} // namespace rolo
