#include "pch.h"
#include "Strings.h"

#include <filesystem>
#include <shlobj.h>

namespace DiscordWin3::I18n
{
    namespace
    {
        constexpr int LangCount = static_cast<int>(Lang::Count);

        wchar_t const* const g_table[static_cast<int>(S::Count)][LangCount] = {
#define DISCORDWIN3_ROW(key, fr, en, ptpt, ptbr, es, de, it) { fr, en, ptpt, ptbr, es, de, it },
            DISCORDWIN3_STRINGS(DISCORDWIN3_ROW)
#undef DISCORDWIN3_ROW
        };

        struct LangInfo
        {
            wchar_t const* code;      // persisted value
            wchar_t const* native;
            wchar_t const* locale;
            wchar_t const* discord;
        };
        constexpr LangInfo g_langs[LangCount] = {
            { L"fr", L"Français", L"fr-FR", L"fr" },
            { L"en", L"English", L"en-US", L"en-US" },
            { L"pt-PT", L"Português (Portugal)", L"pt-PT", L"pt-BR" },   // Discord has no pt-PT locale
            { L"pt-BR", L"Português (Brasil)", L"pt-BR", L"pt-BR" },
            { L"es", L"Español", L"es-ES", L"es-ES" },
            { L"de", L"Deutsch", L"de-DE", L"de" },
            { L"it", L"Italiano", L"it-IT", L"it" },
        };

        Lang g_current = Lang::En;

        std::wstring SettingsPath()
        {
            wchar_t* base = nullptr;
            std::filesystem::path dir;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)))
            {
                dir = std::filesystem::path{ base } / L"DiscordWin3";
            }
            CoTaskMemFree(base);
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            return (dir / L"settings.ini").wstring();
        }

        Lang FromCode(std::wstring_view code)
        {
            for (int i = 0; i < LangCount; ++i)
            {
                if (_wcsicmp(std::wstring{ code }.c_str(), g_langs[i].code) == 0) return static_cast<Lang>(i);
            }
            // Fall back on the language part only ("fr-CA" -> fr, "pt-AO" -> pt-PT, "pt" -> pt-BR...)
            std::wstring lower{ code };
            for (auto& c : lower) c = towlower(c);
            if (lower == L"pt-br") return Lang::PtBR;
            if (lower.starts_with(L"pt")) return lower == L"pt" ? Lang::PtBR : Lang::PtPT;
            if (lower.starts_with(L"fr")) return Lang::Fr;
            if (lower.starts_with(L"es")) return Lang::Es;
            if (lower.starts_with(L"de")) return Lang::De;
            if (lower.starts_with(L"it")) return Lang::It;
            return Lang::En;
        }
    }

    void Initialize()
    {
        wchar_t saved[32]{};
        GetPrivateProfileStringW(L"ui", L"language", L"", saved, 32, SettingsPath().c_str());
        if (saved[0])
        {
            g_current = FromCode(saved);
            return;
        }
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
        if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0)
        {
            g_current = FromCode(locale);
        }
    }

    void SetLanguage(Lang lang)
    {
        g_current = lang;
        WritePrivateProfileStringW(L"ui", L"language", g_langs[static_cast<int>(lang)].code, SettingsPath().c_str());
    }

    Lang Current() { return g_current; }
    wchar_t const* NativeName(Lang lang) { return g_langs[static_cast<int>(lang)].native; }
    wchar_t const* LocaleName(Lang lang) { return g_langs[static_cast<int>(lang)].locale; }
    wchar_t const* DiscordLocale(Lang lang) { return g_langs[static_cast<int>(lang)].discord; }

    wchar_t const* Tr(S key)
    {
        return g_table[static_cast<int>(key)][static_cast<int>(g_current)];
    }

    std::wstring Fmt(S key, std::wstring_view a0, std::wstring_view a1)
    {
        std::wstring out{ Tr(key) };
        auto replace = [&](std::wstring_view token, std::wstring_view value)
        {
            if (auto pos = out.find(token); pos != std::wstring::npos) out.replace(pos, token.size(), value);
        };
        replace(L"{0}", a0);
        replace(L"{1}", a1);
        return out;
    }
}
