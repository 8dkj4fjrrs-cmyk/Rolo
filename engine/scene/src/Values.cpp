#include "rolo/scene/Values.hpp"

#include <nlohmann/json.hpp>

#include <cmath>

namespace rolo {

DataValues parseDataValues(std::string_view text)
{
    DataValues values;
    if (text.empty()) {
        return values;
    }
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);   // (false: no exceptions)
    if (!j.is_object()) {
        return values;
    }
    for (const auto& [name, value] : j.items()) {
        if (value.is_boolean()) {
            values[name] = value.get<bool>();
        } else if (value.is_number()) {
            values[name] = value.get<double>();
        } else if (value.is_string()) {
            values[name] = value.get<std::string>();
        }
    }
    return values;
}

std::string writeDataValues(const DataValues& values)
{
    if (values.empty()) {
        return {};
    }
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [name, value] : values) {
        if (const bool* b = std::get_if<bool>(&value)) {
            j[name] = *b;
        } else if (const double* d = std::get_if<double>(&value)) {
            // Whole numbers are written as such: 120, not 120.0.
            if (std::isfinite(*d) && *d == std::floor(*d) && std::fabs(*d) < 9.0e15) {
                j[name] = static_cast<long long>(*d);
            } else {
                j[name] = std::isfinite(*d) ? *d : 0.0;
            }
        } else {
            j[name] = std::get<std::string>(value);
        }
    }
    return j.dump();
}

} // namespace rolo
