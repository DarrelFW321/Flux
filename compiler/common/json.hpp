#pragma once
#include <cstdio>
#include <string>
#include <vector>

// Minimal JSON writing helpers — the compiler only ever produces JSON.
namespace json {

inline std::string str(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out + "\"";
}

inline std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

inline std::string num(int v) { return std::to_string(v); }
inline std::string boolean(bool b) { return b ? "true" : "false"; }

template <class T, class F>
std::string array(const std::vector<T>& items, F&& fn) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += fn(items[i]);
    }
    return out + "]";
}

inline std::string ints(const std::vector<int>& v) {
    return array(v, [](int x) { return std::to_string(x); });
}

// {"k":v,...} from pre-encoded values.
inline std::string object(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string out = "{";
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) out += ",";
        out += str(fields[i].first) + ":" + fields[i].second;
    }
    return out + "}";
}

} // namespace json
