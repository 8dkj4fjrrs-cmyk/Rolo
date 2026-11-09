#include "Rumble.hpp"

namespace rolo::detail {

void RumbleTimer::update(const std::vector<RumbleRequest>& requests, double time,
                         const std::array<int, Input::kMaxGamepads>& jids, Rumble& out)
{
    for (const RumbleRequest& request : requests) {
        for (int pad = 0; pad < Input::kMaxGamepads; ++pad) {
            if (request.pad == -1 || request.pad == pad) {
                Shake& shake = m_shakes[static_cast<std::size_t>(pad)];
                shake.low = request.low;
                shake.high = request.high;
                shake.until = time + static_cast<double>(request.seconds);
            }
        }
    }
    for (int pad = 0; pad < Input::kMaxGamepads; ++pad) {
        Shake& shake = m_shakes[static_cast<std::size_t>(pad)];
        const int jid = jids[static_cast<std::size_t>(pad)];
        const bool on = time < shake.until;
        const std::array<float, 2> wanted{on ? shake.low : 0.0f, on ? shake.high : 0.0f};
        if (jid >= 0 && wanted != shake.sent) {
            out.set(jid, wanted[0], wanted[1]);   // a pad that cannot rumble just stays still
            shake.sent = wanted;
        }
    }
}

void RumbleTimer::stop(const std::array<int, Input::kMaxGamepads>& jids, Rumble& out)
{
    for (int pad = 0; pad < Input::kMaxGamepads; ++pad) {
        Shake& shake = m_shakes[static_cast<std::size_t>(pad)];
        const int jid = jids[static_cast<std::size_t>(pad)];
        if (jid >= 0 && (shake.sent[0] > 0.0f || shake.sent[1] > 0.0f)) {
            out.set(jid, 0.0f, 0.0f);
        }
        shake = Shake{};
    }
}

void RumbleTimer::forget(int pad)
{
    if (pad >= 0 && pad < Input::kMaxGamepads) {
        m_shakes[static_cast<std::size_t>(pad)] = Shake{};
    }
}

} // namespace rolo::detail
