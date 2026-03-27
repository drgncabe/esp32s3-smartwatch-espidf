#pragma once

#include <cstdint>
#include <type_traits>

/** Unordered hash of integral arguments. Expects chars, not char*. */
template <typename T, typename... Args>
inline T get_unordered_hash(Args... args) {
    static_assert(std::is_integral_v<T>, "Return type must be integral");
    T hash = 0;
    auto mix = [](uint8_t v) {
        v ^= v >> 4;
        v *= 0x27;
        v ^= v >> 3;
        return v;
    };
    ((hash ^= static_cast<T>(mix(static_cast<uint8_t>(args)))), ...);
    return hash;
}
