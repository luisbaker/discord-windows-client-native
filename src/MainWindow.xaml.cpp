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

        bool IsVoiceLike(int type)
        {
            return type == 2 || type == 13;
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

        std::wstring DefaultAvatar(std::wstring const& id, std::wstring const& discriminator)
        {
            uint64_t index = (discriminator.empty() || discriminator == L"0")
                ? (Json::U64(id) >> 22) % 6
                : Json::U64(discriminator) % 5;
            return std::wstring{ Discord::CdnBase } + L"/embed/avatars/" + std::to_wstring(index) + L".png";
        }

        std::wstring AvatarUrl(JsonObject const& user)
        {
            auto id = Json::Str(user, L"id");
            auto hash = Json::Str(user, L"avatar");
            if (!hash.empty())
            {
                return std::wstring{ Discord::CdnBase } + L"/avatars/" + id + L"/" + hash + L".png?size=64";
            }
            return DefaultAvatar(id, Json::Str(user, L"discriminator"));
        }

        SYSTEMTIME ToLocal(int64_t unixMs)
        {
            // unix ms -> FILETIME (100ns since 1601) -> local SYSTEMTIME
            ULARGE_INTEGER t;
            t.QuadPart = static_cast<ULONGLONG>(unixMs) * 10000ULL + 116444736000000000ULL;
            FILETIME ft{ t.LowPart, t.HighPart };
            SYSTEMTIME utc, local;
            FileTimeToSystemTime(&ft, &utc);
            SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
            return local;
        }

        int64_t DayNumber(SYSTEMTIME st)
        {
            st.wHour = st.wMinute = st.wSecond = st.wMilliseconds = 0;
            FILETIME ft;
            SystemTimeToFileTime(&st, &ft);
            return static_cast<int64_t>((static_cast<ULONGLONG>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime) / 864000000000ULL);
        }

        int64_t NowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        std::wstring FormatTime(int64_t unixMs)
        {
            SYSTEMTIME local = ToLocal(unixMs), now;
            GetLocalTime(&now);
            int64_t delta = DayNumber(now) - DayNumber(local);

            wchar_t buf[64];
            if (delta == 0)
            {
                swprintf_s(buf, L"Aujourd'hui à %02d:%02d", local.wHour, local.wMinute);
            }
            else if (delta == 1)
            {
                swprintf_s(buf, L"Hier à %02d:%02d", local.wHour, local.wMinute);
            }
            else
            {
                swprintf_s(buf, L"%02d/%02d/%04d %02d:%02d", local.wDay, local.wMonth, local.wYear, local.wHour, local.wMinute);
            }
            return buf;
        }

        std::wstring LongDate(int64_t unixMs)
        {
            SYSTEMTIME local = ToLocal(unixMs);
            wchar_t buf[96]{};
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &local, nullptr, buf, 96, nullptr);
            return buf;
        }

        std::wstring StatusLabel(std::wstring const& status)
        {
            if (status == L"idle") return L"Inactif";
            if (status == L"dnd") return L"Ne pas déranger";
            if (status == L"invisible" || status == L"offline") return L"Invisible";
            return L"En ligne";
        }

        uint32_t StatusColor(std::wstring const& status)
        {
            if (status == L"idle") return 0xF0B232;
            if (status == L"dnd") return 0xF23F43;
            if (status == L"invisible" || status == L"offline") return 0x80848E;
            return 0x23A55A;
        }

        std::wstring ActivityText(JsonArray const& activities)
        {
            if (!activities)
            {
                return {};
            }
            std::wstring fallback;
            for (auto const& a : activities)
            {
                if (a.ValueType() != JsonValueType::Object) continue;
                auto o = a.GetObject();
                auto name = Json::Str(o, L"name");
                switch (static_cast<int>(Json::Num(o, L"type", -1)))
                {
                case 4: if (auto state = Json::Str(o, L"state"); !state.empty()) return state; break;
                case 0: if (fallback.empty()) fallback = L"Joue à " + name; break;
                case 1: if (fallback.empty()) fallback = L"Streame " + name; break;
                case 2: if (fallback.empty()) fallback = L"Écoute " + (name == L"Spotify" ? Json::Str(o, L"details") : name); break;
                case 3: if (fallback.empty()) fallback = L"Regarde " + name; break;
                case 5: if (fallback.empty()) fallback = L"Participe à " + name; break;
                }
            }
            return fallback;
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

        std::vector<uint64_t> RoleIds(JsonObject const& member)
        {
            std::vector<uint64_t> roles;
            if (auto array = Json::Arr(member, L"roles"))
            {
                roles.reserve(array.Size());
                for (auto const& id : array)
                {
                    if (id.ValueType() == JsonValueType::String)
                    {
                        roles.push_back(Json::U64(id.GetString()));
                    }
                }
            }
            return roles;
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
        MemberList().ItemsSource(m_memberItems);

        m_typingTimer = m_dispatcher.CreateTimer();
        m_typingTimer.Interval(std::chrono::seconds(1));
        m_typingTimer.Tick([weak = get_weak()](auto&&, auto&&)
        {
            if (auto self = weak.get()) self->UpdateTypingText();
        });

        // Minimized = background mode: give memory back, lower memory & CPU priority.
        AppWindow().Changed([weak = get_weak()](Microsoft::UI::Windowing::AppWindow const& window,
                                                Microsoft::UI::Windowing::AppWindowChangedEventArgs const&)
        {
            auto self = weak.get();
            auto presenter = window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
            if (self && presenter)
            {
                self->SetBackgroundMode(presenter.State() == Microsoft::UI::Windowing::OverlappedPresenterState::Minimized);
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

    void MainWindow::SetBackgroundMode(bool background)
    {
        if (background == m_background)
        {
            return;
        }
        m_background = background;

        // Memory priority: in background our pages are the first to be trimmed under pressure.
        MEMORY_PRIORITY_INFORMATION memory{ background ? MEMORY_PRIORITY_LOW : MEMORY_PRIORITY_NORMAL };
        SetProcessInformation(GetCurrentProcess(), ProcessMemoryPriority, &memory, sizeof(memory));

        // EcoQoS: efficiency cores / lower clocks while nobody is looking.
        PROCESS_POWER_THROTTLING_STATE power{};
        power.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        power.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        power.StateMask = background ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &power, sizeof(power));

        if (background)
        {
            ::DiscordWin3::ImageCache::Clear();
            HeapCompact(GetProcessHeap(), 0);
            SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0);
        }
    }

    // ------------------------------------------------------------------ animations

    void MainWindow::MorphGuild(UIElement const& root, bool squircle, bool animate)
    {
        using namespace Microsoft::UI::Composition;
        auto element = root.try_as<FrameworkElement>();
        if (!element)
        {
            return;
        }
        auto visual = Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(root);
        auto compositor = visual.Compositor();
        auto geometry = element.Tag().try_as<CompositionRoundedRectangleGeometry>();
        if (!geometry)
        {
            geometry = compositor.CreateRoundedRectangleGeometry();
            geometry.Size({ 48.f, 48.f });
            geometry.CornerRadius({ 24.f, 24.f });
            visual.Clip(compositor.CreateGeometricClip(geometry));
            element.Tag(geometry);
        }

        float radius = squircle ? 16.f : 24.f;
        if (!animate)
        {
            geometry.CornerRadius({ radius, radius });
            return;
        }
        auto animation = compositor.CreateVector2KeyFrameAnimation();
        animation.InsertKeyFrame(1.f, { radius, radius },
            compositor.CreateCubicBezierEasingFunction({ 0.2f, 0.f }, { 0.f, 1.f }));
        animation.Duration(std::chrono::milliseconds(170));
        geometry.StartAnimation(L"CornerRadius", animation);
    }

    void MainWindow::OnGuildContainerChanging(ListViewBase const&, ContainerContentChangingEventArgs const& args)
    {
        if (args.InRecycleQueue())
        {
            return;
        }
        auto container = args.ItemContainer();
        auto root = container.ContentTemplateRoot().try_as<FrameworkElement>();
        if (!root)
        {
            return;
        }

        bool fresh = !root.Tag();
        MorphGuild(root, container.IsSelected(), false);
        if (!fresh)
        {
            return;
        }

        // Hover: circle -> rounded square, exactly like Discord's server rail.
        weak_ref<Primitives::SelectorItem> weakContainer{ container };
        weak_ref<FrameworkElement> weakRoot{ root };
        auto weakSelf = get_weak();
        root.PointerEntered([weakSelf, weakRoot](IInspectable const&, Input::PointerRoutedEventArgs const&)
        {
            auto self = weakSelf.get();
            if (auto r = weakRoot.get(); self && r) self->MorphGuild(r, true, true);
        });
        root.PointerExited([weakSelf, weakRoot, weakContainer](IInspectable const&, Input::PointerRoutedEventArgs const&)
        {
            auto self = weakSelf.get();
            auto r = weakRoot.get();
            auto c = weakContainer.get();
            if (self && r && c) self->MorphGuild(r, c.IsSelected(), true);
        });
    }

    void MainWindow::UpdateGuildMorphs()
    {
        for (uint32_t i = 0; i < m_guildItems.Size(); ++i)
        {
            auto container = GuildList().ContainerFromIndex(i).try_as<ListViewItem>();
            if (!container)
            {
                continue;
            }
            if (auto root = container.ContentTemplateRoot())
            {
                MorphGuild(root, container.IsSelected(), true);
            }
        }
    }

    void MainWindow::AnimateMessagesIn()
    {
        // OpacityTransition / TranslationTransition declared in XAML do the interpolation.
        MessageList().Opacity(1);
        MessageList().Translation({ 0, 0, 0 });
    }

    // ------------------------------------------------------------------ login

    void MainWindow::ShowLogin(std::wstring const& error)
    {
        ChatRoot().Opacity(0);
        ChatRoot().Visibility(Visibility::Collapsed);
        LoginRoot().Visibility(Visibility::Visible);
        LoginRoot().Opacity(1);
        LoginError().Text(error);
        TokenLoginButton().IsEnabled(true);
        StatusText().Text(L"");
        StartQr();
    }

    void MainWindow::ShowChat()
    {
        ChatRoot().Visibility(Visibility::Visible);
        ChatRoot().Opacity(1);
        LoginRoot().Opacity(0);

        // Collapse the login layer once its fade-out is done (frees its visuals).
        [](weak_ref<MainWindow> weak) -> fire_and_forget
        {
            co_await resume_after(std::chrono::milliseconds(250));
            auto self = weak.get();
            if (!self) co_return;
            co_await wil::resume_foreground(self->m_dispatcher);
            if (self->LoginRoot().Opacity() == 0)
            {
                self->LoginRoot().Visibility(Visibility::Collapsed);
                self->QrImage().Source(nullptr);
            }
        }(get_weak());
    }

    void MainWindow::StartQr()
    {
        if (m_remoteAuth)
        {
            m_remoteAuth->Stop();
        }
        QrImage().Opacity(0);
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
                w->QrImage().Opacity(0.15);
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
                w->QrImage().Opacity(0);
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
        QrImage().Opacity(1);
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
        ShowChat();
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
        m_users.clear();
        m_members.clear();
        m_requestedMembers.clear();
        m_channelNames.clear();
        m_roleNames.clear();
        m_currentGuildId.clear();
        m_currentChannelId.clear();
        ++m_channelGeneration;
        m_guildItems.Clear();
        m_channelItems.Clear();
        m_messageItems.Clear();
        m_memberItems.Clear();
        m_memberListGuild.clear();
        m_collapsed.clear();
        m_typing.clear();
        m_typingTimer.Stop();
        TypingText().Text(L"");
        ChannelTitle().Text(L"");
        ChannelGlyph().Text(L"");
        ChannelAvatarBorder().Visibility(Visibility::Collapsed);
        TitleText().Text(L"Discord Win3");
        TitleIcon().Source(nullptr);
        SelfName().Text(L"");
        SelfStatusText().Text(L"");
        SelfAvatar().Source(nullptr);
        Composer().IsEnabled(false);
        AttachButton().IsEnabled(false);
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
                if (m_typing.erase(Json::Str(Json::Obj(d, L"author"), L"id")))
                {
                    UpdateTypingText();
                }
                AppendMessage(BuildMessage(d));
            }
        }
        else if (type == L"TYPING_START")
        {
            OnTypingStart(d);
        }
        else if (type == L"GUILD_MEMBER_LIST_UPDATE")
        {
            OnMemberListUpdate(d);
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
                auto old = Impl(m_messageItems.GetAt(index));
                m_messageItems.SetAt(index, make<MessageItem>(BuildMessage(d), old->ShowsHeader(), old->Day()));
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
        else if (type == L"GUILD_MEMBERS_CHUNK")
        {
            OnMembersChunk(d);
        }
        else if (type == L"GUILD_MEMBER_UPDATE")
        {
            if (auto guild = FindGuild(Json::Str(d, L"guild_id")))
            {
                CacheMember(*guild, d);
            }
        }
        else if (type == L"VOICE_STATE_UPDATE")
        {
            auto guild = FindGuild(Json::Str(d, L"guild_id"));
            if (!guild)
            {
                return;
            }
            JsonArray single;
            single.Append(d);
            ParseVoiceStates(*guild, single);
            if (guild->id == m_currentGuildId)
            {
                RefreshChannelList();
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
            m_members.erase(id);
            RefreshGuildRail();
        }
        else if (type == L"CHANNEL_CREATE" || type == L"CHANNEL_UPDATE" || type == L"CHANNEL_DELETE")
        {
            auto channel = ParseChannel(d);
            auto guildId = Json::Str(d, L"guild_id");
            std::vector<ChannelInfo>* list = nullptr;
            if (guildId.empty())
            {
                ParseDmChannel(d, channel);
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

    UserInfo const& MainWindow::CacheUser(JsonObject const& user)
    {
        auto& info = m_users[Json::Str(user, L"id")];
        info.name = UserDisplayName(user);
        info.avatarUrl = AvatarUrl(user);
        return info;
    }

    void MainWindow::CacheMember(GuildInfo const& guild, JsonObject const& member)
    {
        auto user = Json::Obj(member, L"user");
        auto userId = user ? Json::Str(user, L"id") : Json::Str(member, L"user_id");
        if (userId.empty())
        {
            return;
        }
        if (user)
        {
            CacheUser(user);
        }

        GuildMember info;
        info.nick = Json::Str(member, L"nick");
        auto avatar = Json::Str(member, L"avatar");
        if (!avatar.empty())
        {
            info.avatarUrl = std::wstring{ Discord::CdnBase } + L"/guilds/" + guild.id + L"/users/" + userId
                + L"/avatars/" + avatar + L".png?size=64";
        }

        // Name color = color of the highest positioned colored role.
        int best = -1;
        for (auto roleId : RoleIds(member))
        {
            auto it = guild.roles.find(roleId);
            if (it != guild.roles.end() && it->second.color != 0 && it->second.position > best)
            {
                best = it->second.position;
                info.color = it->second.color;
            }
        }
        m_members[guild.id][userId] = std::move(info);
    }

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

    void MainWindow::ParseDmChannel(JsonObject const& c, ChannelInfo& info)
    {
        std::wstring joined;
        std::wstring firstAvatar;
        auto append = [&](UserInfo const& u)
        {
            joined += (joined.empty() ? L"" : L", ") + u.name;
            if (firstAvatar.empty()) firstAvatar = u.avatarUrl;
        };

        if (auto recipients = Json::Arr(c, L"recipients"))
        {
            for (auto const& r : recipients)
            {
                if (r.ValueType() == JsonValueType::Object)
                {
                    append(CacheUser(r.GetObject()));
                }
            }
        }
        else if (auto ids = Json::Arr(c, L"recipient_ids"))
        {
            for (auto const& r : ids)
            {
                if (r.ValueType() != JsonValueType::String) continue;
                auto it = m_users.find(std::wstring{ r.GetString() });
                if (it != m_users.end()) append(it->second);
            }
        }

        auto name = Json::Str(c, L"name");
        info.name = !name.empty() ? name : (joined.empty() ? L"Groupe sans nom" : joined);

        auto icon = Json::Str(c, L"icon");
        if (info.type == 3)
        {
            info.avatarUrl = icon.empty() ? L""
                : std::wstring{ Discord::CdnBase } + L"/channel-icons/" + info.id + L"/" + icon + L".png?size=64";
        }
        else
        {
            info.avatarUrl = firstAvatar;
        }
    }

    void MainWindow::ParseVoiceStates(GuildInfo& guild, JsonArray const& states)
    {
        if (!states)
        {
            return;
        }
        for (auto const& s : states)
        {
            if (s.ValueType() != JsonValueType::Object) continue;
            auto state = s.GetObject();
            auto userId = Json::Str(state, L"user_id");
            auto channelId = Json::Str(state, L"channel_id");
            if (auto member = Json::Obj(state, L"member"))
            {
                CacheMember(guild, member);
            }
            if (channelId.empty())
            {
                guild.voice.erase(userId);
            }
            else
            {
                guild.voice[userId] = channelId;
            }
        }
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
                auto id = Json::U64(roleId);
                guild.perms.rolePermissions[id] = Json::U64(role, L"permissions");
                guild.roles[id] = { static_cast<int>(Json::Num(role, L"position")),
                                    static_cast<uint32_t>(Json::Num(role, L"color")) };
                m_roleNames[roleId] = Json::Str(role, L"name");
            }
        }

        if (auto members = Json::Arr(g, L"members"))
        {
            for (auto const& m : members)
            {
                if (m.ValueType() != JsonValueType::Object) continue;
                auto member = m.GetObject();
                CacheMember(guild, member);
                auto user = Json::Obj(member, L"user");
                auto userId = user ? Json::Str(user, L"id") : Json::Str(member, L"user_id");
                if (userId == m_selfId)
                {
                    guild.perms.known = true;
                    guild.perms.selfRoles = RoleIds(member);
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

        ParseVoiceStates(guild, Json::Arr(g, L"voice_states"));
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
        auto const& self = CacheUser(user);
        SelfName().Text(self.name);
        SelfAvatar().Source(::DiscordWin3::ImageCache::Get(self.avatarUrl, 32));

        // Own status + custom status, as shown in the user panel.
        std::wstring status = L"online";
        std::wstring customStatus;
        if (auto settings = Json::Obj(d, L"user_settings"))
        {
            if (auto s = Json::Str(settings, L"status"); !s.empty()) status = s;
            customStatus = Json::Str(Json::Obj(settings, L"custom_status"), L"text");
        }
        if (auto sessions = Json::Arr(d, L"sessions"); sessions && sessions.Size() > 0
            && sessions.GetAt(0).ValueType() == JsonValueType::Object)
        {
            if (auto s = Json::Str(sessions.GetAt(0).GetObject(), L"status"); !s.empty()) status = s;
        }
        SelfStatus().Fill(SolidBrush(StatusColor(status)).as<Media::SolidColorBrush>());
        SelfStatusText().Text(customStatus.empty() ? StatusLabel(status) : customStatus);

        if (auto users = Json::Arr(d, L"users"))
        {
            for (auto const& u : users)
            {
                if (u.ValueType() == JsonValueType::Object)
                {
                    CacheUser(u.GetObject());
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
                if (mergedMembers && i < mergedMembers.Size()
                    && mergedMembers.GetAt(i).ValueType() == JsonValueType::Array)
                {
                    for (auto const& m : mergedMembers.GetAt(i).GetArray())
                    {
                        if (m.ValueType() != JsonValueType::Object) continue;
                        auto member = m.GetObject();
                        CacheMember(guild, member);
                        if (Json::Str(member, L"user_id") == m_selfId && !guild.perms.known)
                        {
                            guild.perms.known = true;
                            guild.perms.selfRoles = RoleIds(member);
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
                ParseDmChannel(o, info);
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
            std::vector<ChannelInfo const*> dms;
            dms.reserve(m_dms.size());
            for (auto const& c : m_dms) dms.push_back(&c);
            std::sort(dms.begin(), dms.end(), [](ChannelInfo const* a, ChannelInfo const* b)
            {
                return Json::SnowflakeLess(b->lastMessageId, a->lastMessageId);
            });
            for (auto c : dms)
            {
                items.push_back(make<ChannelItem>(hstring{ c->id }, hstring{ c->name }, hstring{ Glyph(c->type) },
                                                  ChannelKind::Text, c->avatarUrl));
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

            // Voice participants grouped by channel.
            std::unordered_map<std::wstring, std::vector<std::wstring>> voiceUsers;
            std::vector<std::wstring> unknown;
            auto& members = m_members[guild->id];
            for (auto const& [userId, channelId] : guild->voice)
            {
                voiceUsers[channelId].push_back(userId);
                if (!m_users.contains(userId) && m_requestedMembers.insert(guild->id + L":" + userId).second)
                {
                    unknown.push_back(userId);
                }
            }
            if (!unknown.empty() && m_gateway)
            {
                m_gateway->RequestGuildMembers(guild->id, unknown);
            }

            std::vector<ChannelInfo const*> categories;
            std::unordered_map<std::wstring, std::vector<ChannelInfo const*>> children;
            for (auto const& c : guild->channels)
            {
                if (c.type == 4)
                {
                    categories.push_back(&c);
                }
                else if ((IsTextLike(c.type) || IsVoiceLike(c.type) || c.type == 15) && guild->perms.CanView(c.overwrites))
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
                    items.push_back(make<ChannelItem>(hstring{ c->id }, hstring{ c->name }, hstring{ Glyph(c->type) },
                                                      IsTextLike(c->type) ? ChannelKind::Text : ChannelKind::Voice));
                    auto it = voiceUsers.find(c->id);
                    if (it == voiceUsers.end()) continue;
                    for (auto const& userId : it->second)
                    {
                        auto user = m_users.find(userId);
                        auto member = members.find(userId);
                        std::wstring name = member != members.end() && !member->second.nick.empty() ? member->second.nick
                            : user != m_users.end() ? user->second.name : L"…";
                        std::wstring avatar = member != members.end() && !member->second.avatarUrl.empty() ? member->second.avatarUrl
                            : user != m_users.end() ? user->second.avatarUrl : DefaultAvatar(userId, L"0");
                        items.push_back(make<ChannelItem>(hstring{ L"voice:" + userId }, hstring{ name }, L"",
                                                          ChannelKind::VoiceUser, avatar));
                    }
                }
            };

            emit(children[L""]);
            for (auto category : categories)
            {
                auto& list = children[category->id];
                if (list.empty()) continue;
                bool collapsed = m_collapsed.contains(category->id);
                items.push_back(make<ChannelItem>(hstring{ category->id }, hstring{ Upper(category->name) },
                                                  collapsed ? L"›" : L"⌄", ChannelKind::Category));
                if (collapsed)
                {
                    // Like Discord: a collapsed category still shows the channel you are in.
                    std::erase_if(list, [&](ChannelInfo const* c) { return c->id != m_currentChannelId; });
                }
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
        UpdateGuildMorphs();
        auto item = GuildList().SelectedItem().try_as<DiscordWin3::GuildItem>();
        if (!item || item.Id() == m_currentGuildId)
        {
            return;
        }
        m_currentGuildId = item.Id();
        m_memberItems.Clear();
        m_memberListGuild.clear();
        MembersPane().Visibility(Show(MembersToggle().IsChecked().Value() && m_currentGuildId != HomeId));
        RefreshChannelList();
        UpdateTitle();

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
        if (!item || item.Id() == m_currentChannelId)
        {
            return;
        }
        if (!item.IsTextLike())
        {
            if (!item.IsCategory() && !item.IsVoiceUser())
            {
                StatusText().Text(L"Les appels vocaux arrivent dans une prochaine version.");
            }
            // Keep the highlight on the channel that is actually open.
            for (uint32_t i = 0; i < m_channelItems.Size(); ++i)
            {
                if (m_channelItems.GetAt(i).as<DiscordWin3::ChannelItem>().Id() == m_currentChannelId)
                {
                    ChannelList().SelectedIndex(static_cast<int>(i));
                    return;
                }
            }
            ChannelList().SelectedIndex(-1);
            return;
        }
        // Header: "# name" for guild channels, avatar + name for DMs (like the official client).
        auto impl = get_self<implementation::ChannelItem>(item);
        bool hasAvatar = impl->AvatarVisibility() == Visibility::Visible;
        ChannelGlyph().Text(hasAvatar ? L"" : item.Glyph());
        ChannelGlyph().Visibility(Show(!hasAvatar));
        ChannelAvatarBorder().Visibility(Show(hasAvatar));
        ChannelAvatar().Source(hasAvatar ? item.Avatar() : nullptr);
        LoadChannel(std::wstring{ item.Id() }, std::wstring{ item.Name() });
        Composer().PlaceholderText(L"Envoyer un message " + std::wstring{ m_currentGuildId == HomeId ? L"à @" : L"dans #" }
                                   + std::wstring{ item.Name() });
        UpdateTitle();
    }

    void MainWindow::OnChannelClicked(IInspectable const&, ItemClickEventArgs const& e)
    {
        auto item = e.ClickedItem().try_as<DiscordWin3::ChannelItem>();
        if (!item || !item.IsCategory())
        {
            return;
        }
        std::wstring id{ item.Id() };
        if (!m_collapsed.erase(id))
        {
            m_collapsed.insert(id);
        }
        RefreshChannelList();
    }

    void MainWindow::OnToggleMembers(IInspectable const&, RoutedEventArgs const&)
    {
        bool show = MembersToggle().IsChecked().Value() && m_currentGuildId != HomeId;
        MembersPane().Visibility(Show(show));
        if (!show)
        {
            // Hidden list = no need to keep its rows (and avatars) alive.
            m_memberItems.Clear();
            m_memberListGuild.clear();
        }
        else
        {
            SubscribeMembers();
        }
    }

    void MainWindow::UpdateTitle()
    {
        if (m_currentGuildId == HomeId)
        {
            auto name = ChannelTitle().Text();
            TitleText().Text(name.empty() ? hstring{ L"Messages privés" } : name);
            TitleIcon().Source(ChannelAvatar().Source());
            return;
        }
        if (auto guild = FindGuild(m_currentGuildId))
        {
            TitleText().Text(guild->name);
            TitleIcon().Source(guild->icon.empty() ? nullptr
                : ::DiscordWin3::ImageCache::Get(std::wstring{ Discord::CdnBase } + L"/icons/" + guild->id + L"/" + guild->icon + L".png?size=96", 16));
        }
    }

    // ------------------------------------------------------------------ members

    void MainWindow::ApplyMember(MessageData& data)
    {
        if (m_currentGuildId.empty() || m_currentGuildId == HomeId)
        {
            return;
        }
        auto guild = m_members.find(m_currentGuildId);
        if (guild == m_members.end())
        {
            return;
        }
        auto member = guild->second.find(data.authorId);
        if (member == guild->second.end())
        {
            return;
        }
        if (!member->second.nick.empty()) data.authorName = member->second.nick;
        if (!member->second.avatarUrl.empty()) data.avatarUrl = member->second.avatarUrl;
        data.color = member->second.color;
    }

    void MainWindow::RequestMissingMembers()
    {
        if (!m_gateway || m_currentGuildId.empty() || m_currentGuildId == HomeId)
        {
            return;
        }
        auto& known = m_members[m_currentGuildId];
        std::vector<std::wstring> missing;
        for (uint32_t i = 0; i < m_messageItems.Size(); ++i)
        {
            auto const& id = Impl(m_messageItems.GetAt(i))->Data().authorId;
            if (id.empty() || known.contains(id))
            {
                continue;
            }
            if (m_requestedMembers.insert(m_currentGuildId + L":" + id).second)
            {
                missing.push_back(id);
            }
        }
        if (!missing.empty())
        {
            m_gateway->RequestGuildMembers(m_currentGuildId, std::move(missing));
        }
    }

    void MainWindow::OnMembersChunk(JsonObject const& d)
    {
        auto guild = FindGuild(Json::Str(d, L"guild_id"));
        auto members = Json::Arr(d, L"members");
        if (!guild || !members)
        {
            return;
        }

        std::unordered_set<std::wstring> updated;
        for (auto const& m : members)
        {
            if (m.ValueType() != JsonValueType::Object) continue;
            auto member = m.GetObject();
            CacheMember(*guild, member);
            if (auto user = Json::Obj(member, L"user"))
            {
                updated.insert(Json::Str(user, L"id"));
            }
        }
        if (guild->id != m_currentGuildId || updated.empty())
        {
            return;
        }

        // Re-skin the rows of those authors with their nick / guild avatar / role color.
        for (uint32_t i = 0; i < m_messageItems.Size(); ++i)
        {
            auto item = Impl(m_messageItems.GetAt(i));
            if (!updated.contains(item->Data().authorId))
            {
                continue;
            }
            MessageData data = item->Data();
            ApplyMember(data);
            if (data.authorName != item->Data().authorName || data.avatarUrl != item->Data().avatarUrl || data.color != item->Data().color)
            {
                m_messageItems.SetAt(i, make<MessageItem>(std::move(data), item->ShowsHeader(), item->Day()));
            }
        }

        bool voiceTouched = std::any_of(updated.begin(), updated.end(), [&](auto const& id) { return guild->voice.contains(id); });
        if (voiceTouched)
        {
            RefreshChannelList();
        }
    }

    // ------------------------------------------------------------------ messages

    fire_and_forget MainWindow::LoadChannel(std::wstring id, std::wstring title)
    {
        auto strong = get_strong();
        auto generation = ++m_channelGeneration;
        auto rest = m_rest;
        m_currentChannelId = id;
        m_hasMoreOlder = false;
        m_loadingOlder = false;
        ChannelTitle().Text(title);
        Composer().IsEnabled(true);
        AttachButton().IsEnabled(true);
        StatusText().Text(L"");
        m_typing.clear();
        UpdateTypingText();
        SubscribeMembers();

        // Fade/slide the old content out while the new one loads.
        MessageList().Opacity(0);
        MessageList().Translation({ 0, 16, 0 });
        m_messageItems.Clear();
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
            RequestMissingMembers();
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
            info.body.push_back({ ::DiscordWin3::Segment::Kind::Text,
                message.starts_with(L"HTTP 403") ? L"Tu n'as pas accès à ce salon." : L"Erreur de chargement : " + message });
            AppendMessage(std::move(info));
        }
        AnimateMessagesIn();
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
                m_messageItems.InsertAt(0, MakeRow(batch[i], i > 0 ? &batch[i - 1] : nullptr));
            }
            FixHeaderAt(static_cast<uint32_t>(batch.size()));
            RequestMissingMembers();
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

    // ------------------------------------------------------------------ message body

    std::vector<::DiscordWin3::Segment> MainWindow::ParseBody(std::wstring const& raw, JsonObject const& m)
    {
        using Kind = ::DiscordWin3::Segment::Kind;
        std::vector<::DiscordWin3::Segment> out;
        auto push = [&](Kind kind, std::wstring text, std::wstring url = {})
        {
            if (text.empty() && kind != Kind::Emoji) return;
            if (kind == Kind::Text && !out.empty() && out.back().kind == Kind::Text)
            {
                out.back().text += text;
                return;
            }
            out.push_back({ kind, std::move(text), std::move(url) });
        };

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

        size_t i = 0;
        std::wstring text;
        auto flush = [&]() { push(Kind::Text, std::move(text)); text.clear(); };

        while (i < raw.size())
        {
            std::wstring_view rest{ raw.data() + i, raw.size() - i };

            // ```code block```
            if (rest.starts_with(L"```"))
            {
                auto end = raw.find(L"```", i + 3);
                if (end != std::wstring::npos)
                {
                    flush();
                    auto code = raw.substr(i + 3, end - i - 3);
                    // Drop the language hint on the first line ("```cpp").
                    if (auto nl = code.find(L'\n'); nl != std::wstring::npos && code.find(L' ') > nl && nl < 20)
                        code = code.substr(nl + 1);
                    while (!code.empty() && (code.back() == L'\n' || code.back() == L'\r')) code.pop_back();
                    push(Kind::CodeBlock, std::move(code));
                    i = end + 3;
                    continue;
                }
            }
            // `inline code`
            if (rest.starts_with(L"`"))
            {
                auto end = raw.find(L'`', i + 1);
                if (end != std::wstring::npos && end > i + 1)
                {
                    flush();
                    push(Kind::Code, raw.substr(i + 1, end - i - 1));
                    i = end + 1;
                    continue;
                }
            }
            // **bold**
            if (rest.starts_with(L"**"))
            {
                auto end = raw.find(L"**", i + 2);
                if (end != std::wstring::npos && end > i + 2)
                {
                    flush();
                    push(Kind::Bold, raw.substr(i + 2, end - i - 2));
                    i = end + 2;
                    continue;
                }
            }
            // bare links
            if (rest.starts_with(L"https://") || rest.starts_with(L"http://"))
            {
                size_t end = i;
                while (end < raw.size() && !iswspace(raw[end]) && raw[end] != L'>') ++end;
                flush();
                auto url = raw.substr(i, end - i);
                push(Kind::Link, url, url);
                i = end;
                continue;
            }
            // <@user> <@&role> <#channel> <:emoji:id> <t:123:R> <https://...>
            if (raw[i] == L'<')
            {
                auto close = raw.find(L'>', i);
                if (close != std::wstring::npos && close - i <= 100)
                {
                    std::wstring_view tag{ raw.data() + i + 1, close - i - 1 };
                    bool handled = true;
                    if (tag.starts_with(L"@&"))
                    {
                        auto it = m_roleNames.find(std::wstring{ tag.substr(2) });
                        flush();
                        push(Kind::Mention, L"@" + (it != m_roleNames.end() ? it->second : std::wstring{ L"rôle" }));
                    }
                    else if (tag.starts_with(L"@"))
                    {
                        auto id = std::wstring{ tag.substr(tag.starts_with(L"@!") ? 2 : 1) };
                        std::wstring name;
                        if (auto g = m_members.find(m_currentGuildId); g != m_members.end())
                        {
                            if (auto mem = g->second.find(id); mem != g->second.end()) name = mem->second.nick;
                        }
                        if (name.empty()) if (auto it = mentions.find(id); it != mentions.end()) name = it->second;
                        if (name.empty()) if (auto it = m_users.find(id); it != m_users.end()) name = it->second.name;
                        flush();
                        push(Kind::Mention, L"@" + (name.empty() ? std::wstring{ L"utilisateur" } : name));
                    }
                    else if (tag.starts_with(L"#"))
                    {
                        auto it = m_channelNames.find(std::wstring{ tag.substr(1) });
                        flush();
                        push(Kind::Mention, L"#" + (it != m_channelNames.end() ? it->second : std::wstring{ L"salon-inconnu" }));
                    }
                    else if (tag.starts_with(L":") || tag.starts_with(L"a:"))
                    {
                        auto start = tag.find(L':');
                        auto end = tag.find(L':', start + 1);
                        if (end == std::wstring_view::npos)
                        {
                            handled = false;
                        }
                        else
                        {
                            flush();
                            // Static PNG even for animated emojis: no GIF decoder kept alive per emoji.
                            auto id = std::wstring{ tag.substr(end + 1) };
                            push(Kind::Emoji, L":" + std::wstring{ tag.substr(start + 1, end - start - 1) } + L":",
                                 std::wstring{ Discord::CdnBase } + L"/emojis/" + id + L".png?size=48");
                        }
                    }
                    else if (tag.starts_with(L"t:"))
                    {
                        auto ts = tag.substr(2);
                        text += FormatTime(static_cast<int64_t>(Json::U64(ts.substr(0, ts.find(L':')))) * 1000);
                    }
                    else if (tag.starts_with(L"http"))
                    {
                        flush();
                        push(Kind::Link, std::wstring{ tag }, std::wstring{ tag });
                    }
                    else
                    {
                        handled = false;
                    }
                    if (handled)
                    {
                        i = close + 1;
                        continue;
                    }
                }
            }
            text.push_back(raw[i++]);
        }
        flush();
        return out;
    }

    std::wstring MainWindow::PlainText(std::wstring const& raw, JsonObject const& m)
    {
        std::wstring out;
        for (auto const& s : ParseBody(raw, m))
        {
            out += s.text;
        }
        return out;
    }

    void MainWindow::RenderBody(RichTextBlock const& block, MessageData const& data)
    {
        using Kind = ::DiscordWin3::Segment::Kind;
        using namespace Microsoft::UI::Xaml::Documents;

        block.Blocks().Clear();
        if (data.body.empty())
        {
            return;
        }

        // Emoji-only messages get "jumbo" emojis, like Discord.
        bool jumbo = std::all_of(data.body.begin(), data.body.end(), [](auto const& s)
        {
            return s.kind == Kind::Emoji || (s.kind == Kind::Text && std::all_of(s.text.begin(), s.text.end(), iswspace));
        });
        double emojiSize = jumbo ? 48 : 22;

        Paragraph paragraph;
        auto inlines = paragraph.Inlines();
        for (auto const& s : data.body)
        {
            switch (s.kind)
            {
            case Kind::Text:
            {
                Run run;
                run.Text(s.text);
                inlines.Append(run);
                break;
            }
            case Kind::Bold:
            {
                Run run;
                run.Text(s.text);
                run.FontWeight(Windows::UI::Text::FontWeight{ 700 });
                inlines.Append(run);
                break;
            }
            case Kind::Code:
            {
                Run run;
                run.Text(s.text);
                run.FontFamily(Media::FontFamily{ L"Cascadia Mono, Consolas" });
                run.FontSize(13);
                run.Foreground(SolidBrush(0xE6E6E6).as<Media::Brush>());
                inlines.Append(run);
                break;
            }
            case Kind::CodeBlock:
            {
                Border box;
                box.Background(SolidBrush(0x111214));
                box.BorderBrush(SolidBrush(0x2A2A2F));
                box.BorderThickness({ 1, 1, 1, 1 });
                box.CornerRadius({ 4, 4, 4, 4 });
                box.Padding({ 10, 8, 10, 8 });
                box.Margin({ 0, 4, 0, 4 });
                TextBlock code;
                code.Text(s.text);
                code.FontFamily(Media::FontFamily{ L"Cascadia Mono, Consolas" });
                code.FontSize(13);
                code.TextWrapping(TextWrapping::Wrap);
                code.IsTextSelectionEnabled(true);
                box.Child(code);
                InlineUIContainer container;
                container.Child(box);
                inlines.Append(LineBreak{});
                inlines.Append(container);
                inlines.Append(LineBreak{});
                break;
            }
            case Kind::Mention:
            {
                // Blurple pill.
                Border pill;
                pill.Background(SolidBrush(0x5865F2, 0x4D));
                pill.CornerRadius({ 3, 3, 3, 3 });
                pill.Padding({ 2, 0, 2, 0 });
                pill.Margin({ 0, 0, 0, -4 });
                TextBlock label;
                label.Text(s.text);
                label.FontSize(15);
                label.FontWeight(Windows::UI::Text::FontWeight{ 500 });
                label.Foreground(SolidBrush(0xC9CDFB));
                pill.Child(label);
                InlineUIContainer container;
                container.Child(pill);
                inlines.Append(container);
                break;
            }
            case Kind::Link:
            {
                Hyperlink link;
                try
                {
                    link.NavigateUri(Uri{ s.url });
                }
                catch (...)
                {
                }
                link.UnderlineStyle(UnderlineStyle::None);
                link.Foreground(SolidBrush(0x00A8FC));
                Run run;
                run.Text(s.text);
                link.Inlines().Append(run);
                inlines.Append(link);
                break;
            }
            case Kind::Emoji:
            {
                Image image;
                image.Width(emojiSize);
                image.Height(emojiSize);
                image.Margin({ 1, 0, 1, jumbo ? 0.0 : -5.0 });
                image.Source(::DiscordWin3::ImageCache::Get(s.url, static_cast<int>(emojiSize)));
                ToolTipService::SetToolTip(image, box_value(s.text));
                InlineUIContainer container;
                container.Child(image);
                inlines.Append(container);
                break;
            }
            }
        }
        block.Blocks().Append(paragraph);
    }

    void MainWindow::OnMessageContainerChanging(ListViewBase const&, ContainerContentChangingEventArgs const& args)
    {
        auto root = args.ItemContainer().ContentTemplateRoot().try_as<FrameworkElement>();
        if (!root)
        {
            return;
        }
        auto body = root.FindName(L"Body").try_as<RichTextBlock>();
        if (!body)
        {
            return;
        }
        if (args.InRecycleQueue())
        {
            body.Blocks().Clear();   // release inline images of rows scrolled away
            return;
        }
        RenderBody(body, Impl(args.Item())->Data());
    }

    MessageData MainWindow::BuildMessage(JsonObject const& m)
    {
        using Kind = ::DiscordWin3::Segment::Kind;
        MessageData data;
        data.id = Json::Str(m, L"id");
        data.unixMs = Json::SnowflakeMs(data.id);
        data.timestamp = FormatTime(data.unixMs);

        auto author = Json::Obj(m, L"author");
        data.authorId = Json::Str(author, L"id");
        if (!Json::Str(m, L"webhook_id").empty())
        {
            data.authorName = UserDisplayName(author);
            data.avatarUrl = AvatarUrl(author);
        }
        else
        {
            auto const& user = CacheUser(author);
            data.authorName = user.name;
            data.avatarUrl = user.avatarUrl;

            // MESSAGE_CREATE carries the member object: free nick/roles, cache them.
            if (auto member = Json::Obj(m, L"member"))
            {
                if (auto guild = FindGuild(Json::Str(m, L"guild_id")))
                {
                    member.Insert(L"user", author);
                    CacheMember(*guild, member);
                }
            }
            ApplyMember(data);
        }

        // Server tag next to the name ("⚡IPv6" in the official client).
        auto primaryGuild = Json::Obj(author, L"primary_guild");
        if (!primaryGuild) primaryGuild = Json::Obj(author, L"clan");
        if (primaryGuild && Json::Bool(primaryGuild, L"identity_enabled", true))
        {
            data.tag = Json::Str(primaryGuild, L"tag");
        }

        // Highlight rows that ping me.
        data.mentionsMe = Json::Bool(m, L"mention_everyone");
        if (auto list = Json::Arr(m, L"mentions"))
        {
            for (auto const& u : list)
            {
                if (u.ValueType() == JsonValueType::Object && Json::Str(u.GetObject(), L"id") == m_selfId)
                {
                    data.mentionsMe = true;
                }
            }
        }

        data.body = ParseBody(Json::Str(m, L"content"), m);
        auto system = [&](std::wstring text)
        {
            data.body = { { Kind::Text, std::move(text) } };
            data.forceHeader = true;
        };

        switch (static_cast<int>(Json::Num(m, L"type")))
        {
        case 7: system(L"→ a rejoint le serveur."); break;
        case 6: system(L"📌 a épinglé un message."); break;
        case 8: case 9: case 10: case 11: system(L"🚀 a boosté le serveur !"); break;
        case 19:
            if (auto ref = Json::Obj(m, L"referenced_message"))
            {
                auto snippet = PlainText(Json::Str(ref, L"content"), ref);
                if (snippet.size() > 100) snippet = snippet.substr(0, 100) + L"…";
                for (auto& c : snippet) if (c == L'\n') c = L' ';
                data.reply = L"↱ @" + UserDisplayName(Json::Obj(ref, L"author")) + L"  " + snippet;
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
                if (data.embedTitle.empty() && data.embedDescription.empty())
                {
                    data.embedProvider = Json::Str(Json::Obj(o, L"provider"), L"name");
                    if (data.embedProvider.empty()) data.embedProvider = Json::Str(Json::Obj(o, L"author"), L"name");
                    data.embedTitle = Json::Str(o, L"title");
                    auto description = Json::Str(o, L"description");
                    data.embedDescription = PlainText(description.substr(0, 350), m);
                    data.embedColor = static_cast<uint32_t>(Json::Num(o, L"color"));
                }
            }
        }

        if (auto stickers = Json::Arr(m, L"sticker_items"))
        {
            for (auto const& s : stickers)
            {
                if (s.ValueType() == JsonValueType::Object)
                    data.body.push_back({ Kind::Text, (data.body.empty() ? L"" : L"\n") + (L"[Sticker : " + Json::Str(s.GetObject(), L"name") + L"]") });
            }
        }

        if (Json::Get(m, L"edited_timestamp") && !data.body.empty())
        {
            data.body.push_back({ Kind::Text, L" (modifié)" });
        }
        return data;
    }

    bool MainWindow::ShouldShowHeader(MessageData const* prev, MessageData const& cur)
    {
        return !prev || cur.forceHeader || prev->authorId != cur.authorId || cur.authorId.empty()
            || cur.unixMs - prev->unixMs > GroupWindowMs;
    }

    std::wstring MainWindow::DayLabel(MessageData const* prev, MessageData const& cur)
    {
        if (cur.unixMs == 0 || (prev && DayNumber(ToLocal(prev->unixMs)) == DayNumber(ToLocal(cur.unixMs))))
        {
            return {};
        }
        return LongDate(cur.unixMs);
    }

    DiscordWin3::MessageItem MainWindow::MakeRow(MessageData data, MessageData const* prev)
    {
        auto day = DayLabel(prev, data);
        bool header = !day.empty() || ShouldShowHeader(prev, data);
        return make<MessageItem>(std::move(data), header, std::move(day));
    }

    void MainWindow::AppendMessage(MessageData data)
    {
        uint32_t size = m_messageItems.Size();
        MessageData const* prev = size ? &Impl(m_messageItems.GetAt(size - 1))->Data() : nullptr;
        m_messageItems.Append(MakeRow(std::move(data), prev));

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
        auto day = DayLabel(prev, item->Data());
        bool header = !day.empty() || ShouldShowHeader(prev, item->Data());
        if (header != item->ShowsHeader() || day != item->Day())
        {
            m_messageItems.SetAt(index, make<MessageItem>(item->Data(), header, std::move(day)));
        }
    }

    // ------------------------------------------------------------------ member list

    void MainWindow::SubscribeMembers()
    {
        if (!m_gateway || m_currentGuildId.empty() || m_currentGuildId == HomeId || m_currentChannelId.empty()
            || !MembersToggle().IsChecked().Value())
        {
            return;
        }
        if (m_memberListGuild != m_currentGuildId)
        {
            m_memberItems.Clear();
        }
        m_memberListGuild = m_currentGuildId;
        m_gateway->SubscribeMemberList(m_currentGuildId, m_currentChannelId);
    }

    IInspectable MainWindow::BuildMemberRow(JsonObject const& item, GuildInfo const& guild)
    {
        if (auto group = Json::Obj(item, L"group"))
        {
            auto id = Json::Str(group, L"id");
            std::wstring title = id == L"online" ? L"En ligne" : id == L"offline" ? L"Hors ligne" : L"";
            if (title.empty())
            {
                auto it = m_roleNames.find(id);
                title = it != m_roleNames.end() ? it->second : L"Rôle";
            }
            int count = static_cast<int>(Json::Num(group, L"count", -1));
            if (count < 0) { auto it = m_memberGroupCounts.find(id); count = it != m_memberGroupCounts.end() ? it->second : 0; }
            return make<MemberItem>(title + L" — " + std::to_wstring(count));
        }

        auto member = Json::Obj(item, L"member");
        auto user = Json::Obj(member, L"user");
        if (!member || !user)
        {
            return nullptr;
        }
        CacheMember(guild, member);
        auto userId = Json::Str(user, L"id");
        auto const& cachedUser = m_users[userId];
        auto const& cachedMember = m_members[guild.id][userId];

        auto presence = Json::Obj(member, L"presence");
        return make<MemberItem>(
            cachedMember.nick.empty() ? cachedUser.name : cachedMember.nick,
            cachedMember.color,
            cachedMember.avatarUrl.empty() ? cachedUser.avatarUrl : cachedMember.avatarUrl,
            Json::Str(presence, L"status"),
            ActivityText(Json::Arr(presence, L"activities")));
    }

    void MainWindow::OnMemberListUpdate(JsonObject const& d)
    {
        constexpr uint32_t MaxRows = 100;
        auto guild = FindGuild(Json::Str(d, L"guild_id"));
        auto ops = Json::Arr(d, L"ops");
        if (!guild || !ops || guild->id != m_memberListGuild)
        {
            return;
        }

        // Group counts live in d.groups (items only carry the id).
        if (auto groups = Json::Arr(d, L"groups"))
        {
            for (auto const& g : groups)
            {
                if (g.ValueType() == JsonValueType::Object)
                    m_memberGroupCounts[Json::Str(g.GetObject(), L"id")] = static_cast<int>(Json::Num(g.GetObject(), L"count"));
            }
        }

        for (auto const& value : ops)
        {
            if (value.ValueType() != JsonValueType::Object) continue;
            auto op = value.GetObject();
            auto kind = Json::Str(op, L"op");
            auto index = static_cast<uint32_t>(Json::Num(op, L"index"));

            if (kind == L"SYNC")
            {
                auto range = Json::Arr(op, L"range");
                if (!range || range.Size() < 1 || range.GetNumberAt(0) != 0) continue;
                std::vector<IInspectable> rows;
                if (auto items = Json::Arr(op, L"items"))
                {
                    for (auto const& it : items)
                    {
                        if (it.ValueType() != JsonValueType::Object) continue;
                        if (auto row = BuildMemberRow(it.GetObject(), *guild)) rows.push_back(row);
                        if (rows.size() >= MaxRows) break;
                    }
                }
                m_memberItems.ReplaceAll(rows);
            }
            else if (kind == L"INSERT" && index <= m_memberItems.Size() && index < MaxRows)
            {
                if (auto row = BuildMemberRow(Json::Obj(op, L"item"), *guild))
                {
                    m_memberItems.InsertAt(index, row);
                    if (m_memberItems.Size() > MaxRows) m_memberItems.RemoveAtEnd();
                }
            }
            else if (kind == L"UPDATE" && index < m_memberItems.Size())
            {
                if (auto row = BuildMemberRow(Json::Obj(op, L"item"), *guild))
                {
                    m_memberItems.SetAt(index, row);
                }
            }
            else if (kind == L"DELETE" && index < m_memberItems.Size())
            {
                m_memberItems.RemoveAt(index);
            }
            else if (kind == L"INVALIDATE")
            {
                auto range = Json::Arr(op, L"range");
                if (range && range.Size() > 0 && range.GetNumberAt(0) == 0) m_memberItems.Clear();
            }
        }
    }

    // ------------------------------------------------------------------ typing

    void MainWindow::OnTypingStart(JsonObject const& d)
    {
        auto userId = Json::Str(d, L"user_id");
        if (Json::Str(d, L"channel_id") != m_currentChannelId || userId == m_selfId)
        {
            return;
        }
        std::wstring name;
        if (auto member = Json::Obj(d, L"member"))
        {
            name = Json::Str(member, L"nick");
            if (name.empty()) name = UserDisplayName(Json::Obj(member, L"user"));
        }
        if (name.empty())
        {
            auto it = m_users.find(userId);
            name = it != m_users.end() ? it->second.name : L"Quelqu'un";
        }
        m_typing[userId] = { NowMs() + 10000, name };
        UpdateTypingText();
        m_typingTimer.Start();
    }

    void MainWindow::UpdateTypingText()
    {
        auto now = NowMs();
        std::erase_if(m_typing, [now](auto const& entry) { return entry.second.first < now; });

        std::vector<std::wstring const*> names;
        for (auto const& [id, entry] : m_typing) names.push_back(&entry.second);

        if (names.empty())
        {
            TypingText().Text(L"");
            m_typingTimer.Stop();
        }
        else if (names.size() == 1)
        {
            TypingText().Text(*names[0] + L" est en train d'écrire…");
        }
        else if (names.size() == 2)
        {
            TypingText().Text(*names[0] + L" et " + *names[1] + L" sont en train d'écrire…");
        }
        else
        {
            TypingText().Text(L"Plusieurs personnes sont en train d'écrire…");
        }
    }

    void MainWindow::OnComposerTextChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        // Let others see "X est en train d'écrire…" (Discord expects one ping per ~8 s).
        auto now = NowMs();
        if (!m_rest || m_currentChannelId.empty() || Composer().Text().empty() || now - m_lastTypingSent < 8000)
        {
            return;
        }
        m_lastTypingSent = now;
        m_rest->PostJson(L"/channels/" + m_currentChannelId + L"/typing", JsonObject{});
    }

    // ------------------------------------------------------------------ uploads

    fire_and_forget MainWindow::OnAttach(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        Microsoft::Windows::Storage::Pickers::FileOpenPicker picker{ AppWindow().Id() };
        picker.FileTypeFilter().Append(L"*");
        auto result = co_await picker.PickSingleFileAsync();
        if (result)
        {
            UploadFile(std::wstring{ result.Path() });
        }
    }

    fire_and_forget MainWindow::UploadFile(std::wstring path)
    {
        using namespace Windows::Web::Http;
        auto strong = get_strong();
        auto rest = m_rest;
        auto channelId = m_currentChannelId;
        if (!rest || channelId.empty())
        {
            co_return;
        }

        hstring error;
        try
        {
            auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(path);
            auto props = co_await file.GetBasicPropertiesAsync();
            co_await wil::resume_foreground(m_dispatcher);
            if (props.Size() > 10ull * 1024 * 1024)
            {
                StatusText().Text(L"Fichier trop lourd (10 Mo max sans Nitro).");
                co_return;
            }
            StatusText().Text(L"Envoi de " + std::wstring{ file.Name() } + L"…");

            auto text = std::wstring{ Composer().Text() };
            Composer().Text(L"");

            JsonObject attachment;
            attachment.Insert(L"id", JsonValue::CreateStringValue(L"0"));
            attachment.Insert(L"filename", JsonValue::CreateStringValue(file.Name()));
            JsonArray attachments;
            attachments.Append(attachment);
            JsonObject payload;
            payload.Insert(L"content", JsonValue::CreateStringValue(text));
            payload.Insert(L"nonce", JsonValue::CreateStringValue(NowNonce()));
            payload.Insert(L"attachments", attachments);

            HttpMultipartFormDataContent form;
            form.Add(HttpStringContent{ payload.Stringify(), Windows::Storage::Streams::UnicodeEncoding::Utf8, L"application/json" },
                     L"payload_json");
            HttpStreamContent content{ co_await file.OpenReadAsync() };
            auto type = file.ContentType().empty() ? hstring{ L"application/octet-stream" } : file.ContentType();
            content.Headers().ContentType(Headers::HttpMediaTypeHeaderValue{ type });
            form.Add(content, L"files[0]", file.Name());

            co_await rest->PostContent(L"/channels/" + channelId + L"/messages", form);
            co_await wil::resume_foreground(m_dispatcher);
            StatusText().Text(L"");
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
