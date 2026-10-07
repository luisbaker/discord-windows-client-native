#pragma once

// Token persisted in %LOCALAPPDATA%\DiscordWin3\token.bin, encrypted with DPAPI
// (only the current Windows user can decrypt it).
namespace DiscordWin3::TokenStore
{
    std::wstring Load();
    void Save(std::wstring const& token);
    void Clear();
}
