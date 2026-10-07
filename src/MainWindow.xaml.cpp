#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "Discord/Json.h"
#include "ImageCache.h"
#include "TokenStore.h"
#include "third_party/qrcodegen.hpp"

#include <algorithm>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
namespace Json = ::DiscordWin3::Json;
namespace Discord = ::DiscordWin3::Discord;
using ::DiscordWin3::MessageData;

namespace winrt::DiscordWin3::implementation
{
    namespace
    {
        constexpr wchar_t HomeId[] = L"@me";
        constexpr uint32_t MaxMessages = 400;
        constexpr int64_t GroupWindowMs = 7 * 60 * 1000;

        bool IsTextLike(int type)
        {
            return type == 0 || type == 1 || type == 3 || type == 5;
        }

        std::wstring Glyph(int type)
        {
            switch (type)
            {
            case 1: case 3: return L"@";
            case 2: return L"\U0001F50A";
            case 5: return L"\U0001F4E3";
            case 13: return L"\U0001F399";
            case 15: return L"\U0001F4AC";
            default: return L"#";
            }
        }

        std::wstring UserDisplayName(JsonObject const& user)
        {
            auto global = Json::Str(user, L"global_name");
            return global.empty() ? Json::Str(user, L"username") : global;
        }

        std::wstring AvatarUrl(JsonObject const& user)
        {
            auto id = Json::Str(user, L"id");
            auto hash = Json::Str(user, L"avatar");
            if (!hash.empty())
            {
                return std::wstring{ Discord::CdnBase } + L"/avatars/" + id + L"/" + hash + L".png?size=64";
            }
            auto discriminator = Json::Str(user, L"discriminator");
            uint64_t index = (discriminator.empty() || discriminator == L"0")
                ? (Json::U64(id) >> 22) % 6
                : Json::U64(discriminator) % 5;
            return std::wstring{ Discord::CdnBase } + L"/embed/avatars/" + std::to_wstring(index) + L".png";
        }

        std::wstring FormatTime(int64_t unixMs, bool dateOnly = false)
        {
            // unix ms -> FILETIME (100ns since 1601) -> local SYSTEMTIME
            ULARGE_INTEGER t;
            t.QuadPart = static_cast<ULONGLONG>(unixMs) * 10000ULL + 116444736000000000ULL;
            FILETIME ft{ t.LowPart, t.HighPart };
            SYSTEMTIME utc, local, now;
            FileTimeToSystemTime(&ft, &utc);
            SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
            GetLocalTime(&now);

            wchar_t buf[64];
            if (dateOnly)
            {
                swprintf_s(buf, L"%02d/%02d/%04d", local.wDay, local.wMonth, local.wYear);
            }
            else if (local.wYear == now.wYear && local.wMonth == now.wMonth && local.wDay == now.wDay)
            {
                swprintf_s(buf, L"Aujourd'hui à %02d:%02d", local.wHour, local.wMinute);
            }
            else
            {
                swprintf_s(buf, L"%02d/%02d/%04d %02d:%02d", local.wDay, local.wMonth, local.wYear, local.wHour, local.wMinute);
            }
            return buf;
        }

        std::wstring Upper(std::wstring s)
        {
            if (!s.empty())
            {
                LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, s.data(), static_cast<int>(s.size()),
                              s.data(), static_cast<int>(s.size()), nullptr, nullptr, 0);
            }
            return s;
        }

        std::wstring NowNonce()
        {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            return std::to_wstring(static_cast<uint64_t>(ms - 1420070400000LL) << 22);
        }

        implementation::MessageItem* Impl(IInspectable const& item)
        {
            return get_self<implementation::MessageItem>(item.as<DiscordWin3::MessageItem>());
        }

