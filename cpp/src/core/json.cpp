#include "json.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace qseq::json {

const Value *Value::get(const std::string &key) const
{
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
}

namespace {

struct Parser {
    const std::string &s;
    size_t i = 0;

    void ws()
    {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            i++;
    }
    bool lit(const char *w)
    {
        size_t n = std::char_traits<char>::length(w);
        if (s.compare(i, n, w) != 0)
            return false;
        i += n;
        return true;
    }
    static void utf8(std::string &out, unsigned cp)
    {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool string(std::string &out)
    {
        if (i >= s.size() || s[i] != '"')
            return false;
        i++;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size())
                return false;
            char e = s[i++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':
                if (i + 4 > s.size())
                    return false;
                utf8(out, static_cast<unsigned>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16)));
                i += 4;
                break;
            default: out += e; break;
            }
        }
        if (i >= s.size())
            return false;
        i++;
        return true;
    }
    bool value(Value &v, int depth)
    {
        if (depth > 32)
            return false;
        ws();
        if (i >= s.size())
            return false;
        char c = s[i];
        if (c == '{') {
            v.type = Value::Object;
            i++;
            ws();
            if (i < s.size() && s[i] == '}') {
                i++;
                return true;
            }
            for (;;) {
                ws();
                std::string key;
                if (!string(key))
                    return false;
                ws();
                if (i >= s.size() || s[i] != ':')
                    return false;
                i++;
                if (!value(v.obj[key], depth + 1))
                    return false;
                ws();
                if (i < s.size() && s[i] == ',') {
                    i++;
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    i++;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            v.type = Value::Array;
            i++;
            ws();
            if (i < s.size() && s[i] == ']') {
                i++;
                return true;
            }
            for (;;) {
                v.arr.emplace_back();
                if (!value(v.arr.back(), depth + 1))
                    return false;
                ws();
                if (i < s.size() && s[i] == ',') {
                    i++;
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    i++;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            v.type = Value::String;
            return string(v.str);
        }
        if (lit("true")) {
            v.type = Value::Bool;
            v.b = true;
            return true;
        }
        if (lit("false")) {
            v.type = Value::Bool;
            return true;
        }
        if (lit("null"))
            return true;
        char *end = nullptr;
        v.num = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i)
            return false;
        v.type = Value::Number;
        i = static_cast<size_t>(end - s.c_str());
        return true;
    }
};

} // namespace

bool parse(const std::string &text, Value &out)
{
    Parser p{text};
    out = Value{};
    if (!p.value(out, 0))
        return false;
    p.ws();
    return p.i == text.size();
}

std::string quote(const std::string &s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                o += buf;
            } else {
                o += static_cast<char>(c);
            }
        }
    }
    return o + "\"";
}

} // namespace qseq::json
