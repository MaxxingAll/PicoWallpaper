#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace picowp {

inline std::string HResultHex(uint32_t value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result = "0x00000000";
    for (int i = 9; i >= 2; --i) {
        result[i] = digits[value & 0xF];
        value >>= 4;
    }
    return result;
}

inline std::wstring IntDecimal(int value) {
    wchar_t buffer[std::numeric_limits<int>::digits10 + 3] = {};
    wchar_t* end = buffer + sizeof(buffer) / sizeof(buffer[0]) - 1;
    wchar_t* current = end;
    unsigned int magnitude = static_cast<unsigned int>(value);
    if (value < 0) magnitude = 0u - magnitude;
    do {
        *--current = static_cast<wchar_t>(L'0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    if (value < 0) *--current = L'-';
    return std::wstring(current, end);
}

} // namespace picowp
