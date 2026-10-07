#include "pch.h"
#include "SlimJson.h"

namespace DiscordWin3::Slim
{
    namespace
    {
        bool IsSpace(wchar_t c) { return c == L' ' || c == L'\n' || c == L'\r' || c == L'\t'; }

        void AppendUnescaped(std::wstring& out, std::wstring_view s)
        {
            out.reserve(out.size() + s.size());
            for (size_t i = 0; i < s.size(); ++i)
            {
                wchar_t c = s[i];
                if (c != L'\\' || i + 1 >= s.size())
                {
                    out.push_back(c);
                    continue;
                }
                wchar_t e = s[++i];
                switch (e)
                {
                case L'n': out.push_back(L'\n'); break;
                case L'r': out.push_back(L'\r'); break;
                case L't': out.push_back(L'\t'); break;
                case L'b': out.push_back(L'\b'); break;
                case L'f': out.push_back(L'\f'); break;
                case L'u':
                    if (i + 4 < s.size())
                    {
                        wchar_t code = 0;
                        for (int k = 1; k <= 4; ++k)
                        {
                            wchar_t h = s[i + k];
                            code = static_cast<wchar_t>(code * 16 + (h >= L'0' && h <= L'9' ? h - L'0'
                                                                   : h >= L'a' && h <= L'f' ? h - L'a' + 10
                                                                   : h >= L'A' && h <= L'F' ? h - L'A' + 10 : 0));
                        }
                        out.push_back(code);   // UTF-16 code unit; surrogate pairs arrive as two escapes
                        i += 4;
                    }
                    break;
                default: out.push_back(e); break;   // \" \\ \/
                }
            }
        }
    }

    class Parser
    {
    public:
        explicit Parser(Document& doc) : m_doc(doc), m_s(doc.m_text) {}

        bool Run()
        {
            m_doc.m_nodes.reserve(m_s.size() / 12 + 16);   // ~1 value per 12 chars in Discord payloads
            m_doc.m_nodes.emplace_back();                  // [0] null sentinel
            SkipSpace();
            return ParseValue() == 1;
        }

    private:
        void SkipSpace() { while (m_i < m_s.size() && IsSpace(m_s[m_i])) ++m_i; }

        // Returns the index just after the closing quote; sets `escaped`.
        bool ScanString(uint32_t& start, uint32_t& length, bool& escaped)
        {
            if (m_i >= m_s.size() || m_s[m_i] != L'"') return false;
            start = static_cast<uint32_t>(++m_i);
            escaped = false;
            while (m_i < m_s.size())
            {
                wchar_t c = m_s[m_i];
                if (c == L'\\') { escaped = true; m_i += 2; continue; }
                if (c == L'"')
                {
                    length = static_cast<uint32_t>(m_i - start);
                    ++m_i;
                    return true;
                }
                ++m_i;
            }
            return false;
        }

        uint32_t ParseValue()
        {
            if (m_i >= m_s.size()) return 0;
            auto index = static_cast<uint32_t>(m_doc.m_nodes.size());
            m_doc.m_nodes.emplace_back();
            wchar_t c = m_s[m_i];

            if (c == L'{' || c == L'[')
            {
                bool object = c == L'{';
                wchar_t close = object ? L'}' : L']';
                m_doc.m_nodes[index].type = object ? Type::Object : Type::Array;
                m_doc.m_nodes[index].pos = static_cast<uint32_t>(m_i);
                ++m_i;
                SkipSpace();
                uint32_t last = 0;
                uint32_t count = 0;
                if (m_i < m_s.size() && m_s[m_i] == close)
                {
                    ++m_i;
                }
                else
                {
                    for (;;)
                    {
                        uint32_t keyPos = 0, keyLen = 0;
                        bool keyEscaped = false;
                        if (object)
                        {
                            if (!ScanString(keyPos, keyLen, keyEscaped)) return 0;
                            SkipSpace();
                            if (m_i >= m_s.size() || m_s[m_i] != L':') return 0;
                            ++m_i;
                            SkipSpace();
                        }
                        uint32_t child = ParseValue();
                        if (!child) return 0;
                        auto& n = m_doc.m_nodes[child];
                        n.keyPos = keyPos;
                        n.keyLen = keyLen;
                        n.keyEscaped = keyEscaped;
                        if (last) m_doc.m_nodes[last].next = child;
                        else m_doc.m_nodes[index].first = child;
                        last = child;
                        ++count;
                        SkipSpace();
                        if (m_i < m_s.size() && m_s[m_i] == L',') { ++m_i; SkipSpace(); continue; }
                        if (m_i < m_s.size() && m_s[m_i] == close) { ++m_i; break; }
                        return 0;
                    }
                }
                m_doc.m_nodes[index].len = static_cast<uint32_t>(m_i) - m_doc.m_nodes[index].pos;
                return index;
            }
            if (c == L'"')
            {
                uint32_t start = 0, length = 0;
                bool escaped = false;
                if (!ScanString(start, length, escaped)) return 0;
                auto& n = m_doc.m_nodes[index];
                n.type = Type::String;
                n.pos = start;
                n.len = length;
                n.escaped = escaped;
                return index;
            }

            // Literal / number: read the token.
            auto start = m_i;
            while (m_i < m_s.size() && !IsSpace(m_s[m_i]) && m_s[m_i] != L',' && m_s[m_i] != L'}' && m_s[m_i] != L']') ++m_i;
            std::wstring_view token = std::wstring_view{ m_s }.substr(start, m_i - start);
            auto& n = m_doc.m_nodes[index];
            n.pos = static_cast<uint32_t>(start);
            n.len = static_cast<uint32_t>(token.size());
            if (token == L"null") n.type = Type::Null;
            else if (token == L"true") n.type = Type::True;
            else if (token == L"false") n.type = Type::False;
            else if (!token.empty()) n.type = Type::Number;
            else return 0;
            return index;
        }

