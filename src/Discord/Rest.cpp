#include "pch.h"
#include "Rest.h"
#include "Json.h"

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Web::Http;

namespace DiscordWin3::Discord
{
    std::wstring UserAgent()
    {
        return L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
               L"Chrome/140.0.0.0 Safari/537.36";
    }

    JsonObject ClientProperties()
    {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
        GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);

        JsonObject p;
        p.Insert(L"os", JsonValue::CreateStringValue(L"Windows"));
        p.Insert(L"browser", JsonValue::CreateStringValue(L"Chrome"));
        p.Insert(L"device", JsonValue::CreateStringValue(L""));
        p.Insert(L"system_locale", JsonValue::CreateStringValue(locale));
        p.Insert(L"browser_user_agent", JsonValue::CreateStringValue(UserAgent()));
        p.Insert(L"browser_version", JsonValue::CreateStringValue(L"140.0.0.0"));
        p.Insert(L"os_version", JsonValue::CreateStringValue(L"10"));
        p.Insert(L"referrer", JsonValue::CreateStringValue(L""));
        p.Insert(L"referring_domain", JsonValue::CreateStringValue(L""));
        p.Insert(L"release_channel", JsonValue::CreateStringValue(L"stable"));
        return p;
    }

    std::wstring SuperPropertiesBase64()
    {
        using namespace Windows::Security::Cryptography;
        auto json = ClientProperties().Stringify();
        auto buffer = CryptographicBuffer::ConvertStringToBinary(json, BinaryStringEncoding::Utf8);
        return std::wstring{ CryptographicBuffer::EncodeToBase64String(buffer) };
    }

    Rest::Rest(std::wstring token) : m_token(std::move(token))
    {
        Filters::HttpBaseProtocolFilter filter;
        filter.AutomaticDecompression(true);
        filter.CacheControl().ReadBehavior(Filters::HttpCacheReadBehavior::NoCache);
        filter.CacheControl().WriteBehavior(Filters::HttpCacheWriteBehavior::NoCache);
        m_client = HttpClient{ filter };

        auto headers = m_client.DefaultRequestHeaders();
        headers.UserAgent().TryParseAdd(UserAgent());
        headers.TryAppendWithoutValidation(L"X-Super-Properties", SuperPropertiesBase64());
        headers.TryAppendWithoutValidation(L"X-Discord-Locale", L"fr");
        if (!m_token.empty())
        {
            headers.TryAppendWithoutValidation(L"Authorization", m_token);
        }
    }

    IAsyncOperation<IJsonValue> Rest::GetJson(std::wstring path)
    {
        return Send(HttpMethod::Get(), std::move(path), nullptr);
    }

    IAsyncOperation<IJsonValue> Rest::PostJson(std::wstring path, JsonObject body)
    {
        auto text = body.Stringify();
        return Send(HttpMethod::Post(), std::move(path), [text]() -> IHttpContent
        {
            return HttpStringContent{ text, Windows::Storage::Streams::UnicodeEncoding::Utf8, L"application/json" };
        });
    }

    IAsyncOperation<IJsonValue> Rest::PostContent(std::wstring path, IHttpContent content)
    {
        // Single attempt: multipart content streams cannot be replayed.
        bool used = false;
        return Send(HttpMethod::Post(), std::move(path), [content, used]() mutable -> IHttpContent
        {
            if (used) throw hresult_error(E_FAIL, L"HTTP 429: rate limited");
            used = true;
            return content;
        });
    }

    IAsyncOperation<IJsonValue> Rest::Send(HttpMethod method, std::wstring path, std::function<IHttpContent()> content)
    {
        Uri uri{ std::wstring{ ApiBase } + path };

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            HttpRequestMessage request{ method, uri };
            if (content)
            {
                request.Content(content());
            }

            auto response = co_await m_client.SendRequestAsync(request);
            auto text = co_await response.Content().ReadAsStringAsync();
            auto status = static_cast<uint32_t>(response.StatusCode());

            if (status == 429)
            {
                // Respect the rate limit once or twice, then give up.
                double retryAfter = 1.0;
                JsonObject o;
                if (JsonObject::TryParse(text, o))
                {
                    retryAfter = Json::Num(o, L"retry_after", 1.0);
                }
                co_await resume_after(std::chrono::milliseconds(static_cast<int64_t>(retryAfter * 1000) + 50));
                continue;
            }

            if (status < 200 || status >= 300)
            {
                throw hresult_error(E_FAIL, hstring{ L"HTTP " + std::to_wstring(status) + L": " + std::wstring{ text } });
            }

            if (text.empty() || status == 204)
            {
                co_return JsonValue::CreateNullValue();
            }
            co_return JsonValue::Parse(text);
        }
        throw hresult_error(E_FAIL, L"HTTP 429: rate limited");
    }
}

namespace DiscordWin3::Discord
{
    IAsyncOperation<IJsonValue> Rest::Call(HttpMethod method, std::wstring path, JsonObject body)
    {
        if (!body)
        {
            return Send(method, std::move(path), nullptr);
        }
        auto text = body.Stringify();
        return Send(method, std::move(path), [text]() -> IHttpContent
        {
            return HttpStringContent{ text, Windows::Storage::Streams::UnicodeEncoding::Utf8, L"application/json" };
        });
    }
}
