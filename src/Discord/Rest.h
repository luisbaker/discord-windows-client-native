#pragma once

namespace DiscordWin3::Discord
{
    inline constexpr wchar_t ApiBase[] = L"https://discord.com/api/v9";
    inline constexpr wchar_t CdnBase[] = L"https://cdn.discordapp.com";

    std::wstring UserAgent();
    std::wstring SuperPropertiesBase64();
    winrt::Windows::Data::Json::JsonObject ClientProperties();

    // Thin wrapper over Windows.Web.Http (WinINet stack shared with the OS: no bundled TLS/HTTP).
    class Rest
    {
    public:
        explicit Rest(std::wstring token);

        // Throw winrt::hresult_error; status is encoded in the message as "HTTP <code>".
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Data::Json::IJsonValue> GetJson(std::wstring path);
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Data::Json::IJsonValue> PostJson(
            std::wstring path, winrt::Windows::Data::Json::JsonObject body);

    private:
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Data::Json::IJsonValue> Send(
            winrt::Windows::Web::Http::HttpMethod method, std::wstring path, winrt::hstring body);

        std::wstring m_token;
        winrt::Windows::Web::Http::HttpClient m_client{ nullptr };
    };
}
