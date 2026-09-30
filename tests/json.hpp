#pragma once

#include <cctype>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Minimal JSON for the test fixtures — objects/arrays/strings/numbers only.
// All fixture values are "0x..." strings or scalars; _info is skipped over.

struct Json {
    enum T { Obj, Arr, Str, Num, Bool, Null } t = Null;
    std::map<std::string, Json> o;
    std::vector<Json>           a;
    std::string                 s;
    double                      n = 0;

    const Json& operator[](const std::string& k) const {
        static const Json nil;
        auto it = o.find(k);
        return it == o.end() ? nil : it->second;
    }
    const Json& operator[](std::size_t i) const { return a.at(i); }
    bool has(const std::string& k) const { return o.count(k) != 0; }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : p_(src.c_str()) { skip_ws(); }
    Json run() { Json v = value(); skip_ws(); return v; }

private:
    const char* p_;

    void skip_ws() { while (*p_ && std::isspace((unsigned char)*p_)) ++p_; }
    char peek() { skip_ws(); return *p_; }
    [[noreturn]] void fail() { throw std::runtime_error(std::string("json parse near: ") + p_); }

    Json value() {
        switch (peek()) {
            case '{': return object();
            case '[': return array();
            case '"': return str();
            case 't': case 'f': return boolean();
            case 'n': return null();
            default:  return number();
        }
    }

    Json object() {
        Json v; v.t = Json::Obj;
        ++p_;                                    // '{'
        if (peek() == '}') { ++p_; return v; }
        for (;;) {
            if (peek() != '"') fail();
            std::string k = str().s;
            if (peek() != ':') fail();
            ++p_;
            v.o.emplace(std::move(k), value());
            char c = peek();
            if (c == ',') { ++p_; continue; }
            if (c == '}') { ++p_; return v; }
            fail();
        }
    }

    Json array() {
        Json v; v.t = Json::Arr;
        ++p_;                                    // '['
        if (peek() == ']') { ++p_; return v; }
        for (;;) {
            v.a.push_back(value());
            char c = peek();
            if (c == ',') { ++p_; continue; }
            if (c == ']') { ++p_; return v; }
            fail();
        }
    }

    Json str() {
        Json v; v.t = Json::Str;
        ++p_;                                    // '"'
        while (*p_ && *p_ != '"') {
            if (*p_ == '\\') {
                ++p_;
                switch (*p_) {
                    case 'n': v.s += '\n'; break;
                    case 't': v.s += '\t'; break;
                    case 'r': v.s += '\r'; break;
                    case 'b': v.s += '\b'; break;
                    case 'f': v.s += '\f'; break;
                    case 'u': {                // \uXXXX -> keep ASCII range
                        unsigned cp = 0;
                        for (int i = 0; i < 4 && p_[1]; ++i) {
                            ++p_;
                            cp = cp * 16 + (std::isdigit((unsigned char)*p_)
                                ? *p_ - '0' : std::tolower((unsigned char)*p_) - 'a' + 10);
                        }
                        v.s += (char)(cp & 0xff);
                        break;
                    }
                    default:  v.s += *p_; break;
                }
                ++p_;
            } else {
                v.s += *p_++;
            }
        }
        if (*p_ != '"') fail();
        ++p_;
        return v;
    }

    Json number() {
        Json v; v.t = Json::Num;
        char* end = nullptr;
        v.n = std::strtod(p_, &end);
        if (end == p_) fail();
        p_ = end;
        return v;
    }

    Json boolean() {
        Json v; v.t = Json::Bool;
        v.n = (*p_ == 't') ? 1 : 0;
        p_ += (*p_ == 't') ? 4 : 5;
        return v;
    }

    Json null() { p_ += 4; return Json{}; }
};

inline Json parse_json(const std::string& src) { return JsonParser(src).run(); }
