#pragma once

#include "../SlimJson.h"

// Null-safe accessors over Windows.Data.Json (Discord sends `null` for many optional fields,
// and the stock GetNamedXxx calls throw on a type mismatch).
namespace DiscordWin3::Json
{
    using winrt::Windows::Data::Json::JsonArray;
    using winrt::Windows::Data::Json::JsonObject;
    using winrt::Windows::Data::Json::JsonValueType;
    using winrt::Windows::Data::Json::IJsonValue;

    inline IJsonValue Get(JsonObject const& o, std::wstring_view key)
    {
        if (!o)
        {
            return nullptr;
        }
        auto v = o.TryLookup(key);
        if (!v || v.ValueType() == JsonValueType::Null)
        {
            return nullptr;
        }
        return v;
    }

    inline std::wstring Str(JsonObject const& o, std::wstring_view key)
    {
        auto v = Get(o, key);
        if (!v)
        {
            return {};
        }
        if (v.ValueType() == JsonValueType::String)
        {
            return std::wstring{ v.GetString() };
        }
        if (v.ValueType() == JsonValueType::Number)
        {
            return std::to_wstring(static_cast<int64_t>(v.GetNumber()));
        }
        return {};
    }

    inline double Num(JsonObject const& o, std::wstring_view key, double fallback = 0)
    {
        auto v = Get(o, key);
        return (v && v.ValueType() == JsonValueType::Number) ? v.GetNumber() : fallback;
    }

    inline bool Bool(JsonObject const& o, std::wstring_view key, bool fallback = false)
    {
        auto v = Get(o, key);
        return (v && v.ValueType() == JsonValueType::Boolean) ? v.GetBoolean() : fallback;
    }

    inline JsonObject Obj(JsonObject const& o, std::wstring_view key)
    {
        auto v = Get(o, key);
        return (v && v.ValueType() == JsonValueType::Object) ? v.GetObject() : nullptr;
    }

    inline JsonArray Arr(JsonObject const& o, std::wstring_view key)
    {
        auto v = Get(o, key);
        return (v && v.ValueType() == JsonValueType::Array) ? v.GetArray() : nullptr;
    }

    // Discord snowflakes / permission bitfields arrive as decimal strings.
    inline uint64_t U64(std::wstring_view s)
    {
        uint64_t r = 0;
        for (wchar_t c : s)
        {
            if (c < L'0' || c > L'9')
            {
                break;
            }
            r = r * 10 + static_cast<uint64_t>(c - L'0');
        }
        return r;
    }

    inline uint64_t U64(JsonObject const& o, std::wstring_view key)
    {
        return U64(Str(o, key));
    }

    // ---- Same helpers over the compact DOM (gateway payloads) ----

    inline Slim::Value Get(Slim::Value o, std::wstring_view key)
    {
        auto v = o[key];
        return v ? v : Slim::Value{};
    }
    inline std::wstring Str(Slim::Value o, std::wstring_view key) { return o[key].Str(); }
    inline double Num(Slim::Value o, std::wstring_view key, double fallback = 0)
    {
        auto v = o[key];
        return v.IsNumber() ? v.Num(fallback) : fallback;
    }
    inline bool Bool(Slim::Value o, std::wstring_view key, bool fallback = false) { return o[key].Bool(fallback); }
    inline Slim::Value Obj(Slim::Value o, std::wstring_view key)
    {
        auto v = o[key];
        return v.IsObject() ? v : Slim::Value{};
    }
    inline Slim::Value Arr(Slim::Value o, std::wstring_view key)
    {
        auto v = o[key];
        return v.IsArray() ? v : Slim::Value{};
    }
    inline uint64_t U64(Slim::Value o, std::wstring_view key) { return U64(Str(o, key)); }

    // Element helpers usable from code templated on either JSON flavor.
    inline bool IsObject(IJsonValue const& v) { return v && v.ValueType() == JsonValueType::Object; }
    inline JsonObject AsObject(IJsonValue const& v) { return v.GetObject(); }
    inline bool IsString(IJsonValue const& v) { return v && v.ValueType() == JsonValueType::String; }
    inline std::wstring AsString(IJsonValue const& v) { return std::wstring{ v.GetString() }; }
    inline bool IsObject(Slim::Value const& v) { return v.IsObject(); }
    inline Slim::Value AsObject(Slim::Value const& v) { return v; }
    inline bool IsString(Slim::Value const& v) { return v.IsString(); }
    inline std::wstring AsString(Slim::Value const& v) { return v.Str(); }

    // Snowflake -> unix milliseconds.
    inline int64_t SnowflakeMs(std::wstring_view id)
    {
        return static_cast<int64_t>(U64(id) >> 22) + 1420070400000LL;
    }

    // Numeric comparison of decimal snowflake strings.
    inline bool SnowflakeLess(std::wstring_view a, std::wstring_view b)
    {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    }
}
