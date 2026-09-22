#pragma once
#include "Types.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace ludo::core::text {

inline Text trim(TextView input) {
    auto first = input.begin();
    auto last = input.end();
    while (first != last && std::isspace(static_cast<unsigned char>(*first))) ++first;
    while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1)))) --last;
    return Text(first, last);
}

inline Text lowerAscii(TextView input) {
    Text out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    return out;
}

inline bool iequalsAscii(TextView a, TextView b) {
    return lowerAscii(a) == lowerAscii(b);
}

inline std::vector<Text> split(TextView input, char delimiter) {
    std::vector<Text> out;
    std::stringstream stream{Text(input)};
    Text part;
    while (std::getline(stream, part, delimiter)) out.push_back(part);
    return out;
}

} // namespace ludo::core::text
