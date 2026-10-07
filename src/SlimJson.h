#pragma once

// Compact read-only JSON DOM for Discord gateway payloads.
//
// Windows.Data.Json turns every value into a COM object (~27x the text size: a 2 MB READY costs
// ~55 MB). Here the whole document is one std::wstring (the source) plus one flat vector of
// 28-byte nodes; strings and numbers are decoded on demand and everything is freed in one go.
namespace DiscordWin3::Slim
{
    enum class Type : uint8_t { Null, False, True, Number, String, Object, Array };

    struct Node
    {
        uint32_t keyPos = 0;     // member name (inside the source), objects only
        uint32_t keyLen = 0;
        uint32_t pos = 0;        // value span in the source (strings: without quotes)
        uint32_t len = 0;
        uint32_t first = 0;      // first child (containers), 0 = none
        uint32_t next = 0;       // next sibling, 0 = none
        Type type = Type::Null;
        bool escaped = false;    // string value contains backslash escapes
        bool keyEscaped = false;
    };

    class Document;

    class Value
    {
    public:
        Value() = default;
        Value(Document const* doc, uint32_t index) : m_doc(doc), m_index(index) {}

        Type GetType() const;
        bool IsNull() const { return GetType() == Type::Null; }
        bool IsObject() const { return GetType() == Type::Object; }
        bool IsArray() const { return GetType() == Type::Array; }
        bool IsString() const { return GetType() == Type::String; }
        bool IsNumber() const { return GetType() == Type::Number; }
        explicit operator bool() const { return m_doc && !IsNull(); }

        Value operator[](std::wstring_view key) const;   // object member, null Value if missing
        std::wstring Str() const;                         // strings (numbers are returned as their text)
        double Num(double fallback = 0) const;
        bool Bool(bool fallback = false) const;
        std::wstring Raw() const;                         // exact source text of this value (for re-parsing)

        uint32_t Size() const;                            // children count (arrays / objects)
        Value At(uint32_t index) const;                   // n-th child

        class Iterator
        {
        public:
            Iterator(Document const* doc, uint32_t index) : m_doc(doc), m_index(index) {}
            Value operator*() const { return { m_doc, m_index }; }
            Iterator& operator++();
            bool operator!=(Iterator const& other) const { return m_index != other.m_index; }
        private:
            Document const* m_doc;
            uint32_t m_index;
        };
        Iterator begin() const;
        Iterator end() const { return { m_doc, 0 }; }

    private:
        Node const* node() const;
        Document const* m_doc = nullptr;
        uint32_t m_index = 0;
    };

    class Document
    {
    public:
        // Returns nullptr on malformed input. Takes ownership of the text.
        static std::shared_ptr<Document> Parse(std::wstring text);

        Value Root() const { return { this, 1 }; }
        std::wstring const& Text() const { return m_text; }
        std::vector<Node> const& Nodes() const { return m_nodes; }

    private:
        friend class Parser;
        std::wstring m_text;
        std::vector<Node> m_nodes;   // [0] = shared "null" sentinel, [1] = root
    };
}
