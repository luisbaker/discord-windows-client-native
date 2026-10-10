#include "pch.h"
#include "Theme.h"
#include "Strings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Media;

namespace DiscordWin3::Theme
{
    namespace
    {
        struct Palette
        {
            wchar_t const* code;
            wchar_t const* name;
            uint32_t frame;      // background-base-lowest: server rail, channel list, title bar
            uint32_t chat;       // background-base-lower: messages, member list
            uint32_t composer;   // chat input
            uint32_t panel;      // surface-higher: popups, hover bar, user panel
            uint32_t card;       // surface-high: embeds, cards
            uint32_t line;       // border-subtle
            uint32_t text;       // text-default
            uint32_t muted;      // text-muted
            uint32_t hover;      // background-mod-subtle over the frame
            uint32_t selected;   // background-mod-normal over the frame
        };

        constexpr Palette Palettes[] = {
            { L"dark",     L"Dark",     0x2C2D32, 0x323339, 0x393A41, 0x3C3D45, 0x36373E, 0x44454C, 0xF3F3F4, 0xABACB2, 0x36373E, 0x3F4048 },
            { L"darker",   L"Darker",   0x121214, 0x1A1A1E, 0x222327, 0x28282D, 0x242429, 0x2A2A2F, 0xEFEFF1, 0x96979E, 0x1E1E22, 0x2C2D32 },
            { L"midnight", L"Midnight", 0x000000, 0x000000, 0x101013, 0x121214, 0x0A0A0C, 0x26262A, 0xD4D5D8, 0x81828A, 0x151518, 0x222225 },
        };

        Kind g_current = Kind::Midnight;

        Windows::UI::Color ToColor(uint32_t rgb)
        {
            return { 255, static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb) };
        }

        void Apply(Palette const& p)
        {
            auto resources = Application::Current().Resources();
            auto set = [&](wchar_t const* key, uint32_t rgb)
            {
                if (auto brush = resources.TryLookup(box_value(key)).try_as<SolidColorBrush>())
                    brush.Color(ToColor(rgb));
            };
            set(L"RailBrush", p.frame);
            set(L"SidebarBrush", p.frame);
            set(L"ChatBrush", p.chat);
            set(L"ComposerBrush", p.composer);
            set(L"PanelBrush", p.panel);
            set(L"CardBrush", p.card);
            set(L"LineBrush", p.line);
            set(L"BodyTextBrush", p.text);
            set(L"MutedTextBrush", p.muted);
            set(L"ListViewItemBackgroundPointerOver", p.hover);
            set(L"ListViewItemBackgroundSelected", p.selected);
            set(L"ListViewItemBackgroundSelectedPointerOver", p.selected);
            set(L"ListViewItemBackgroundPressed", p.hover);
            set(L"ListViewItemBackgroundSelectedPressed", p.selected);
        }
    }

    void Initialize()
    {
        wchar_t saved[32]{};
        GetPrivateProfileStringW(L"ui", L"theme", L"midnight", saved, 32, I18n::SettingsFile().c_str());
        for (int i = 0; i < static_cast<int>(Kind::Count); ++i)
        {
            if (std::wstring_view{ saved } == Palettes[i].code) g_current = static_cast<Kind>(i);
        }
        Apply(Palettes[static_cast<int>(g_current)]);
    }

    void Set(Kind kind)
    {
        g_current = kind;
        WritePrivateProfileStringW(L"ui", L"theme", Palettes[static_cast<int>(kind)].code, I18n::SettingsFile().c_str());
        Apply(Palettes[static_cast<int>(kind)]);
    }

    Kind Current() { return g_current; }
    wchar_t const* Name(Kind kind) { return Palettes[static_cast<int>(kind)].name; }
}
