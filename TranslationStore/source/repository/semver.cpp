#include "semver.hpp"

#include <cctype>

namespace semver {

bool parse(const std::string &text, Version &out)
{
    long parts[3] = {0, 0, 0};
    int count = 0;
    size_t index = 0;
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index]))) {
        ++index;
    }
    if (index < text.size() && (text[index] == 'v' || text[index] == 'V')) {
        ++index;
    }
    while (count < 3) {
        if (index >= text.size() ||
            !std::isdigit(static_cast<unsigned char>(text[index]))) {
            return false;
        }
        long value = 0;
        while (index < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[index]))) {
            value = value * 10 + (text[index] - '0');
            if (value > 999999) {
                return false;
            }
            ++index;
        }
        parts[count++] = value;
        if (index >= text.size() || text[index] == '-' || text[index] == '+') {
            break;
        }
        if (text[index] != '.') {
            return false;
        }
        ++index;
    }
    if (index < text.size() && text[index] != '-' && text[index] != '+') {
        return false;
    }
    out.major = parts[0];
    out.minor = parts[1];
    out.patch = parts[2];
    return true;
}

bool isValid(const std::string &text)
{
    Version ignored;
    return parse(text, ignored);
}

int compare(const std::string &a, const std::string &b)
{
    Version left;
    Version right;
    parse(a, left);
    parse(b, right);
    const long l[3] = {left.major, left.minor, left.patch};
    const long r[3] = {right.major, right.minor, right.patch};
    for (int i = 0; i < 3; ++i) {
        if (l[i] != r[i]) {
            return l[i] < r[i] ? -1 : 1;
        }
    }
    return 0;
}

} // namespace semver
