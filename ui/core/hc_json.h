// Hex Chain native X11 UI — a minimal JSON reader/writer for the App panels (the
// plugins' patch:Set strings: Scratch Pad's #pattern/#status/#waveform/#userlib, Hex
// Forge's preset and rig stores). Tolerant: bad input yields a null value, never throws.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hc {

class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json parse(const std::string& text);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Null; }
    bool isObject() const { return type_ == Object; }
    bool isArray() const { return type_ == Array; }
    bool isString() const { return type_ == String; }
    bool isNumber() const { return type_ == Number; }

    double             num(double def = 0) const { return type_ == Number ? n_ : type_ == Bool ? (b_ ? 1 : 0) : def; }
    int                integer(int def = 0) const { return type_ == Number || type_ == Bool ? int(num()) : def; }
    bool               boolean() const { return type_ == Bool ? b_ : type_ == Number ? n_ != 0 : false; }
    const std::string& str() const { return s_; }

    // Object access (a missing key gives a shared null) and array access.
    const Json& operator[](const char* key) const;
    const Json& operator[](size_t i) const;
    bool        has(const char* key) const { return type_ == Object && obj_.count(key); }
    size_t      size() const { return type_ == Array ? arr_.size() : type_ == Object ? obj_.size() : 0; }
    const std::vector<Json>&           items() const { return arr_; }
    const std::map<std::string, Json>& members() const { return obj_; }

    // Building.
    static Json number(double v);
    static Json string(std::string v);
    static Json boolean(bool v);
    static Json array();
    static Json object();
    Json& push(Json v);
    Json& set(const std::string& key, Json v);

    std::string dump() const;

private:
    Type                        type_ = Null;
    bool                        b_ = false;
    double                      n_ = 0;
    std::string                 s_;
    std::vector<Json>           arr_;
    std::map<std::string, Json> obj_;
    std::vector<std::string>    order_;   // object key order for dump()

    friend struct JsonParser;
};

// JSON string literal (with quotes and escapes).
std::string jsonQuote(const std::string& s);
// Standard base64 decode, skipping '=' and anything outside the alphabet.
std::vector<uint8_t> base64Decode(const std::string& s);

} // namespace hc
