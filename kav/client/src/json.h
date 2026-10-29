#pragma once
// A small JSON reader for the KosmosAV server's replies (objects, arrays, strings, numbers, booleans, null). Not a general library.
#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace kav
{
    struct Json
    {
        enum Kind { null_v, bool_v, num_v, str_v, arr_v, obj_v } kind = null_v;
        bool b = false;
        double n = 0;
        std::string s;
        std::vector<Json> a;
        std::map<std::string, Json> o;

        const Json &operator[](const std::string &k) const
        {
            static const Json none;
            auto it = o.find(k);
            return it == o.end() ? none : it->second;
        }
        std::string str(const std::string &fallback = "") const { return kind == str_v ? s : fallback; }
        double num(double fallback = 0) const { return kind == num_v ? n : fallback; }
        long integer(long fallback = 0) const { return kind == num_v ? (long)n : fallback; }
    };

    class JsonParser
    {
    public:
        explicit JsonParser(const std::string &t) : t_(t) {}
        bool parse(Json &out)
        {
            ws();
            if (!value(out))
                return false;
            ws();
            return p_ == t_.size();
        }

    private:
        const std::string &t_;
        size_t p_ = 0;

        void ws() { while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\n' || t_[p_] == '\r' || t_[p_] == '\t')) ++p_; }
        bool lit(const char *w) { size_t n = std::char_traits<char>::length(w); if (t_.compare(p_, n, w) != 0) return false; p_ += n; return true; }

        bool string(std::string &out)
        {
            if (p_ >= t_.size() || t_[p_] != '"')
                return false;
            ++p_;
            while (p_ < t_.size() && t_[p_] != '"')
            {
                char c = t_[p_++];
                if (c == '\\' && p_ < t_.size())
                {
                    char e = t_[p_++];
                    switch (e)
                    {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'u': // the server never sends these; keep a placeholder
                        p_ = std::min(p_ + 4, t_.size());
                        c = '?';
                        break;
                    default: c = e;
                    }
                }
                out += c;
            }
            if (p_ >= t_.size())
                return false;
            ++p_;
            return true;
        }

        bool value(Json &v)
        {
            if (p_ >= t_.size())
                return false;
            char c = t_[p_];
            if (c == '{')
            {
                ++p_;
                v.kind = Json::obj_v;
                ws();
                if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
                for (;;)
                {
                    ws();
                    std::string k;
                    if (!string(k))
                        return false;
                    ws();
                    if (p_ >= t_.size() || t_[p_++] != ':')
                        return false;
                    ws();
                    if (!value(v.o[k]))
                        return false;
                    ws();
                    if (p_ < t_.size() && t_[p_] == ',') { ++p_; continue; }
                    if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
                    return false;
                }
            }
            if (c == '[')
            {
                ++p_;
                v.kind = Json::arr_v;
                ws();
                if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
                for (;;)
                {
                    ws();
                    v.a.emplace_back();
                    if (!value(v.a.back()))
                        return false;
                    ws();
                    if (p_ < t_.size() && t_[p_] == ',') { ++p_; continue; }
                    if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
                    return false;
                }
            }
            if (c == '"') { v.kind = Json::str_v; return string(v.s); }
            if (lit("true")) { v.kind = Json::bool_v; v.b = true; return true; }
            if (lit("false")) { v.kind = Json::bool_v; return true; }
            if (lit("null")) return true;
            char *end = nullptr;
            v.n = std::strtod(t_.c_str() + p_, &end);
            if (end == t_.c_str() + p_)
                return false;
            v.kind = Json::num_v;
            p_ = (size_t)(end - t_.c_str());
            return true;
        }
    };

    inline bool parse_json(const std::string &text, Json &out) { return JsonParser(text).parse(out); }
}
