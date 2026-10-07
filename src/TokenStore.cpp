#include "pch.h"
#include "TokenStore.h"

#include <dpapi.h>
#include <filesystem>
#include <fstream>
#include <shlobj.h>

namespace
{
    std::filesystem::path TokenPath()
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
        return dir / L"token.bin";
    }
}

namespace DiscordWin3::TokenStore
{
    std::wstring Load()
    {
        std::ifstream file{ TokenPath(), std::ios::binary };
        if (!file)
        {
            return {};
        }
        std::vector<char> blob{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        if (blob.empty())
        {
            return {};
        }

        DATA_BLOB in{ static_cast<DWORD>(blob.size()), reinterpret_cast<BYTE*>(blob.data()) };
        DATA_BLOB out{};
        if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        {
            return {};
        }
        std::wstring token(reinterpret_cast<wchar_t*>(out.pbData), out.cbData / sizeof(wchar_t));
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
        return token;
    }

    void Save(std::wstring const& token)
    {
        DATA_BLOB in{ static_cast<DWORD>(token.size() * sizeof(wchar_t)),
                      reinterpret_cast<BYTE*>(const_cast<wchar_t*>(token.data())) };
        DATA_BLOB out{};
        if (!CryptProtectData(&in, L"DiscordWin3", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        {
            return;
        }
        std::ofstream file{ TokenPath(), std::ios::binary | std::ios::trunc };
        file.write(reinterpret_cast<char const*>(out.pbData), out.cbData);
        LocalFree(out.pbData);
    }

    void Clear()
    {
        std::error_code ec;
        std::filesystem::remove(TokenPath(), ec);
    }
}
