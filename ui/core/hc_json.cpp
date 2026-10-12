// Hex Chain native X11 UI — minimal JSON (see hc_json.h).
#include "hc_json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hc {

namespace {
const Json kNull;
}

struct JsonParser {
    const std::string& t;
    size_t             i = 0;
    int                depth = 0;

    void ws()
    {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r'))
            ++i;
    }
    bool lit(const char* w)
    {
        size_t n = 0;
        while (w[n])
            ++n;
        if (t.compare(i, n, w) != 0)
            return false;
        i += n;
        return true;
    }
    static void utf8(std::string& o, unsigned cp)
    {
        if (cp < 0x80) {
            o += char(cp);
        } else if (cp < 0x800) {
            o += char(0xC0 | (cp >> 6));
            o += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += char(0xE0 | (cp >> 12));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        } else {
            o += char(0xF0 | (cp >> 18));
            o += char(0x80 | ((cp >> 12) & 0x3F));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& cp)
    {
        if (i + 4 > t.size())
            return false;
        cp = unsigned(std::strtoul(t.substr(i, 4).c_str(), nullptr, 16));
        i += 4;
        return true;
    }
    bool str(std::string& o)
    {
        if (i >= t.size() || t[i] != '"')
            return false;
        ++i;
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c != '\\') {
                o += c;
                continue;
            }
            if (i >= t.size())
                return false;
            c = t[i++];
            switch (c) {
            case 'n': o += '\n'; break;
            case 't': o += '\t'; break;
            case 'r': o += '\r'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp))
                    return false;
                if (cp >= 0xD800 && cp < 0xDC00 && t.compare(i, 2, "\\u") == 0) {
                    i += 2;
                    unsigned lo = 0;
                    if (!hex4(lo))
                        return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(o, cp);
                break;
            }
            default: o += c; break;
            }
        }
        if (i >= t.size())
            return false;
        ++i;
        return true;
    }
    bool value(Json& v)
    {
        if (++depth > 64)
            return false;
        ws();
        if (i >= t.size())
            return false;
        const char c = t[i];
        bool ok = true;
        if (c == '{') {
            ++i;
            v.type_ = Json::Object;
            ws();
            if (i < t.size() && t[i] == '}') {
                ++i;
            } else {
                for (;;) {
                    ws();
                    std::string k;
                    if (!str(k))
                        return false;
                    ws();
                    if (i >= t.size() || t[i] != ':')
                        return false;
                    ++i;
                    Json m;
                    if (!value(m))
                        return false;
                    if (!v.obj_.count(k))
                        v.order_.push_back(k);
                    v.obj_[k] = std::move(m);
                    ws();
                    if (i < t.size() && t[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < t.size() && t[i] == '}') {
                        ++i;
                        break;
                    }
                    return false;
                }
            }
        } else if (c == '[') {
            ++i;
            v.type_ = Json::Array;
            ws();
            if (i < t.size() && t[i] == ']') {
                ++i;
            } else {
                for (;;) {
                    Json m;
                    if (!value(m))
                        return false;
                    v.arr_.push_back(std::move(m));
                    ws();
                    if (i < t.size() && t[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < t.size() && t[i] == ']') {
                        ++i;
                        break;
                    }
                    return false;
                }
            }
        } else if (c == '"') {
            v.type_ = Json::String;
            ok = str(v.s_);
        } else if (lit("true")) {
            v.type_ = Json::Bool;
            v.b_ = true;
        } else if (lit("false")) {
            v.type_ = Json::Bool;
        } else if (lit("null")) {
            v.type_ = Json::Null;
        } else {
            const char* start = t.c_str() + i;
            char*       end   = nullptr;
            const double d    = std::strtod(start, &end);
            if (end == start)
                return false;
            i += size_t(end - start);
            v.type_ = Json::Number;
            v.n_ = d;
        }
        --depth;
        return ok;
    }
};

Json Json::parse(const std::string& text)
{
    JsonParser p { text };
    Json v;
    if (!p.value(v))
        return Json();
    return v;
}

const Json& Json::operator[](const char* key) const
{
    if (type_ != Object)
        return kNull;
    auto it = obj_.find(key);
    return it != obj_.end() ? it->second : kNull;
}

const Json& Json::operator[](size_t i) const
{
    return type_ == Array && i < arr_.size() ? arr_[i] : kNull;
}

Json Json::number(double v)
{
    Json j;
    j.type_ = Number;
    j.n_ = v;
    return j;
}
Json Json::string(std::string v)
{
    Json j;
    j.type_ = String;
    j.s_ = std::move(v);
    return j;
}
Json Json::boolean(bool v)
{
    Json j;
    j.type_ = Bool;
    j.b_ = v;
    return j;
}
Json Json::array()
{
    Json j;
    j.type_ = Array;
    return j;
}
Json Json::object()
{
    Json j;
    j.type_ = Object;
    return j;
}
Json& Json::push(Json v)
{
    type_ = Array;
    arr_.push_back(std::move(v));
    return *this;
}
Json& Json::set(const std::string& key, Json v)
{
    type_ = Object;
    if (!obj_.count(key))
        order_.push_back(key);
    obj_[key] = std::move(v);
    return *this;
}

std::string jsonQuote(const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
            } else {
                o += char(c);
            }
        }
    }
    return o + "\"";
}

std::string Json::dump() const
{
    switch (type_) {
    case Null:   return "null";
    case Bool:   return b_ ? "true" : "false";
    case Number: {
        if (std::fabs(n_ - std::round(n_)) < 1e-9 && std::fabs(n_) < 1e15)
            return std::to_string(static_cast<long long>(std::llround(n_)));
        char b[32];
        std::snprintf(b, sizeof b, "%.6g", n_);
        return b;
    }
    case String: return jsonQuote(s_);
    case Array: {
        std::string o = "[";
        for (size_t k = 0; k < arr_.size(); ++k)
            o += (k ? "," : "") + arr_[k].dump();
        return o + "]";
    }
    case Object: {
        std::string o = "{";
        bool first = true;
        for (const auto& k : order_) {
            o += (first ? "" : ",") + jsonQuote(k) + ":" + obj_.at(k).dump();
            first = false;
        }
        return o + "}";
    }
    }
    return "null";
}

std::vector<uint8_t> base64Decode(const std::string& s)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    unsigned acc = 0;
    int      bits = 0;
    for (char c : s) {
        const int v = val(c);
        if (v < 0)
            continue;
        acc = (acc << 6) | unsigned(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((acc >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace hc