        template <typename T>
        T FindDescendant(DependencyObject const& root)
        {
            int count = Media::VisualTreeHelper::GetChildrenCount(root);
            for (int i = 0; i < count; ++i)
            {
                auto child = Media::VisualTreeHelper::GetChild(root, i);
                if (auto match = child.try_as<T>())
                {
                    return match;
                }
                if (auto nested = FindDescendant<T>(child))
                {
                    return nested;
                }
            }
            return nullptr;
        }
    }

    // ------------------------------------------------------------------ setup

    void MainWindow::InitializeComponent()
    {
        MainWindowT::InitializeComponent();

        m_dispatcher = DispatcherQueue();
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppWindow().Resize({ 1280, 800 });

        GuildList().ItemsSource(m_guildItems);
        ChannelList().ItemsSource(m_channelItems);
        MessageList().ItemsSource(m_messageItems);

        // Give memory back to the OS while the window is minimized.
        AppWindow().Changed([weak = get_weak()](Microsoft::UI::Windowing::AppWindow const& window,
                                                Microsoft::UI::Windowing::AppWindowChangedEventArgs const&)
        {
            auto self = weak.get();
            auto presenter = window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
            if (self && presenter && presenter.State() == Microsoft::UI::Windowing::OverlappedPresenterState::Minimized)
            {
                self->TrimMemory();
            }
        });

        Closed([weak = get_weak()](IInspectable const&, WindowEventArgs const&)
        {
            if (auto self = weak.get())
            {
                if (self->m_gateway) self->m_gateway->Stop();
                if (self->m_remoteAuth) self->m_remoteAuth->Stop();
            }
        });

        auto token = ::DiscordWin3::TokenStore::Load();
        if (token.empty())
        {
            ShowLogin({});
        }
        else
        {
            StartSession(token);
        }
    }

    void MainWindow::TrimMemory()
    {
        ::DiscordWin3::ImageCache::Clear();
        SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0);
    }

    // ------------------------------------------------------------------ login

    void MainWindow::ShowLogin(std::wstring const& error)
    {
        ChatRoot().Visibility(Visibility::Collapsed);
        LoginRoot().Visibility(Visibility::Visible);
        LoginError().Text(error);
        TokenLoginButton().IsEnabled(true);
        StatusText().Text(L"");
        StartQr();
    }

    void MainWindow::StartQr()
    {
        if (m_remoteAuth)
        {
            m_remoteAuth->Stop();
        }
        QrImage().Source(nullptr);
        QrProgress().IsActive(true);
        QrRetry().Visibility(Visibility::Collapsed);
        QrTitle().Text(L"Se connecter avec un code QR");
        QrHint().Text(L"Scanne ce code avec l'application mobile Discord pour te connecter instantanément.");

        auto weak = get_weak();
        auto dq = m_dispatcher;
        auto post = [weak, dq](auto fn)
        {
            dq.TryEnqueue([weak, fn]()
            {
                if (auto self = weak.get())
                {
                    fn(self.get());
                }
            });
        };

        Discord::RemoteAuth::Callbacks cb;
        cb.onQrCode = [post](std::wstring const& url) { post([url](MainWindow* w) { w->RenderQr(url); }); };
        cb.onScanned = [post](std::wstring const& user)
        {
            post([user](MainWindow* w)
            {
                w->QrTitle().Text(L"Regarde ton téléphone !");
                w->QrHint().Text(L"Connexion en tant que " + user + L" — confirme sur l'appli mobile.");
            });
        };
        cb.onToken = [post](std::wstring const& token)
        {
            post([token](MainWindow* w)
            {
                ::DiscordWin3::TokenStore::Save(token);
                w->StartSession(token);
            });
        };
        cb.onError = [post](std::wstring const& message)
        {
            post([message](MainWindow* w)
            {
                if (w->LoginRoot().Visibility() != Visibility::Visible)
                {
                    return;
                }
                w->QrProgress().IsActive(false);
                w->QrImage().Source(nullptr);
                w->QrTitle().Text(L"Code expiré");
                w->QrHint().Text(message);
                w->QrRetry().Visibility(Visibility::Visible);
            });
        };

        m_remoteAuth = std::make_shared<Discord::RemoteAuth>(std::move(cb));
        m_remoteAuth->Start();
    }

    fire_and_forget MainWindow::RenderQr(std::wstring url)
    {
        auto strong = get_strong();
        auto qr = qrcodegen::QrCode::encodeText(winrt::to_string(url).c_str(), qrcodegen::QrCode::Ecc::MEDIUM);

        constexpr int scale = 8;
        constexpr int border = 2;
        int modules = qr.getSize() + border * 2;
        int px = modules * scale;
        std::vector<uint8_t> pixels(static_cast<size_t>(px) * px * 4, 255);
        for (int y = 0; y < qr.getSize(); ++y)
        {
            for (int x = 0; x < qr.getSize(); ++x)
            {
                if (!qr.getModule(x, y))
                {
                    continue;
                }
                for (int dy = 0; dy < scale; ++dy)
                {
                    uint8_t* row = pixels.data() + ((static_cast<size_t>(y + border) * scale + dy) * px + static_cast<size_t>(x + border) * scale) * 4;
                    for (int dx = 0; dx < scale; ++dx)
                    {
                        row[dx * 4 + 0] = row[dx * 4 + 1] = row[dx * 4 + 2] = 0;
                    }
                }
            }
        }

        using namespace Windows::Graphics::Imaging;
        SoftwareBitmap bitmap{ BitmapPixelFormat::Bgra8, px, px, BitmapAlphaMode::Premultiplied };
        bitmap.CopyFromBuffer(Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(pixels));
        Media::Imaging::SoftwareBitmapSource source;
        co_await source.SetBitmapAsync(bitmap);
        QrProgress().IsActive(false);
        QrImage().Source(source);
    }

    void MainWindow::OnQrRetry(IInspectable const&, RoutedEventArgs const&)
    {
        StartQr();
    }

    void MainWindow::OnTokenLogin(IInspectable const&, RoutedEventArgs const&)
    {
        std::wstring token{ TokenBox().Password() };
        token.erase(std::remove_if(token.begin(), token.end(), [](wchar_t c) { return c == L' ' || c == L'"' || c == L'\r' || c == L'\n'; }), token.end());
        if (token.empty())
        {
            LoginError().Text(L"Entre un token.");
            return;
        }
        TokenBox().Password(L"");
        m_saveTokenOnReady = true;
        StartSession(token);
    }

    void MainWindow::OnLogout(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_rest)
        {
            // Best effort server-side invalidation of the session token.
            JsonObject body;
            body.Insert(L"provider", JsonValue::CreateNullValue());
            body.Insert(L"voip_provider", JsonValue::CreateNullValue());
            m_rest->PostJson(L"/auth/logout", body);
        }
        ::DiscordWin3::TokenStore::Clear();
        EndSession();
        ShowLogin({});
    }

    // ------------------------------------------------------------------ session

    void MainWindow::StartSession(std::wstring token)
    {
        if (m_remoteAuth)
        {
            m_remoteAuth->Stop();
            m_remoteAuth.reset();
        }
        EndSession();

        m_token = std::move(token);
        m_rest = std::make_shared<Discord::Rest>(m_token);
        LoginRoot().Visibility(Visibility::Collapsed);
        ChatRoot().Visibility(Visibility::Visible);
        GuildTitle().Text(L"Chargement…");

        auto weak = get_weak();
        auto dq = m_dispatcher;
        m_gateway = std::make_shared<Discord::Gateway>(
            m_token,
            [weak, dq](std::wstring const& type, JsonObject const& d)
            {
                dq.TryEnqueue([weak, type, d]()
                {
                    if (auto self = weak.get())
                    {
                        self->OnDispatch(type, d);
                    }
                });
            },
            [weak, dq](Discord::GatewayStatus status)
            {
                dq.TryEnqueue([weak, status]()
                {
                    if (auto self = weak.get())
                    {
                        self->OnStatus(status);
                    }
                });
            });
        m_gateway->Start();
    }

    void MainWindow::EndSession()
    {
        if (m_gateway)
        {
            m_gateway->Stop();
            m_gateway.reset();
        }
        m_rest.reset();
        m_token.clear();
        m_guilds.clear();
        m_dms.clear();
        m_userNames.clear();
        m_channelNames.clear();
        m_roleNames.clear();
        m_currentGuildId.clear();
        m_currentChannelId.clear();
        ++m_channelGeneration;
        m_guildItems.Clear();
        m_channelItems.Clear();
        m_messageItems.Clear();
        ChannelTitle().Text(L"");
        Composer().IsEnabled(false);
        ::DiscordWin3::ImageCache::Clear();
    }

    void MainWindow::OnStatus(Discord::GatewayStatus status)
    {
        switch (status)
        {
        case Discord::GatewayStatus::Connecting: StatusText().Text(L"Connexion…"); break;
        case Discord::GatewayStatus::Reconnecting: StatusText().Text(L"Reconnexion…"); break;
        case Discord::GatewayStatus::Connected: StatusText().Text(L""); break;
        case Discord::GatewayStatus::AuthFailed:
            m_saveTokenOnReady = false;
            ::DiscordWin3::TokenStore::Clear();
            EndSession();
            ShowLogin(L"Token invalide ou expiré. Reconnecte-toi.");
            break;
        }
    }

    void MainWindow::OnDispatch(std::wstring const& type, JsonObject const& d)
    {
        if (type == L"READY")
        {
            HandleReady(d);
        }
        else if (type == L"MESSAGE_CREATE")
        {
            auto channelId = Json::Str(d, L"channel_id");
            for (auto& dm : m_dms)
            {
                if (dm.id == channelId)
                {
                    dm.lastMessageId = Json::Str(d, L"id");
                }
            }
            if (channelId == m_currentChannelId)
            {
                AppendMessage(BuildMessage(d));
            }
        }
        else if (type == L"MESSAGE_UPDATE")
        {
            if (Json::Str(d, L"channel_id") != m_currentChannelId || !Json::Obj(d, L"author"))
            {
                return;
            }
            int index = FindMessage(Json::Str(d, L"id"));
            if (index >= 0)
            {
                bool header = Impl(m_messageItems.GetAt(index))->ShowsHeader();
                m_messageItems.SetAt(index, make<MessageItem>(BuildMessage(d), header));
            }
        }
        else if (type == L"MESSAGE_DELETE")
        {
            if (Json::Str(d, L"channel_id") != m_currentChannelId)
            {
                return;
            }
            int index = FindMessage(Json::Str(d, L"id"));
            if (index >= 0)
            {
                m_messageItems.RemoveAt(index);
                FixHeaderAt(index);
            }
        }
        else if (type == L"GUILD_CREATE")
        {
            UpsertGuild(ParseGuild(d));
            RefreshGuildRail();
        }
        else if (type == L"GUILD_DELETE")
        {
            if (Json::Bool(d, L"unavailable"))
            {
                return;
            }
            auto id = Json::Str(d, L"id");
            std::erase_if(m_guilds, [&](GuildInfo const& g) { return g.id == id; });
            RefreshGuildRail();
        }
        else if (type == L"CHANNEL_CREATE" || type == L"CHANNEL_UPDATE" || type == L"CHANNEL_DELETE")
        {
            auto channel = ParseChannel(d);
            auto guildId = Json::Str(d, L"guild_id");
            std::vector<ChannelInfo>* list = nullptr;
            if (guildId.empty())
            {
                channel.name = DmName(d);
                list = &m_dms;
            }
            else if (auto guild = FindGuild(guildId))
            {
                list = &guild->channels;
            }
            if (!list)
            {
                return;
            }
            std::erase_if(*list, [&](ChannelInfo const& c) { return c.id == channel.id; });
            if (type != L"CHANNEL_DELETE")
            {
                m_channelNames[channel.id] = channel.name;
                list->push_back(std::move(channel));
            }
            if ((guildId.empty() ? std::wstring{ HomeId } : guildId) == m_currentGuildId)
            {
                RefreshChannelList();
            }
        }
    }

    // ------------------------------------------------------------------ model

    ChannelInfo MainWindow::ParseChannel(JsonObject const& c)
    {
        ChannelInfo info;
        info.id = Json::Str(c, L"id");
        info.name = Json::Str(c, L"name");
        info.parentId = Json::Str(c, L"parent_id");
        info.type = static_cast<int>(Json::Num(c, L"type"));
        info.position = static_cast<int>(Json::Num(c, L"position"));
        info.lastMessageId = Json::Str(c, L"last_message_id");
        info.overwrites = Discord::ParseOverwrites(Json::Arr(c, L"permission_overwrites"));
        return info;
    }

    std::wstring MainWindow::DmName(JsonObject const& c)
    {
        auto name = Json::Str(c, L"name");
        if (!name.empty())
        {
            return name;
        }
        std::wstring joined;
        auto append = [&](std::wstring const& n)
        {
            if (!n.empty())
            {
                joined += (joined.empty() ? L"" : L", ") + n;
            }
        };
        if (auto recipients = Json::Arr(c, L"recipients"))
        {
            for (auto const& r : recipients)
            {
                if (r.ValueType() == JsonValueType::Object)
                {
                    auto user = r.GetObject();
                    auto display = UserDisplayName(user);
                    m_userNames[Json::Str(user, L"id")] = display;
                    append(display);
                }
            }
        }
        else if (auto ids = Json::Arr(c, L"recipient_ids"))
        {
            for (auto const& r : ids)
            {
                if (r.ValueType() == JsonValueType::String)
                {
                    auto it = m_userNames.find(std::wstring{ r.GetString() });
                    append(it != m_userNames.end() ? it->second : L"?");
                }
            }
        }
        return joined.empty() ? L"Groupe sans nom" : joined;
    }

    GuildInfo MainWindow::ParseGuild(JsonObject const& g)
    {
        // Newer READY payloads nest name/icon/owner under "properties".
        auto props = Json::Obj(g, L"properties");
        auto const& meta = props ? props : g;

        GuildInfo guild;
        guild.id = Json::Str(g, L"id");
        guild.name = Json::Str(meta, L"name");
        guild.icon = Json::Str(meta, L"icon");

        guild.perms.guildId = Json::U64(guild.id);
        guild.perms.selfId = Json::U64(m_selfId);
        guild.perms.ownerId = Json::U64(meta, L"owner_id");

        if (auto roles = Json::Arr(g, L"roles"))
        {
            for (auto const& r : roles)
            {
                if (r.ValueType() != JsonValueType::Object) continue;
                auto role = r.GetObject();
                auto roleId = Json::Str(role, L"id");
                guild.perms.rolePermissions[Json::U64(roleId)] = Json::U64(role, L"permissions");
                m_roleNames[roleId] = Json::Str(role, L"name");
            }
        }

        if (auto members = Json::Arr(g, L"members"))
        {
            for (auto const& m : members)
            {
                if (m.ValueType() != JsonValueType::Object) continue;
                auto member = m.GetObject();
                auto user = Json::Obj(member, L"user");
                auto userId = user ? Json::Str(user, L"id") : Json::Str(member, L"user_id");
                if (userId == m_selfId)
                {
                    guild.perms.known = true;
                    if (auto roles = Json::Arr(member, L"roles"))
                    {
                        for (auto const& id : roles)
                        {
                            if (id.ValueType() == JsonValueType::String)
                                guild.perms.selfRoles.push_back(Json::U64(id.GetString()));
                        }
                    }
                }
            }
        }

        if (auto channels = Json::Arr(g, L"channels"))
        {
            guild.channels.reserve(channels.Size());
            for (auto const& c : channels)
            {
                if (c.ValueType() != JsonValueType::Object) continue;
                auto info = ParseChannel(c.GetObject());
                m_channelNames[info.id] = info.name;
                guild.channels.push_back(std::move(info));
            }
        }
        return guild;
    }

    void MainWindow::HandleReady(JsonObject const& d)
    {
        if (m_saveTokenOnReady)
        {
            ::DiscordWin3::TokenStore::Save(m_token);
            m_saveTokenOnReady = false;
        }

        auto user = Json::Obj(d, L"user");
        m_selfId = Json::Str(user, L"id");
        SelfName().Text(UserDisplayName(user));

        if (auto users = Json::Arr(d, L"users"))
        {
            for (auto const& u : users)
            {
                if (u.ValueType() == JsonValueType::Object)
                {
                    auto o = u.GetObject();
                    m_userNames[Json::Str(o, L"id")] = UserDisplayName(o);
                }
            }
        }

        m_guilds.clear();
        auto mergedMembers = Json::Arr(d, L"merged_members");
        if (auto guilds = Json::Arr(d, L"guilds"))
        {
            for (uint32_t i = 0; i < guilds.Size(); ++i)
            {
                auto value = guilds.GetAt(i);
                if (value.ValueType() != JsonValueType::Object) continue;
                auto guild = ParseGuild(value.GetObject());

                // merged_members[i] holds our own member object for guilds[i].
                if (!guild.perms.known && mergedMembers && i < mergedMembers.Size()
                    && mergedMembers.GetAt(i).ValueType() == JsonValueType::Array)
                {
                    for (auto const& m : mergedMembers.GetAt(i).GetArray())
                    {
                        if (m.ValueType() != JsonValueType::Object) continue;
                        auto member = m.GetObject();
                        if (Json::Str(member, L"user_id") != m_selfId) continue;
                        guild.perms.known = true;
                        if (auto roles = Json::Arr(member, L"roles"))
                        {
                            for (auto const& id : roles)
                            {
                                if (id.ValueType() == JsonValueType::String)
                                    guild.perms.selfRoles.push_back(Json::U64(id.GetString()));
                            }
                        }
                    }
                }
                if (!guild.name.empty())
                {
                    m_guilds.push_back(std::move(guild));
                }
            }
        }

        // Respect the user's folder order when available.
        if (auto settings = Json::Obj(d, L"user_settings"))
        {
            std::vector<std::wstring> order;
            if (auto folders = Json::Arr(settings, L"guild_folders"))
            {
                for (auto const& f : folders)
                {
                    if (f.ValueType() != JsonValueType::Object) continue;
                    if (auto ids = Json::Arr(f.GetObject(), L"guild_ids"))
                    {
                        for (auto const& id : ids)
                        {
                            if (id.ValueType() == JsonValueType::String) order.emplace_back(id.GetString());
                            else if (id.ValueType() == JsonValueType::Number) order.push_back(std::to_wstring(static_cast<uint64_t>(id.GetNumber())));
                        }
                    }
                }
            }
            if (!order.empty())
            {
                auto rank = [&](std::wstring const& id)
                {
                    auto it = std::find(order.begin(), order.end(), id);
                    return it == order.end() ? order.size() : static_cast<size_t>(it - order.begin());
                };
                std::stable_sort(m_guilds.begin(), m_guilds.end(), [&](GuildInfo const& a, GuildInfo const& b) { return rank(a.id) < rank(b.id); });
            }
        }

        m_dms.clear();
        if (auto privateChannels = Json::Arr(d, L"private_channels"))
        {
            for (auto const& c : privateChannels)
            {
                if (c.ValueType() != JsonValueType::Object) continue;
                auto o = c.GetObject();
                auto info = ParseChannel(o);
                info.name = DmName(o);
                m_channelNames[info.id] = info.name;
                m_dms.push_back(std::move(info));
            }
        }

        RefreshGuildRail();
        if (GuildList().SelectedIndex() < 0 && m_guildItems.Size() > 0)
        {
            GuildList().SelectedIndex(0);
        }
    }

    GuildInfo* MainWindow::FindGuild(std::wstring const& id)
    {
        for (auto& g : m_guilds)
        {
            if (g.id == id) return &g;
        }
        return nullptr;
    }

    void MainWindow::UpsertGuild(GuildInfo guild)
    {
        if (guild.name.empty())
        {
            return;
        }
        bool isCurrent = m_currentGuildId == guild.id;
        if (auto existing = FindGuild(guild.id))
        {
            *existing = std::move(guild);
        }
        else
        {
            m_guilds.push_back(std::move(guild));
        }
        if (isCurrent)
        {
            RefreshChannelList();
        }
    }

    void MainWindow::RefreshGuildRail()
    {
        auto selected = m_currentGuildId;
        std::vector<IInspectable> items;
        items.reserve(m_guilds.size() + 1);
        items.push_back(make<GuildItem>(HomeId, L"Messages privés", L""));
        for (auto const& g : m_guilds)
        {
            std::wstring icon = g.icon.empty() ? L""
                : std::wstring{ Discord::CdnBase } + L"/icons/" + g.id + L"/" + g.icon + L".png?size=96";
            items.push_back(make<GuildItem>(hstring{ g.id }, hstring{ g.name }, hstring{ icon }));
        }
        m_guildItems.ReplaceAll(items);

        for (uint32_t i = 0; i < m_guildItems.Size(); ++i)
        {
            if (m_guildItems.GetAt(i).as<DiscordWin3::GuildItem>().Id() == selected)
            {
                GuildList().SelectedIndex(static_cast<int>(i));
                break;
            }
        }
    }

    void MainWindow::RefreshChannelList()
    {
        std::vector<IInspectable> items;

        if (m_currentGuildId == HomeId)
        {
            GuildTitle().Text(L"Messages privés");
            auto dms = m_dms;
            std::sort(dms.begin(), dms.end(), [](ChannelInfo const& a, ChannelInfo const& b)
            {
                return Json::SnowflakeLess(b.lastMessageId, a.lastMessageId);
            });
            for (auto const& c : dms)
            {
                items.push_back(make<ChannelItem>(hstring{ c.id }, hstring{ c.name }, hstring{ Glyph(c.type) }, false, true));
            }
        }
        else if (auto guild = FindGuild(m_currentGuildId))
        {
            GuildTitle().Text(guild->name);

            auto byPosition = [](ChannelInfo const* a, ChannelInfo const* b)
            {
                bool av = !IsTextLike(a->type), bv = !IsTextLike(b->type);
                if (av != bv) return bv;                // text before voice
                if (a->position != b->position) return a->position < b->position;
                return Json::SnowflakeLess(a->id, b->id);
            };

            std::vector<ChannelInfo const*> categories;
            std::unordered_map<std::wstring, std::vector<ChannelInfo const*>> children;
            for (auto const& c : guild->channels)
            {
                if (c.type == 4)
                {
                    categories.push_back(&c);
                }
                else if ((IsTextLike(c.type) || c.type == 2 || c.type == 13 || c.type == 15) && guild->perms.CanView(c.overwrites))
                {
                    children[c.parentId].push_back(&c);
                }
            }
            std::sort(categories.begin(), categories.end(), [](auto a, auto b) { return a->position < b->position; });

            auto emit = [&](std::vector<ChannelInfo const*>& list)
            {
                std::sort(list.begin(), list.end(), byPosition);
                for (auto c : list)
                {
                    items.push_back(make<ChannelItem>(hstring{ c->id }, hstring{ c->name }, hstring{ Glyph(c->type) }, false, IsTextLike(c->type)));
                }
            };

            emit(children[L""]);
            for (auto category : categories)
            {
                auto& list = children[category->id];
                if (list.empty()) continue;
                items.push_back(make<ChannelItem>(hstring{ category->id }, hstring{ Upper(category->name) }, L"", true, false));
                emit(list);
            }
        }

        m_channelItems.ReplaceAll(items);
        for (uint32_t i = 0; i < m_channelItems.Size(); ++i)
        {
            if (m_channelItems.GetAt(i).as<DiscordWin3::ChannelItem>().Id() == m_currentChannelId)
            {
                ChannelList().SelectedIndex(static_cast<int>(i));
                break;
            }
        }
    }

    void MainWindow::OnGuildSelected(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto item = GuildList().SelectedItem().try_as<DiscordWin3::GuildItem>();
        if (!item || item.Id() == m_currentGuildId)
        {
            return;
        }
        m_currentGuildId = item.Id();
        RefreshChannelList();

        // Auto-open the first text channel, like the official client.
        for (uint32_t i = 0; i < m_channelItems.Size(); ++i)
        {
            auto channel = m_channelItems.GetAt(i).as<DiscordWin3::ChannelItem>();
            if (channel.IsTextLike())
            {
                ChannelList().SelectedIndex(static_cast<int>(i));
                break;
            }
        }
    }

    void MainWindow::OnChannelSelected(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto item = ChannelList().SelectedItem().try_as<DiscordWin3::ChannelItem>();
        if (!item || !item.IsTextLike() || item.Id() == m_currentChannelId)
        {
            return;
        }
        LoadChannel(std::wstring{ item.Id() }, std::wstring{ item.Glyph() } + L" " + std::wstring{ item.Name() });
    }

    // ------------------------------------------------------------------ messages

    fire_and_forget MainWindow::LoadChannel(std::wstring id, std::wstring title)
    {
        auto strong = get_strong();
        auto generation = ++m_channelGeneration;
        auto rest = m_rest;
        m_currentChannelId = id;
        m_messageItems.Clear();
        m_hasMoreOlder = false;
        m_loadingOlder = false;
        ChannelTitle().Text(title);
        Composer().IsEnabled(true);
        Composer().PlaceholderText(L"Envoyer un message dans " + title);
        if (!rest)
        {
            co_return;
        }

        hstring error;
        try
        {
            auto json = co_await rest->GetJson(L"/channels/" + id + L"/messages?limit=50");
            co_await wil::resume_foreground(m_dispatcher);
            if (generation != m_channelGeneration)
            {
                co_return;
            }
            auto array = json.GetArray();
            for (int i = static_cast<int>(array.Size()) - 1; i >= 0; --i)
            {
                AppendMessage(BuildMessage(array.GetAt(i).GetObject()));
            }
            m_hasMoreOlder = array.Size() == 50;
        }
        catch (hresult_error const& e)
        {
            error = e.message();
        }
        if (!error.empty())
        {
            co_await wil::resume_foreground(m_dispatcher);
            if (generation != m_channelGeneration)
            {
                co_return;
            }
            std::wstring message{ error };
            MessageData info;
            info.authorName = L"Discord Win3";
            info.content = message.starts_with(L"HTTP 403") ? L"Tu n'as pas accès à ce salon." : L"Erreur de chargement : " + message;
            AppendMessage(std::move(info));
        }
    }

    fire_and_forget MainWindow::LoadOlder()
    {
        if (m_loadingOlder || !m_hasMoreOlder || m_messageItems.Size() == 0 || !m_rest)
        {
            co_return;
        }
        auto strong = get_strong();
        auto generation = m_channelGeneration;
        auto rest = m_rest;
        m_loadingOlder = true;
        auto before = Impl(m_messageItems.GetAt(0))->Data().id;

        try
        {
            auto json = co_await rest->GetJson(L"/channels/" + m_currentChannelId + L"/messages?limit=50&before=" + before);
            co_await wil::resume_foreground(m_dispatcher);
            if (generation != m_channelGeneration)
            {
                co_return;
            }
            auto array = json.GetArray();
            m_hasMoreOlder = array.Size() == 50;

            // Oldest first, grouped among themselves.
            std::vector<MessageData> batch;
            batch.reserve(array.Size());
            for (int i = static_cast<int>(array.Size()) - 1; i >= 0; --i)
            {
                batch.push_back(BuildMessage(array.GetAt(i).GetObject()));
            }
            for (int i = static_cast<int>(batch.size()) - 1; i >= 0; --i)
            {
                bool header = ShouldShowHeader(i > 0 ? &batch[i - 1] : nullptr, batch[i]);
                m_messageItems.InsertAt(0, make<MessageItem>(batch[i], header));
            }
            FixHeaderAt(static_cast<uint32_t>(batch.size()));
        }
        catch (...)
        {
        }
        m_loadingOlder = false;
    }

    void MainWindow::OnMessageListLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        m_messageScroller = FindDescendant<ScrollViewer>(MessageList());
        if (!m_messageScroller)
        {
            return;
        }
        m_messageScroller.ViewChanged([weak = get_weak()](IInspectable const& sender, ScrollViewerViewChangedEventArgs const&)
        {
            auto self = weak.get();
            if (self && sender.as<ScrollViewer>().VerticalOffset() < 400)
            {
                self->LoadOlder();
            }
        });
    }

    void MainWindow::OnComposerKeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& e)
    {
        if (e.Key() != Windows::System::VirtualKey::Enter)
        {
            return;
        }
        auto shift = Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(Windows::System::VirtualKey::Shift);
        if ((shift & Windows::UI::Core::CoreVirtualKeyStates::Down) == Windows::UI::Core::CoreVirtualKeyStates::Down)
        {
            return; // Shift+Enter = new line
        }
        e.Handled(true);

        std::wstring text{ Composer().Text() };
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
        {
            text.pop_back();
        }
        if (!text.empty())
        {
            Composer().Text(L"");
            SendMessage(std::move(text));
        }
    }

    fire_and_forget MainWindow::SendMessage(std::wstring text)
    {
        auto strong = get_strong();
        auto rest = m_rest;
        auto channelId = m_currentChannelId;
        if (!rest || channelId.empty())
        {
            co_return;
        }

        JsonObject body;
        body.Insert(L"content", JsonValue::CreateStringValue(text));
        body.Insert(L"nonce", JsonValue::CreateStringValue(NowNonce()));
        body.Insert(L"tts", JsonValue::CreateBooleanValue(false));
        body.Insert(L"flags", JsonValue::CreateNumberValue(0));
        hstring error;
        try
        {
            co_await rest->PostJson(L"/channels/" + channelId + L"/messages", body);
        }
        catch (hresult_error const& e)
        {
            error = e.message();
        }
        if (!error.empty())
        {
            auto message = std::wstring{ error };
            co_await wil::resume_foreground(m_dispatcher);
            StatusText().Text(L"Envoi impossible : " + message.substr(0, 80));
            if (Composer().Text().empty() && m_currentChannelId == channelId)
            {
                Composer().Text(text);
            }
        }
    }

    std::wstring MainWindow::FormatContent(std::wstring const& raw, JsonObject const& m)
    {
        std::unordered_map<std::wstring, std::wstring> mentions;
        if (auto list = Json::Arr(m, L"mentions"))
        {
            for (auto const& u : list)
            {
                if (u.ValueType() == JsonValueType::Object)
                {
                    auto o = u.GetObject();
                    mentions[Json::Str(o, L"id")] = UserDisplayName(o);
                }
            }
        }

        std::wstring out;
        out.reserve(raw.size());
        size_t i = 0;
        while (i < raw.size())
        {
            size_t close;
            if (raw[i] != L'<' || (close = raw.find(L'>', i)) == std::wstring::npos || close - i > 80)
            {
                out.push_back(raw[i++]);
                continue;
            }
            std::wstring_view tag{ raw.data() + i + 1, close - i - 1 };
            std::wstring replacement;
            auto lookup = [](auto const& map, std::wstring const& key, std::wstring fallback)
            {
                auto it = map.find(key);
                return it != map.end() ? it->second : fallback;
            };

            if (tag.starts_with(L"@&"))
            {
                replacement = L"@" + lookup(m_roleNames, std::wstring{ tag.substr(2) }, L"rôle");
            }
            else if (tag.starts_with(L"@"))
            {
                auto id = std::wstring{ tag.substr(tag.starts_with(L"@!") ? 2 : 1) };
                auto name = lookup(mentions, id, L"");
                replacement = L"@" + (name.empty() ? lookup(m_userNames, id, L"utilisateur") : name);
            }
            else if (tag.starts_with(L"#"))
            {
                replacement = L"#" + lookup(m_channelNames, std::wstring{ tag.substr(1) }, L"salon-inconnu");
            }
            else if (tag.starts_with(L":") || tag.starts_with(L"a:"))
            {
                auto start = tag.find(L':');
                auto end = tag.find(L':', start + 1);
                replacement = end == std::wstring_view::npos ? std::wstring{ tag } : L":" + std::wstring{ tag.substr(start + 1, end - start - 1) } + L":";
            }
            else if (tag.starts_with(L"t:"))
            {
                auto digits = tag.substr(2, tag.find(L':', 2) == std::wstring_view::npos ? std::wstring_view::npos : tag.find(L':', 2) - 2);
                replacement = FormatTime(static_cast<int64_t>(Json::U64(digits)) * 1000);
            }
            else
            {
                replacement = L"<" + std::wstring{ tag } + L">"; // plain link like <https://...>
                if (tag.starts_with(L"http")) replacement = std::wstring{ tag };
            }
            out += replacement;
            i = close + 1;
        }
        return out;
    }

    MessageData MainWindow::BuildMessage(JsonObject const& m)
    {
        MessageData data;
        data.id = Json::Str(m, L"id");
        data.unixMs = Json::SnowflakeMs(data.id);
        data.timestamp = FormatTime(data.unixMs);

        auto author = Json::Obj(m, L"author");
        data.authorId = Json::Str(author, L"id");
        auto member = Json::Obj(m, L"member");
        auto nick = Json::Str(member, L"nick");
        data.authorName = nick.empty() ? UserDisplayName(author) : nick;
        data.avatarUrl = AvatarUrl(author);

        std::wstring content = FormatContent(Json::Str(m, L"content"), m);

        int type = static_cast<int>(Json::Num(m, L"type"));
        switch (type)
        {
        case 7: content = L"→ a rejoint le serveur."; data.forceHeader = true; break;
        case 6: content = L"📌 a épinglé un message."; data.forceHeader = true; break;
        case 8: case 9: case 10: case 11: content = L"🚀 a boosté le serveur !"; data.forceHeader = true; break;
        case 19:
            if (auto ref = Json::Obj(m, L"referenced_message"))
            {
                auto refAuthor = Json::Obj(ref, L"author");
                auto snippet = Json::Str(ref, L"content");
                if (snippet.size() > 80) snippet = snippet.substr(0, 80) + L"…";
                content = L"↪ @" + UserDisplayName(refAuthor) + L" : " + snippet + L"\n" + content;
            }
            data.forceHeader = true;
            break;
        default: break;
        }

        // Attachments: first image inline (resized by Discord's media proxy), the rest as links.
        auto setImage = [&](std::wstring const& proxyUrl, double w, double h)
        {
            if (proxyUrl.empty() || w <= 0 || h <= 0 || !data.imageUrl.empty()) return false;
            double scale = std::min({ 1.0, 400.0 / w, 300.0 / h });
            data.imageWidth = std::max(1.0, std::floor(w * scale));
            data.imageHeight = std::max(1.0, std::floor(h * scale));
            auto fetchW = static_cast<int>(std::min(w, data.imageWidth * 2));
            auto fetchH = static_cast<int>(std::min(h, data.imageHeight * 2));
            data.imageUrl = proxyUrl + (proxyUrl.find(L'?') == std::wstring::npos ? L"?" : L"&")
                + L"width=" + std::to_wstring(fetchW) + L"&height=" + std::to_wstring(fetchH);
            return true;
        };

        if (auto attachments = Json::Arr(m, L"attachments"))
        {
            for (auto const& a : attachments)
            {
                if (a.ValueType() != JsonValueType::Object) continue;
                auto o = a.GetObject();
                auto contentType = Json::Str(o, L"content_type");
                if (contentType.starts_with(L"image/") &&
                    setImage(Json::Str(o, L"proxy_url"), Json::Num(o, L"width"), Json::Num(o, L"height")))
                {
                    continue;
                }
                data.files += (data.files.empty() ? L"" : L"\n") + (L"📎 " + Json::Str(o, L"filename") + L"  " + Json::Str(o, L"url"));
            }
        }

        if (auto embeds = Json::Arr(m, L"embeds"))
        {
            for (auto const& e : embeds)
            {
                if (e.ValueType() != JsonValueType::Object) continue;
                auto o = e.GetObject();
                for (auto key : { L"image", L"thumbnail" })
                {
                    if (auto img = Json::Obj(o, key))
                    {
                        if (setImage(Json::Str(img, L"proxy_url"), Json::Num(img, L"width"), Json::Num(img, L"height"))) break;
                    }
                }
                auto title = Json::Str(o, L"title");
                auto description = Json::Str(o, L"description");
                if (!title.empty() || !description.empty())
                {
                    content += (content.empty() ? L"" : L"\n") + std::wstring{ L"▌ " } + title
                        + (description.empty() ? L"" : L"\n▌ " + FormatContent(description.substr(0, 300), m));
                }
            }
        }

        if (auto stickers = Json::Arr(m, L"sticker_items"))
        {
            for (auto const& s : stickers)
            {
                if (s.ValueType() == JsonValueType::Object)
                    content += (content.empty() ? L"" : L"\n") + (L"[Sticker : " + Json::Str(s.GetObject(), L"name") + L"]");
            }
        }

        if (Json::Get(m, L"edited_timestamp"))
        {
            content += L" (modifié)";
        }
        data.content = std::move(content);
        return data;
    }

    bool MainWindow::ShouldShowHeader(MessageData const* prev, MessageData const& cur)
    {
        return !prev || cur.forceHeader || prev->authorId != cur.authorId || cur.authorId.empty()
            || cur.unixMs - prev->unixMs > GroupWindowMs;
    }

    void MainWindow::AppendMessage(MessageData data)
    {
        uint32_t size = m_messageItems.Size();
        MessageData const* prev = size ? &Impl(m_messageItems.GetAt(size - 1))->Data() : nullptr;
        bool header = ShouldShowHeader(prev, data);
        m_messageItems.Append(make<MessageItem>(std::move(data), header));

        // Bound memory in busy channels: drop the oldest rows once we are past the cap.
        if (m_messageItems.Size() > MaxMessages)
        {
            m_messageItems.RemoveAt(0);
            m_hasMoreOlder = true;
            FixHeaderAt(0);
        }
    }

    void MainWindow::FixHeaderAt(uint32_t index)
    {
        if (index >= m_messageItems.Size())
        {
            return;
        }
        auto item = Impl(m_messageItems.GetAt(index));
        MessageData const* prev = index ? &Impl(m_messageItems.GetAt(index - 1))->Data() : nullptr;
        bool header = ShouldShowHeader(prev, item->Data());
        if (header != item->ShowsHeader())
        {
            m_messageItems.SetAt(index, make<MessageItem>(item->Data(), header));
        }
    }

    int MainWindow::FindMessage(std::wstring const& id)
    {
        for (int i = static_cast<int>(m_messageItems.Size()) - 1; i >= 0; --i)
        {
            if (Impl(m_messageItems.GetAt(i))->Data().id == id)
            {
                return i;
            }
        }
        return -1;
    }
}