        Document& m_doc;
        std::wstring const& m_s;
        size_t m_i = 0;
    };

    std::shared_ptr<Document> Document::Parse(std::wstring text)
    {
        auto doc = std::make_shared<Document>();
        doc->m_text = std::move(text);
        Parser parser{ *doc };
        if (!parser.Run()) return nullptr;
        doc->m_nodes.shrink_to_fit();
        return doc;
    }

    Node const* Value::node() const
    {
        return m_doc ? &m_doc->Nodes()[m_index] : nullptr;
    }

    Type Value::GetType() const
    {
        auto n = node();
        return n ? n->type : Type::Null;
    }

    Value Value::operator[](std::wstring_view key) const
    {
        auto n = node();
        if (!n || n->type != Type::Object) return {};
        auto const& nodes = m_doc->Nodes();
        std::wstring_view text = m_doc->Text();
        for (uint32_t c = n->first; c; c = nodes[c].next)
        {
            auto const& child = nodes[c];
            if (!child.keyEscaped)
            {
                if (text.substr(child.keyPos, child.keyLen) == key) return { m_doc, c };
            }
            else
            {
                std::wstring k;
                AppendUnescaped(k, text.substr(child.keyPos, child.keyLen));
                if (k == key) return { m_doc, c };
            }
        }
        return {};
    }

    std::wstring Value::Str() const
    {
        auto n = node();
        if (!n || (n->type != Type::String && n->type != Type::Number)) return {};
        std::wstring_view raw = std::wstring_view{ m_doc->Text() }.substr(n->pos, n->len);
        if (!n->escaped) return std::wstring{ raw };
        std::wstring out;
        AppendUnescaped(out, raw);
        return out;
    }

    double Value::Num(double fallback) const
    {
        auto n = node();
        if (!n || n->type != Type::Number || n->len >= 64) return fallback;
        wchar_t buf[64];
        wmemcpy(buf, m_doc->Text().data() + n->pos, n->len);
        buf[n->len] = 0;
        return wcstod(buf, nullptr);
    }

    bool Value::Bool(bool fallback) const
    {
        auto t = GetType();
        return t == Type::True ? true : t == Type::False ? false : fallback;
    }

    std::wstring Value::Raw() const
    {
        auto n = node();
        if (!n) return L"null";
        if (n->type == Type::String) return std::wstring{ std::wstring_view{ m_doc->Text() }.substr(n->pos - 1, n->len + 2) };
        return std::wstring{ std::wstring_view{ m_doc->Text() }.substr(n->pos, n->len) };
    }

    uint32_t Value::Size() const
    {
        uint32_t count = 0;
        for (auto it = begin(); it != end(); ++it) ++count;
        return count;
    }

    Value Value::At(uint32_t index) const
    {
        for (auto it = begin(); it != end(); ++it)
        {
            if (index-- == 0) return *it;
        }
        return {};
    }

    Value::Iterator& Value::Iterator::operator++()
    {
        m_index = m_doc->Nodes()[m_index].next;
        return *this;
    }

    Value::Iterator Value::begin() const
    {
        auto n = node();
        return { m_doc, (n && (n->type == Type::Object || n->type == Type::Array)) ? n->first : 0u };
    }
}
