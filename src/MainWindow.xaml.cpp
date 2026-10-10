#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "Discord/Json.h"
#include "ImageCache.h"
#include "Theme.h"
#include "TokenStore.h"
#include "Strings.h"
#include "MemLog.h"
#include "Voice/Sounds.h"
#include "third_party/qrcodegen.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
namespace Json = ::DiscordWin3::Json;
namespace Discord = ::DiscordWin3::Discord;
using ::DiscordWin3::MessageData;
namespace I18n = ::DiscordWin3::I18n;
namespace Slim = ::DiscordWin3::Slim;
namespace Voice = ::DiscordWin3::Voice;

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

        template <typename O>
        std::wstring UserDisplayName(O const& user)
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

        template <typename O>
        std::wstring AvatarUrl(O const& user)
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
                swprintf_s(buf, L"%02d:%02d", local.wHour, local.wMinute); return buf;   // today: time only (2025 client)
            }
            else if (delta == 1)
            {
                swprintf_s(buf, L"%02d:%02d", local.wHour, local.wMinute); return I18n::Fmt(I18n::S::YesterdayAt, buf);
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
            // "10 octobre 2026" / "October 10, 2026": long date without the weekday, like the separators of the official client.
            auto locale = I18n::LocaleName(I18n::Current());
            std::wstring_view name{ locale };
            wchar_t const* format = name.starts_with(L"en") ? L"MMMM d, yyyy" : name.starts_with(L"de") ? L"d. MMMM yyyy"
                                  : name.starts_with(L"pt") || name.starts_with(L"es") ? L"d 'de' MMMM 'de' yyyy" : L"d MMMM yyyy";
            GetDateFormatEx(locale, 0, &local, format, buf, 96, nullptr);
            return buf;
        }

        std::wstring StatusLabel(std::wstring const& status)
        {
            if (status == L"idle") return I18n::Tr(I18n::S::StatusIdle);
            if (status == L"dnd") return I18n::Tr(I18n::S::StatusDnd);
            if (status == L"invisible" || status == L"offline") return I18n::Tr(I18n::S::StatusInvisible);
            return I18n::Tr(I18n::S::StatusOnline);
        }

        uint32_t StatusColor(std::wstring const& status)
        {
            if (status == L"idle") return 0xF0B232;
            if (status == L"dnd") return 0xF23F43;
            if (status == L"invisible" || status == L"offline") return 0x80848E;
            return 0x23A55A;
        }

        // Text for one activity ("Joue à X", custom status...).
        std::wstring ActivityLine(Slim::Value const& o)
        {
            auto name = Json::Str(o, L"name");
            switch (static_cast<int>(Json::Num(o, L"type", -1)))
            {
            case 4: return Json::Str(o, L"state");
            case 0: return I18n::Fmt(I18n::S::Playing, name);
            case 1: return I18n::Fmt(I18n::S::Streaming, name);
            case 2: return I18n::Fmt(I18n::S::Listening, name == L"Spotify" ? Json::Str(o, L"details") : name);
            case 3: return I18n::Fmt(I18n::S::Watching, name);
            case 5: return I18n::Fmt(I18n::S::Competing, name);
            default: return {};
            }
        }

        // Custom status wins, otherwise the first activity.
        std::wstring ActivityText(Slim::Value const& activities)
        {
            std::wstring fallback;
            for (auto o : activities)
            {
                if (!o.IsObject()) continue;
                auto line = ActivityLine(o);
                if (Json::Num(o, L"type", -1) == 4)
                {
                    if (!line.empty()) return line;
                    continue;
                }
                if (fallback.empty()) fallback = line;
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

        template <typename O>
        std::vector<uint64_t> RoleIds(O const& member)
        {
            std::vector<uint64_t> roles;
            if (auto array = Json::Arr(member, L"roles"))
            {
                for (auto const& id : array)
                {
                    if (Json::IsString(id))
                    {
                        roles.push_back(Json::U64(Json::AsString(id)));
                    }
                }
            }
            return roles;
        }
    }

    // ------------------------------------------------------------------ setup

    void MainWindow::InitializeComponent()
    {
        ::DiscordWin3::MemLog(L"startup");
        MainWindowT::InitializeComponent();
        I18n::Initialize();
        ApplyTexts();

        m_dispatcher = DispatcherQueue();
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppWindow().Resize({ 1280, 800 });
        AppWindow().Title(L"Discord Win3");
        // Taskbar / Alt+Tab icon straight from the embedded resource (no loose file).
        if (auto icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1)))
        {
            AppWindow().SetIcon(Microsoft::UI::GetIconIdFromIcon(icon));
        }

        GuildList().ItemsSource(m_guildItems);
        ChannelList().ItemsSource(m_channelItems);
        MessageList().ItemsSource(m_messageItems);
        MemberList().ItemsSource(m_memberItems);
        FriendsList().ItemsSource(m_friendItems);

        // Keyboard: Ctrl+K quick switcher, Alt+Left / Alt+Right history.
        auto accelerator = [this](Windows::System::VirtualKey key, Windows::System::VirtualKeyModifiers modifiers, auto handler)
        {
            Input::KeyboardAccelerator a;
            a.Key(key);
            a.Modifiers(modifiers);
            a.Invoked([weak = get_weak(), handler](auto&&, Input::KeyboardAcceleratorInvokedEventArgs const& e)
            {
                if (auto self = weak.get()) { e.Handled(true); handler(self.get()); }
            });
            Content().as<UIElement>().KeyboardAccelerators().Append(a);
        };
        using Windows::System::VirtualKey;
        using Windows::System::VirtualKeyModifiers;
        accelerator(VirtualKey::K, VirtualKeyModifiers::Control, [](MainWindow* w) { w->OnQuickSwitch(nullptr, nullptr); });
        accelerator(VirtualKey::Left, VirtualKeyModifiers::Menu, [](MainWindow* w) { w->OnNavigateBack(nullptr, nullptr); });
        accelerator(VirtualKey::Right, VirtualKeyModifiers::Menu, [](MainWindow* w) { w->OnNavigateForward(nullptr, nullptr); });
        NavButtons().SizeChanged([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->UpdateTitleBarRegions(); });

        m_statsTimer = m_dispatcher.CreateTimer();
        m_statsTimer.Interval(std::chrono::seconds(1));
        m_statsTimer.Tick([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->UpdateStatsText(); });
        CallGrid().ItemsSource(m_callItems);

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

        Activated([weak = get_weak()](IInspectable const&, WindowActivatedEventArgs const& e)
        {
            auto self = weak.get();
            if (!self) return;
            self->m_windowActive = e.WindowActivationState() != WindowActivationState::Deactivated;
            if (!self->m_windowActive)
            {
                // 30 s in the background without coming back: give the working set back to Windows.
                auto generation = ++self->m_idleGeneration;
                [](weak_ref<MainWindow> weak, uint64_t generation) -> fire_and_forget
                {
                    co_await resume_after(std::chrono::seconds(30));
                    auto w = weak.get();
                    if (!w) co_return;
                    co_await wil::resume_foreground(w->m_dispatcher);
                    if (w->m_windowActive || w->m_idleGeneration != generation) co_return;
                    HeapCompact(GetProcessHeap(), 0);
                    SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0);
                    ::DiscordWin3::MemLog(L"idle trim");
                }(self->get_weak(), generation);
            }
            // Coming back to the window = the open channel is now read.
            if (self->m_windowActive && !self->m_currentChannelId.empty() && self->m_messageItems.Size() > 0)
            {
                self->Ack(self->m_currentChannelId, Impl(self->m_messageItems.GetAt(self->m_messageItems.Size() - 1))->Data().id);
            }
        });

        // Energy Saver / Windows "Animation effects" -> follow them live.
        m_uiSettings = Windows::UI::ViewManagement::UISettings{};
        ApplyPowerPolicy();
        try
        {
            Microsoft::Windows::System::Power::PowerManager::EnergySaverStatusChanged([weak = get_weak()](auto&&, auto&&)
            {
                if (auto self = weak.get())
                    self->m_dispatcher.TryEnqueue([weak]() { if (auto w = weak.get()) w->ApplyPowerPolicy(); });
            });
        }
        catch (...)
        {
        }
        m_uiSettings.AnimationsEnabledChanged([weak = get_weak()](auto&&, auto&&)
        {
            if (auto self = weak.get())
                self->m_dispatcher.TryEnqueue([weak]() { if (auto w = weak.get()) w->ApplyPowerPolicy(); });
        });

        m_messageMenu = MenuFlyout{};
        m_messageMenu.Opening([weak = get_weak()](IInspectable const& sender, IInspectable const&)
        {
            if (auto self = weak.get()) self->OnMessageMenuOpening(sender);
        });
        InitNotifications();

        Closed([weak = get_weak()](IInspectable const&, WindowEventArgs const&)
        {
            if (auto self = weak.get())
            {
                if (self->m_gateway) self->m_gateway->Stop();
                if (self->m_remoteAuth) self->m_remoteAuth->Stop();
                if (self->m_voice) self->m_voice->Stop();
                self->StopScreenShare(true);
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
        power.StateMask = (background || m_energySaver) ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &power, sizeof(power));

        if (background)
        {
            ::DiscordWin3::ImageCache::Clear();
            HeapCompact(GetProcessHeap(), 0);
            SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0);
        }
    }

    void MainWindow::ApplyPowerPolicy()
    {
        using namespace Microsoft::Windows::System::Power;
        try
        {
            m_energySaver = PowerManager::EnergySaverStatus() == EnergySaverStatus::On;
        }
        catch (...)
        {
            m_energySaver = false;
        }
        bool animationsOn = m_uiSettings ? m_uiSettings.AnimationsEnabled() : true;
        m_reduceMotion = m_energySaver || !animationsOn;

        // Implicit transitions are composition animations: drop them entirely when not wanted.
        if (m_reduceMotion)
        {
            ChatRoot().OpacityTransition(nullptr);
            LoginRoot().OpacityTransition(nullptr);
            MessageList().OpacityTransition(nullptr);
            MessageList().TranslationTransition(nullptr);
            QrImage().OpacityTransition(nullptr);
        }
        else
        {
            auto fade = [](int ms) { ScalarTransition t; t.Duration(std::chrono::milliseconds(ms)); return t; };
            ChatRoot().OpacityTransition(fade(250));
            LoginRoot().OpacityTransition(fade(200));
            MessageList().OpacityTransition(fade(180));
            Vector3Transition slide;
            slide.Duration(std::chrono::milliseconds(220));
            MessageList().TranslationTransition(slide);
            QrImage().OpacityTransition(fade(300));
        }

        // EcoQoS also in the foreground while Energy Saver is on.
        PROCESS_POWER_THROTTLING_STATE power{};
        power.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        power.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        power.StateMask = (m_background || m_energySaver) ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &power, sizeof(power));
    }

    // ------------------------------------------------------------------ animations

    void MainWindow::UpdateGuildPill(FrameworkElement const& iconHost, bool selected, bool hover)
    {
        // Rail-edge pill like the official client: 8px unread, 20px hover, 40px selected.
        auto pill = iconHost.FindName(L"Pill").try_as<FrameworkElement>();
        if (!pill) return;
        auto item = iconHost.DataContext().try_as<DiscordWin3::GuildItem>();
        bool unread = item && item.UnreadVisibility() == Visibility::Visible;
        pill.Height(selected ? 40 : hover ? 20 : 8);
        pill.Visibility(Show(selected || hover || unread));
    }

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
        if (!animate || m_reduceMotion)
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
        auto templateRoot = container.ContentTemplateRoot().try_as<FrameworkElement>();
        auto root = templateRoot ? templateRoot.FindName(L"IconHost").try_as<FrameworkElement>() : nullptr;
        if (!root)
        {
            return;
        }

        bool fresh = !root.Tag();
        MorphGuild(root, container.IsSelected(), false);
        UpdateGuildPill(root, container.IsSelected(), false);
        if (!fresh)
        {
            return;
        }

        // Hover: circle -> rounded square, exactly like Discord's server rail.
        weak_ref<Primitives::SelectorItem> weakContainer{ container };
        weak_ref<FrameworkElement> weakRoot{ root };
        auto weakSelf = get_weak();
        root.PointerEntered([weakSelf, weakRoot, weakContainer](IInspectable const&, Input::PointerRoutedEventArgs const&)
        {
            auto self = weakSelf.get();
            if (auto r = weakRoot.get(); self && r)
            {
                auto c = weakContainer.get();
                self->MorphGuild(r, true, true);
                self->UpdateGuildPill(r, c && c.IsSelected(), true);
            }
        });
        root.PointerExited([weakSelf, weakRoot, weakContainer](IInspectable const&, Input::PointerRoutedEventArgs const&)
        {
            auto self = weakSelf.get();
            auto r = weakRoot.get();
            auto c = weakContainer.get();
            if (self && r && c)
            {
                self->MorphGuild(r, c.IsSelected(), true);
                self->UpdateGuildPill(r, c.IsSelected(), false);
            }
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
            auto templateRoot = container.ContentTemplateRoot().try_as<FrameworkElement>();
            if (auto root = templateRoot ? templateRoot.FindName(L"IconHost").try_as<UIElement>() : nullptr)
            {
                MorphGuild(root, container.IsSelected(), true);
                UpdateGuildPill(root.as<FrameworkElement>(), container.IsSelected(), false);
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
        QrTitle().Text(I18n::Tr(I18n::S::QrTitle));
        QrHint().Text(I18n::Tr(I18n::S::QrHint));

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
                w->QrTitle().Text(I18n::Tr(I18n::S::QrCheckPhone));
                w->QrHint().Text(I18n::Fmt(I18n::S::QrConfirmAs, user));
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
                w->QrTitle().Text(I18n::Tr(I18n::S::QrExpired));
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
            LoginError().Text(I18n::Tr(I18n::S::LoginNeedToken));
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
        GuildTitle().Text(I18n::Tr(I18n::S::Loading));

        auto weak = get_weak();
        auto dq = m_dispatcher;
        m_gateway = std::make_shared<Discord::Gateway>(
            m_token,
            [weak, dq](Discord::DispatchEvent const& event)
            {
                dq.TryEnqueue([weak, event]()
                {
                    if (auto self = weak.get())
                    {
                        self->OnDispatch(event);
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
        LeaveVoice();
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
        m_presence.clear();
        m_relationships.clear();
        m_userTags.clear();
        m_friendItems.Clear();
        m_history.clear();
        m_historyIndex = 0;
        m_showingFriends = false;
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
        case Discord::GatewayStatus::Connecting: StatusText().Text(I18n::Tr(I18n::S::Connecting)); break;
        case Discord::GatewayStatus::Reconnecting: StatusText().Text(I18n::Tr(I18n::S::Reconnecting)); break;
        case Discord::GatewayStatus::Connected: StatusText().Text(L""); break;
        case Discord::GatewayStatus::AuthFailed:
            m_saveTokenOnReady = false;
            ::DiscordWin3::TokenStore::Clear();
            EndSession();
            ShowLogin(I18n::Tr(I18n::S::LoginInvalidToken));
            break;
        }
    }

    void MainWindow::OnDispatch(Discord::DispatchEvent const& e)
    {
        auto const& type = e.type;

        // Message-related events: small payloads, handled through Windows.Data.Json.
        if (e.json)
        {
            auto const& d = e.json;
            if (type == L"MESSAGE_CREATE")
            {
                auto channelId = Json::Str(d, L"channel_id");
                std::optional<MessageData> data;
                if (channelId == m_currentChannelId)
                {
                    if (m_typing.erase(Json::Str(Json::Obj(d, L"author"), L"id")))
                    {
                        UpdateTypingText();
                    }
                    data = BuildMessage(d);
                    AppendMessage(*data);
                }
                OnMessageForUnread(d, data ? &*data : nullptr);
            }
            else if (type == L"MESSAGE_ACK")
            {
                OnRemoteAck(d);
            }
            else if (type.starts_with(L"MESSAGE_REACTION_"))
            {
                OnReactionEvent(type, d);
            }
            else if (type == L"TYPING_START")
            {
                OnTypingStart(d);
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
            return;
        }

        // Everything else is read straight from the compact DOM.
        auto d = e.d;
        if (type == L"READY")
        {
            ::DiscordWin3::MemLog(L"ui: READY received");
            HandleReady(d);
            ::DiscordWin3::MemLog(L"ui: READY handled");
            [](weak_ref<MainWindow> weak) -> fire_and_forget
            {
                for (int s : { 5, 20, 60 })
                {
                    co_await resume_after(std::chrono::seconds(s == 5 ? 5 : s == 20 ? 15 : 40));
                    if (!weak.get()) co_return;
                    ::DiscordWin3::MemLog(s == 5 ? L"settled +5s" : s == 20 ? L"settled +20s" : L"settled +60s");
                }
            }(get_weak());
        }
        else if (type == L"PRESENCE_UPDATE")
        {
            OnPresenceUpdate(d);
        }
        else if (type.starts_with(L"RELATIONSHIP_"))
        {
            OnRelationshipEvent(type, d);
        }
        else if (type == L"GUILD_MEMBER_LIST_UPDATE")
        {
            OnMemberListUpdate(d);
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
            if (Json::Str(d, L"user_id") == m_selfId) OnOwnVoiceState(d);
            auto guild = FindGuild(Json::Str(d, L"guild_id"));
            if (!guild)
            {
                // DM / group call: track participants of our call only.
                auto userId = Json::Str(d, L"user_id");
                m_voiceFlags[userId] = { Json::Bool(d, L"self_mute") || Json::Bool(d, L"mute"), Json::Bool(d, L"self_deaf") || Json::Bool(d, L"deaf") };
                bool wasInCall = m_dmCallUsers.contains(userId);
                if (!m_voiceChannel.empty() && Json::Str(d, L"channel_id") == m_voiceChannel) m_dmCallUsers.insert(userId);
                else m_dmCallUsers.erase(userId);
                if (userId != m_selfId && wasInCall != m_dmCallUsers.contains(userId) && m_voiceConnected && !m_selfDeaf)
                    Voice::Play(wasInCall ? Voice::Sound::UserLeave : Voice::Sound::UserJoin);
                RefreshCallParticipants();
                return;
            }
            // Someone entering / leaving our channel: chime, like the official client.
            auto voiceUser = Json::Str(d, L"user_id");
            auto before = guild->voice.find(voiceUser);
            bool wasHere = before != guild->voice.end() && before->second == m_voiceChannel;
            ParseVoiceState(*guild, d);
            auto after = guild->voice.find(voiceUser);
            bool isHere = after != guild->voice.end() && after->second == m_voiceChannel;
            if (!m_voiceChannel.empty() && voiceUser != m_selfId && wasHere != isHere && m_voiceConnected && !m_selfDeaf)
                Voice::Play(isHere ? Voice::Sound::UserJoin : Voice::Sound::UserLeave);
            if (guild->id == m_voiceGuild) RefreshCallParticipants();
            if (guild->id == m_currentGuildId)
            {
                RefreshChannelList();
            }
        }
        else if (type == L"STREAM_CREATE" || type == L"STREAM_SERVER_UPDATE" || type == L"STREAM_DELETE")
        {
            OnStreamEvent(type, d);
        }
        else if (type == L"VOICE_SERVER_UPDATE")
        {
            OnVoiceServerUpdate(d);
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
                if (!guildId.empty()) m_channelGuild[channel.id] = guildId;
                list->push_back(std::move(channel));
            }
            if ((guildId.empty() ? std::wstring{ HomeId } : guildId) == m_currentGuildId)
            {
                RefreshChannelList();
            }
        }
    }

    // ------------------------------------------------------------------ model

    // Both JSON flavors: gateway data arrives as Slim::Value, REST messages as JsonObject.
    template <typename O>
    UserInfo const& MainWindow::CacheUserT(O const& user)
    {
        auto& info = m_users[Json::Str(user, L"id")];
        info.name = UserDisplayName(user);
        info.avatarUrl = AvatarUrl(user);
        auto tagged = Json::Obj(user, L"primary_guild");
        if (!tagged) tagged = Json::Obj(user, L"clan");
        if (tagged && Json::Bool(tagged, L"identity_enabled", true) && !Json::Str(tagged, L"tag").empty())
            m_userTags[Json::Str(user, L"id")] = Json::Str(tagged, L"tag");
        return info;
    }

    UserInfo const& MainWindow::CacheUser(JsonObject const& user) { return CacheUserT(user); }
    UserInfo const& MainWindow::CacheUser(Slim::Value const& user) { return CacheUserT(user); }

    template <typename O>
    void MainWindow::CacheMemberT(GuildInfo const& guild, O const& member)
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

    void MainWindow::CacheMember(GuildInfo const& guild, JsonObject const& member) { CacheMemberT(guild, member); }
    void MainWindow::CacheMember(GuildInfo const& guild, Slim::Value const& member) { CacheMemberT(guild, member); }

    ChannelInfo MainWindow::ParseChannel(Slim::Value const& c)
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

    void MainWindow::ParseDmChannel(Slim::Value const& c, ChannelInfo& info)
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
            for (auto r : recipients)
            {
                if (!r.IsObject()) continue;
                append(CacheUser(r));
                if (info.recipientId.empty()) info.recipientId = Json::Str(r, L"id");
            }
        }
        else if (auto ids = Json::Arr(c, L"recipient_ids"))
        {
            for (auto r : ids)
            {
                if (!r.IsString()) continue;
                auto id = r.Str();
                if (info.recipientId.empty()) info.recipientId = id;
                auto it = m_users.find(id);
                if (it != m_users.end()) append(it->second);
            }
        }

        auto name = Json::Str(c, L"name");
        info.name = !name.empty() ? name : (joined.empty() ? std::wstring{ I18n::Tr(I18n::S::UnnamedGroup) } : joined);

        auto icon = Json::Str(c, L"icon");
        if (info.type == 3)
        {
            info.recipientId.clear();   // group DM: no single presence dot
            info.avatarUrl = icon.empty() ? L""
                : std::wstring{ Discord::CdnBase } + L"/channel-icons/" + info.id + L"/" + icon + L".png?size=64";
        }
        else
        {
            info.avatarUrl = firstAvatar;
        }
    }

    void MainWindow::ParseVoiceState(GuildInfo& guild, Slim::Value const& state)
    {
        if (!state.IsObject())
        {
            return;
        }
        auto userId = Json::Str(state, L"user_id");
        m_voiceFlags[userId] = { Json::Bool(state, L"self_mute") || Json::Bool(state, L"mute"),
                                 Json::Bool(state, L"self_deaf") || Json::Bool(state, L"deaf") };
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

    GuildInfo MainWindow::ParseGuild(Slim::Value const& g)
    {
        // Newer READY payloads nest name/icon/owner under "properties".
        auto props = Json::Obj(g, L"properties");
        auto meta = props ? props : g;

        GuildInfo guild;
        guild.id = Json::Str(g, L"id");
        guild.name = Json::Str(meta, L"name");
        guild.icon = Json::Str(meta, L"icon");

        guild.perms.guildId = Json::U64(guild.id);
        guild.perms.selfId = Json::U64(m_selfId);
        guild.perms.ownerId = Json::U64(meta, L"owner_id");

        for (auto role : Json::Arr(g, L"roles"))
        {
            if (!role.IsObject()) continue;
            auto roleId = Json::Str(role, L"id");
            auto id = Json::U64(roleId);
            guild.perms.rolePermissions[id] = Json::U64(role, L"permissions");
            guild.roles[id] = { static_cast<int>(Json::Num(role, L"position")),
                                static_cast<uint32_t>(Json::Num(role, L"color")) };
            m_roleNames[roleId] = Json::Str(role, L"name");
        }

        for (auto member : Json::Arr(g, L"members"))
        {
            if (!member.IsObject()) continue;
            CacheMember(guild, member);
            auto user = Json::Obj(member, L"user");
            auto userId = user ? Json::Str(user, L"id") : Json::Str(member, L"user_id");
            if (userId == m_selfId)
            {
                guild.perms.known = true;
                guild.perms.selfRoles = RoleIds(member);
            }
        }

        for (auto c : Json::Arr(g, L"channels"))
        {
            if (!c.IsObject()) continue;
            auto info = ParseChannel(c);
            m_channelNames[info.id] = info.name;
            m_channelGuild[info.id] = guild.id;
            guild.channels.push_back(std::move(info));
        }
        guild.channels.shrink_to_fit();

        for (auto e : Json::Arr(g, L"emojis"))
        {
            if (e.IsObject() && Json::Bool(e, L"available", true))
                guild.emojis.push_back({ Json::Str(e, L"id"), Json::Str(e, L"name") });
        }
        guild.emojis.shrink_to_fit();

        for (auto state : Json::Arr(g, L"voice_states"))
        {
            ParseVoiceState(guild, state);
        }
        return guild;
    }

    void MainWindow::HandleReady(Slim::Value const& d)
    {
        if (m_saveTokenOnReady)
        {
            ::DiscordWin3::TokenStore::Save(m_token);
            m_saveTokenOnReady = false;
        }

        // Users first: with DEDUPE_USER_OBJECTS everything else refers to them by id.
        for (auto u : Json::Arr(d, L"users"))
        {
            if (u.IsObject()) CacheUser(u);
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
        if (auto sessions = Json::Arr(d, L"sessions"))
        {
            auto first = sessions.At(0);
            if (first.IsObject())
            {
                if (auto s = Json::Str(first, L"status"); !s.empty()) status = s;
            }
        }
        SelfStatus().Fill(SolidBrush(StatusColor(status)).as<Media::SolidColorBrush>());
        SelfStatusText().Text(customStatus.empty() ? StatusLabel(status) : customStatus);

        m_guilds.clear();
        // merged_members[i] holds our own member object for guilds[i]: walk both lists together.
        auto mergedMembers = Json::Arr(d, L"merged_members");
        auto merged = mergedMembers.begin();
        for (auto value : Json::Arr(d, L"guilds"))
        {
            Slim::Value members = merged != mergedMembers.end() ? *merged : Slim::Value{};
            if (merged != mergedMembers.end()) ++merged;
            if (!value.IsObject()) continue;
            auto guild = ParseGuild(value);

            for (auto member : members)
            {
                if (!member.IsObject()) continue;
                CacheMember(guild, member);
                if (Json::Str(member, L"user_id") == m_selfId && !guild.perms.known)
                {
                    guild.perms.known = true;
                    guild.perms.selfRoles = RoleIds(member);
                }
            }
            if (!guild.name.empty())
            {
                m_guilds.push_back(std::move(guild));
            }
        }
        m_guilds.shrink_to_fit();

        // Respect the user's folder order when available.
        if (auto settings = Json::Obj(d, L"user_settings"))
        {
            std::vector<std::wstring> order;
            for (auto folder : Json::Arr(settings, L"guild_folders"))
            {
                for (auto id : Json::Arr(folder, L"guild_ids"))
                {
                    order.push_back(id.Str());   // string or number token: same digits
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
        for (auto c : Json::Arr(d, L"private_channels"))
        {
            if (!c.IsObject()) continue;
            auto info = ParseChannel(c);
            ParseDmChannel(c, info);
            m_channelNames[info.id] = info.name;
            m_dms.push_back(std::move(info));
        }

        ParseRelationships(d);
        ParseReadStates(d);
        ParseGuildSettings(Json::Get(d, L"user_guild_settings"));
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
        auto [homeUnread, homeMentions] = HomeBadge();
        items.push_back(make<GuildItem>(HomeId, I18n::Tr(I18n::S::DirectMessages), L"", false, homeMentions));
        for (auto const& g : m_guilds)
        {
            items.push_back(MakeGuildItem(g));
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
            GuildTitle().Text(I18n::Tr(I18n::S::DirectMessages));
            std::vector<ChannelInfo const*> dms;
            dms.reserve(m_dms.size());
            for (auto const& c : m_dms) dms.push_back(&c);
            std::sort(dms.begin(), dms.end(), [](ChannelInfo const* a, ChannelInfo const* b)
            {
                return Json::SnowflakeLess(b->lastMessageId, a->lastMessageId);
            });
            for (auto c : dms)
            {
                auto item = make_self<ChannelItem>(hstring{ c->id }, hstring{ c->name }, hstring{ Glyph(c->type) },
                                                   ChannelKind::Text, c->avatarUrl, IsUnread(*c), MentionsIn(c->id));
                if (!c->recipientId.empty()) item->SetPresence(PresenceColor(c->recipientId), PresenceText(c->recipientId, false));
                items.push_back(item.as<IInspectable>());
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
                    bool text = IsTextLike(c->type);
                    items.push_back(make<ChannelItem>(hstring{ c->id }, hstring{ c->name }, hstring{ Glyph(c->type) },
                                                      text ? ChannelKind::Text : ChannelKind::Voice, std::wstring{},
                                                      text && IsUnread(*c), MentionsIn(c->id)));
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
                        auto voiceItem = make_self<ChannelItem>(hstring{ L"voice:" + userId }, hstring{ name }, L"",
                                                                ChannelKind::VoiceUser, avatar);
                        voiceItem->SetSpeaking(m_speakingUsers.contains(userId));
                        items.push_back(voiceItem.as<IInspectable>());
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
        ::DiscordWin3::MemLog(L"guild: select");
        m_currentGuildId = item.Id();
        m_memberItems.Clear();
        m_memberListGuild.clear();
        MembersPane().Visibility(Show(MembersToggle().IsChecked().Value() && m_currentGuildId != HomeId));
        UpdateHomeChrome();
        RefreshChannelList();
        UpdateTitle();
        ::DiscordWin3::MemLog(L"guild: channels built", m_channelItems.Size());

        // Home opens on the friends page, like the official client.
        if (m_currentGuildId == HomeId)
        {
            ShowFriends(true);
            return;
        }
        ShowFriends(false);

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
                if (std::wstring{ item.Id() } == m_voiceChannel) ShowCallView(true);   // already in: show the call screen
                else JoinVoice(m_currentGuildId, std::wstring{ item.Id() });         // voice / stage channel: join the call
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
        CallButton().Visibility(Show(m_currentGuildId == HomeId));
        ShowCallView(false);
        ShowFriends(false);
        LoadChannel(std::wstring{ item.Id() }, std::wstring{ item.Name() });
        Composer().PlaceholderText(I18n::Fmt(m_currentGuildId == HomeId ? I18n::S::SendMessageTo : I18n::S::SendMessageIn,
                                               std::wstring{ item.Name() }));
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
            TitleText().Text(name.empty() ? hstring{ I18n::Tr(I18n::S::DirectMessages) } : name);
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

    void MainWindow::OnMembersChunk(Slim::Value const& d)
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
            if (!m.IsObject()) continue;
            auto member = m;
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
        PushHistory();
        m_hasMoreOlder = false;
        m_loadingOlder = false;
        ChannelTitle().Text(title);
        Composer().IsEnabled(true);
        AttachButton().IsEnabled(true);
        StatusText().Text(L"");
        m_typing.clear();
        ClearComposerMode();
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
            ::DiscordWin3::MemLog(L"channel loaded", array.Size());
            m_hasMoreOlder = array.Size() == 50;
            RequestMissingMembers();
            if (array.Size() > 0) Ack(id, Json::Str(array.GetAt(0).GetObject(), L"id"));
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
                message.starts_with(L"HTTP 403") ? std::wstring{ I18n::Tr(I18n::S::NoAccess) } : I18n::Fmt(I18n::S::LoadError, message) });
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
        using Windows::System::VirtualKey;
        if (e.Key() == VirtualKey::Escape && (!m_replyToId.empty() || !m_editingId.empty()))
        {
            bool wasEditing = !m_editingId.empty();
            ClearComposerMode();
            if (wasEditing) Composer().Text(L"");
            e.Handled(true);
            return;
        }
        if (e.Key() == VirtualKey::Up && Composer().Text().empty())
        {
            // Like Discord: ↑ in an empty composer edits your last message.
            e.Handled(EditLastOwnMessage());
            return;
        }
        if (e.Key() != VirtualKey::Enter)
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
        if (!text.empty() || !m_pending.empty())
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

        auto editingId = m_editingId;
        auto replyToId = m_replyToId;
        ClearComposerMode();
        if (!m_pending.empty() && editingId.empty())
        {
            SendWithAttachments(text, replyToId);
            co_return;
        }

        JsonObject body;
        body.Insert(L"content", JsonValue::CreateStringValue(text));
        hstring error;
        try
        {
            if (!editingId.empty())
            {
                co_await rest->Call(Windows::Web::Http::HttpMethod::Patch(),
                                    L"/channels/" + channelId + L"/messages/" + editingId, body);
                co_return;
            }

            body.Insert(L"nonce", JsonValue::CreateStringValue(NowNonce()));
            body.Insert(L"tts", JsonValue::CreateBooleanValue(false));
            body.Insert(L"flags", JsonValue::CreateNumberValue(0));
            if (!replyToId.empty())
            {
                JsonObject reference;
                reference.Insert(L"message_id", JsonValue::CreateStringValue(replyToId));
                reference.Insert(L"channel_id", JsonValue::CreateStringValue(channelId));
                body.Insert(L"message_reference", reference);
            }
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
            StatusText().Text(I18n::Fmt(I18n::S::SendFailed, message.substr(0, 80)));
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
                        push(Kind::Mention, L"@" + (it != m_roleNames.end() ? it->second : std::wstring{ I18n::Tr(I18n::S::RoleWord) }));
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
                        push(Kind::Mention, L"@" + (name.empty() ? std::wstring{ I18n::Tr(I18n::S::UnknownUser) } : name));
                    }
                    else if (tag.starts_with(L"#"))
                    {
                        auto it = m_channelNames.find(std::wstring{ tag.substr(1) });
                        flush();
                        push(Kind::Mention, L"#" + (it != m_channelNames.end() ? it->second : std::wstring{ I18n::Tr(I18n::S::UnknownChannel) }));
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
        // A video player never survives its row: stop and drop it (decoder + buffers are heavy).
        if (auto host = root.FindName(L"MediaHost").try_as<Grid>())
        {
            auto children = host.Children();
            for (int i = static_cast<int>(children.Size()) - 1; i >= 0; --i)
            {
                auto surface = children.GetAt(i).try_as<Grid>();
                if (surface && unbox_value_or<hstring>(surface.Tag(), L"") == L"video")
                {
                    if (auto player = surface.Children().GetAt(0).try_as<MediaPlayerElement>())
                    {
                        if (auto mp = player.MediaPlayer()) mp.Pause();
                        player.Source(nullptr);
                    }
                    children.RemoveAt(i);
                }
            }
        }
        if (args.InRecycleQueue())
        {
            body.Blocks().Clear();   // release inline images of rows scrolled away
            if (auto gallery = root.FindName(L"Gallery").try_as<Grid>()) gallery.Children().Clear();
            return;
        }
        RenderBody(body, Impl(args.Item())->Data());
        if (auto gallery = root.FindName(L"Gallery").try_as<Grid>())
        {
            RenderGallery(gallery, Impl(args.Item())->Data());
        }
        if (auto reactions = root.FindName(L"Reactions").try_as<StackPanel>())
        {
            RenderReactions(reactions, Impl(args.Item())->Data());
        }
        if (auto bar = root.FindName(L"HoverBar").try_as<UIElement>())
        {
            bar.Visibility(Visibility::Collapsed);   // recycled rows start hidden
        }
        if (!root.ContextFlyout())
        {
            // First time this row container is used: hover bar + tooltips (containers are recycled, so once is enough).
            weak_ref<FrameworkElement> weakRoot{ root };
            root.PointerEntered([weak = get_weak(), weakRoot](IInspectable const&, Input::PointerRoutedEventArgs const&)
            {
                auto self = weak.get();
                auto r = weakRoot.get();
                if (!self || !r) return;
                if (auto b = r.FindName(L"HoverBar").try_as<UIElement>()) b.Visibility(Visibility::Visible);
                for (int i = 0; i < 3; ++i)
                {
                    auto quick = r.FindName(L"Quick" + std::to_wstring(i)).try_as<Button>();
                    if (quick && i < static_cast<int>(self->m_recentEmojis.size())) quick.Content(box_value(self->m_recentEmojis[i]));
                }
            });
            root.PointerExited([weakRoot](IInspectable const&, Input::PointerRoutedEventArgs const&)
            {
                if (auto r = weakRoot.get())
                {
                    if (auto b = r.FindName(L"HoverBar").try_as<UIElement>()) b.Visibility(Visibility::Collapsed);
                }
            });
            auto tip = [&](wchar_t const* name, I18n::S key)
            {
                if (auto e = root.FindName(name).try_as<DependencyObject>()) ToolTipService::SetToolTip(e, box_value(I18n::Tr(key)));
            };
            tip(L"HoverPicker", I18n::S::AddReaction);
            tip(L"HoverEdit", I18n::S::Edit);
            tip(L"HoverReply", I18n::S::Reply);
            tip(L"HoverForward", I18n::S::ForwardAction);
            tip(L"HoverMore", I18n::S::More);
            root.ContextFlyout(m_messageMenu);
        }
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
        data.own = !data.authorId.empty() && data.authorId == m_selfId;
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

        data.rawContent = Json::Str(m, L"content");
        data.edited = Json::Get(m, L"edited_timestamp") != nullptr;
        if (auto reactions = Json::Arr(m, L"reactions"))
        {
            for (auto const& r : reactions)
            {
                if (r.ValueType() != JsonValueType::Object) continue;
                auto o = r.GetObject();
                auto emoji = Json::Obj(o, L"emoji");
                data.reactions.push_back({ Json::Str(emoji, L"name"), Json::Str(emoji, L"id"),
                                           static_cast<int>(Json::Num(o, L"count")), Json::Bool(o, L"me") });
            }
        }
        data.body = ParseBody(Json::Str(m, L"content"), m);

        // Forwarded message (2024): the content lives in message_snapshots[0].message.
        if (auto snapshots = Json::Arr(m, L"message_snapshots"); snapshots && snapshots.Size() > 0
            && snapshots.GetAt(0).ValueType() == JsonValueType::Object)
        {
            auto snapshot = Json::Obj(snapshots.GetAt(0).GetObject(), L"message");
            data.reply = I18n::Tr(I18n::S::Forwarded);
            data.body = ParseBody(Json::Str(snapshot, L"content"), snapshot);
            data.forceHeader = true;
            if (auto attachments = Json::Arr(snapshot, L"attachments")) m.Insert(L"attachments", attachments);
            if (auto embeds = Json::Arr(snapshot, L"embeds")) m.Insert(L"embeds", embeds);
        }
        auto system = [&](std::wstring text)
        {
            data.body = { { Kind::Text, std::move(text) } };
            data.forceHeader = true;
        };

        switch (static_cast<int>(Json::Num(m, L"type")))
        {
        case 7: system(I18n::Tr(I18n::S::SysJoined)); break;
        case 6: system(I18n::Tr(I18n::S::SysPinned)); break;
        case 8: case 9: case 10: case 11: system(I18n::Tr(I18n::S::SysBoost)); break;
        case 19:
            if (auto ref = Json::Obj(m, L"referenced_message"))
            {
                auto snippet = PlainText(Json::Str(ref, L"content"), ref);
                if (snippet.size() > 100) snippet = snippet.substr(0, 100) + L"…";
                for (auto& c : snippet) if (c == L'\n') c = L' ';
                auto refAuthor = Json::Obj(ref, L"author");
                auto refId = Json::Str(refAuthor, L"id");
                data.replyName = UserDisplayName(refAuthor);
                data.replyAvatarUrl = AvatarUrl(refAuthor);
                if (auto guild = m_members.find(m_currentGuildId); guild != m_members.end())
                {
                    if (auto member = guild->second.find(refId); member != guild->second.end())
                    {
                        if (!member->second.nick.empty()) data.replyName = member->second.nick;
                        if (!member->second.avatarUrl.empty()) data.replyAvatarUrl = member->second.avatarUrl;
                        data.replyColor = member->second.color;
                    }
                }
                auto refGuild = Json::Obj(refAuthor, L"primary_guild");
                if (refGuild && Json::Bool(refGuild, L"identity_enabled", true)) data.replyTag = Json::Str(refGuild, L"tag");
                if (snippet.empty())
                {
                    data.replyAttachmentOnly = true;
                    snippet = I18n::Tr(I18n::S::ClickToSeeAttachment);
                }
                data.replySnippet = std::move(snippet);
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
            // 2+ images: mosaic like the official client (all images, cropped tiles), never raw links.
            int imageCount = 0;
            for (auto const& a : attachments)
            {
                if (a.ValueType() == JsonValueType::Object && Json::Str(a.GetObject(), L"content_type").starts_with(L"image/")
                    && Json::Num(a.GetObject(), L"width") > 0)
                    ++imageCount;
            }
            for (auto const& a : attachments)
            {
                if (a.ValueType() != JsonValueType::Object) continue;
                auto o = a.GetObject();
                auto contentType = Json::Str(o, L"content_type");
                if (imageCount >= 2 && contentType.starts_with(L"image/") && Json::Num(o, L"width") > 0)
                {
                    double w = Json::Num(o, L"width"), h = Json::Num(o, L"height");
                    auto proxy = Json::Str(o, L"proxy_url");
                    double scale = std::min(1.0, 600.0 / std::max(w, h));
                    data.gallery.push_back({ proxy + (proxy.find(L'?') == std::wstring::npos ? L"?" : L"&")
                                                 + L"width=" + std::to_wstring(static_cast<int>(w * scale))
                                                 + L"&height=" + std::to_wstring(static_cast<int>(h * scale)),
                                             Json::Str(o, L"url"), w, h });
                    continue;
                }
                if (contentType.starts_with(L"image/") &&
                    setImage(Json::Str(o, L"proxy_url"), Json::Num(o, L"width"), Json::Num(o, L"height")))
                {
                    data.mediaUrl = Json::Str(o, L"url");
                    continue;
                }
                if (contentType.starts_with(L"video/") && data.imageUrl.empty())
                {
                    // Poster frame from the media proxy; the actual video only streams when played.
                    auto proxy = Json::Str(o, L"proxy_url");
                    auto poster = proxy + (proxy.find(L'?') == std::wstring::npos ? L"?" : L"&") + L"format=jpeg";
                    double w = Json::Num(o, L"width", 640), h = Json::Num(o, L"height", 360);
                    if (setImage(poster, w, h))
                    {
                        data.mediaUrl = Json::Str(o, L"url");
                        data.isVideo = true;
                        continue;
                    }
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
                    data.body.push_back({ Kind::Text, (data.body.empty() ? L"" : L"\n") + I18n::Fmt(I18n::S::StickerLabel, Json::Str(s.GetObject(), L"name")) });
            }
        }

        if (Json::Get(m, L"edited_timestamp") && !data.body.empty())
        {
            data.body.push_back({ Kind::Text, I18n::Tr(I18n::S::Edited) });
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
        ::DiscordWin3::MemLog(L"members: subscribe", m_memberItems.Size());
        m_gateway->SubscribeMemberList(m_currentGuildId, m_currentChannelId);
    }

    IInspectable MainWindow::BuildMemberRow(Slim::Value const& item, GuildInfo const& guild)
    {
        if (auto group = Json::Obj(item, L"group"))
        {
            auto id = Json::Str(group, L"id");
            std::wstring title = id == L"online" ? I18n::Tr(I18n::S::StatusOnline) : id == L"offline" ? I18n::Tr(I18n::S::StatusOffline) : L"";
            if (title.empty())
            {
                auto it = m_roleNames.find(id);
                title = it != m_roleNames.end() ? it->second : std::wstring{ I18n::Tr(I18n::S::RoleTitle) };
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

    void MainWindow::OnMemberListUpdate(Slim::Value const& d)
    {
        constexpr uint32_t MaxRows = 100;
        auto guild = FindGuild(Json::Str(d, L"guild_id"));
        auto ops = Json::Arr(d, L"ops");
        ::DiscordWin3::MemLog(guild && guild->id == m_memberListGuild ? L"members: update (match)" : L"members: update (skip)", ops ? ops.Size() : 0);
        if (!guild || !ops || guild->id != m_memberListGuild)
        {
            return;
        }

        // Group counts live in d.groups (items only carry the id).
        if (auto groups = Json::Arr(d, L"groups"))
        {
            for (auto const& g : groups)
            {
                if (g.IsObject())
                    m_memberGroupCounts[Json::Str(g, L"id")] = static_cast<int>(Json::Num(g, L"count"));
            }
        }

        for (auto const& value : ops)
        {
            if (!value.IsObject()) continue;
            auto op = value;
            auto kind = Json::Str(op, L"op");
            auto index = static_cast<uint32_t>(Json::Num(op, L"index"));

            if (kind == L"SYNC")
            {
                auto range = Json::Arr(op, L"range");
                if (!range || range.Size() < 1 || range.At(0).Num() != 0) continue;
                std::vector<IInspectable> rows;
                if (auto items = Json::Arr(op, L"items"))
                {
                    for (auto const& it : items)
                    {
                        if (!it.IsObject()) continue;
                        if (auto row = BuildMemberRow(it, *guild)) rows.push_back(row);
                        if (rows.size() >= MaxRows) break;
                    }
                }
                m_memberItems.ReplaceAll(rows);
                ::DiscordWin3::MemLog(L"members: sync rows", rows.size());
                // After a full replace the virtualizing panel can stay empty (0 containers): rebind so it re-realizes.
                MemberList().ItemsSource(nullptr);
                MemberList().ItemsSource(m_memberItems);
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
                if (range && range.Size() > 0 && range.At(0).Num() == 0) m_memberItems.Clear();
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
            name = it != m_users.end() ? it->second.name : std::wstring{ I18n::Tr(I18n::S::Someone) };
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
            TypingText().Text(I18n::Fmt(I18n::S::TypingOne, *names[0]));
        }
        else if (names.size() == 2)
        {
            TypingText().Text(I18n::Fmt(I18n::S::TypingTwo, *names[0], *names[1]));
        }
        else
        {
            TypingText().Text(I18n::Tr(I18n::S::TypingMany));
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
            StageFile(std::wstring{ result.Path() });
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

    // ------------------------------------------------------------------ unread state

    namespace
    {
        // READY sends these either as a bare array or as { entries: [...] } depending on capabilities.
        Slim::Value Entries(Slim::Value const& value)
        {
            if (value.IsArray()) return value;
            if (value.IsObject()) return Json::Arr(value, L"entries");
            return {};
        }
    }

    void MainWindow::ParseReadStates(Slim::Value const& d)
    {
        m_readStates.clear();
        auto entries = Entries(Json::Get(d, L"read_state"));
        if (!entries) return;
        for (auto const& e : entries)
        {
            if (!e.IsObject()) continue;
            auto o = e;
            if (Json::Num(o, L"read_state_type", 0) != 0) continue;   // 0 = channel
            m_readStates[Json::Str(o, L"id")] = { Json::Str(o, L"last_message_id"), static_cast<int>(Json::Num(o, L"mention_count")) };
        }
    }

    void MainWindow::ParseGuildSettings(Slim::Value const& settings)
    {
        m_mutedGuilds.clear();
        m_mutedChannels.clear();
        auto entries = Entries(settings);
        if (!entries) return;
        for (auto const& e : entries)
        {
            if (!e.IsObject()) continue;
            auto o = e;
            auto guildId = Json::Str(o, L"guild_id");
            if (Json::Bool(o, L"muted") && !guildId.empty()) m_mutedGuilds.insert(guildId);
            if (auto overrides = Json::Arr(o, L"channel_overrides"))
            {
                for (auto const& c : overrides)
                {
                    if (c.IsObject() && Json::Bool(c, L"muted"))
                        m_mutedChannels.insert(Json::Str(c, L"channel_id"));
                }
            }
        }
    }

    bool MainWindow::IsUnread(ChannelInfo const& channel) const
    {
        if (channel.lastMessageId.empty() || m_mutedChannels.contains(channel.id) || m_mutedChannels.contains(channel.parentId))
        {
            return false;
        }
        auto it = m_readStates.find(channel.id);
        // No read state = never opened: Discord treats those as read unless a mention arrives.
        return it != m_readStates.end() && Json::SnowflakeLess(it->second.lastRead, channel.lastMessageId);
    }

    int MainWindow::MentionsIn(std::wstring const& channelId) const
    {
        auto it = m_readStates.find(channelId);
        return it == m_readStates.end() ? 0 : it->second.mentions;
    }

    std::pair<bool, int> MainWindow::GuildBadge(GuildInfo const& guild) const
    {
        bool unread = false;
        int mentions = 0;
        bool muted = m_mutedGuilds.contains(guild.id);
        for (auto const& c : guild.channels)
        {
            if (!IsTextLike(c.type)) continue;
            int m = MentionsIn(c.id);
            bool u = !muted && IsUnread(c);
            if ((u || m) && guild.perms.CanView(c.overwrites))
            {
                unread |= u;
                mentions += m;
            }
        }
        return { unread, mentions };
    }

    std::pair<bool, int> MainWindow::HomeBadge() const
    {
        int mentions = 0;
        for (auto const& dm : m_dms)
        {
            if (!m_mutedChannels.contains(dm.id)) mentions += MentionsIn(dm.id);
        }
        return { mentions > 0, mentions };
    }

    IInspectable MainWindow::MakeGuildItem(GuildInfo const& g) const
    {
        std::wstring icon = g.icon.empty() ? L""
            : std::wstring{ Discord::CdnBase } + L"/icons/" + g.id + L"/" + g.icon + L".png?size=96";
        auto [unread, mentions] = GuildBadge(g);
        return make<GuildItem>(hstring{ g.id }, hstring{ g.name }, hstring{ icon }, unread, mentions);
    }

    void MainWindow::UpdateGuildRow(std::wstring const& guildId)
    {
        for (uint32_t i = 0; i < m_guildItems.Size(); ++i)
        {
            if (m_guildItems.GetAt(i).as<DiscordWin3::GuildItem>().Id() != guildId) continue;

            IInspectable item{ nullptr };
            if (guildId == HomeId)
            {
                item = make<GuildItem>(HomeId, I18n::Tr(I18n::S::DirectMessages), L"", false, HomeBadge().second);
            }
            else if (auto guild = FindGuild(guildId))
            {
                item = MakeGuildItem(*guild);
            }
            if (!item) return;

            bool selected = GuildList().SelectedIndex() == static_cast<int>(i);
            m_guildItems.SetAt(i, item);
            if (selected) GuildList().SelectedIndex(static_cast<int>(i));
            return;
        }
    }

    void MainWindow::UpdateChannelRow(std::wstring const& channelId)
    {
        auto channel = FindChannel(channelId);
        if (!channel) return;
        for (uint32_t i = 0; i < m_channelItems.Size(); ++i)
        {
            auto existing = get_self<implementation::ChannelItem>(m_channelItems.GetAt(i).as<DiscordWin3::ChannelItem>());
            if (existing->Id() != channelId) continue;

            bool selected = ChannelList().SelectedIndex() == static_cast<int>(i);
            auto item = make_self<ChannelItem>(existing->Id(), existing->Name(), existing->Glyph(), existing->Kind(),
                                               existing->AvatarUrl(), IsUnread(*channel), MentionsIn(channelId));
            if (!channel->recipientId.empty()) item->SetPresence(PresenceColor(channel->recipientId), PresenceText(channel->recipientId, false));
            m_channelItems.SetAt(i, item.as<IInspectable>());
            if (selected) ChannelList().SelectedIndex(static_cast<int>(i));
            return;
        }
    }

    ChannelInfo* MainWindow::FindChannel(std::wstring const& channelId, std::wstring* guildId)
    {
        if (auto it = m_channelGuild.find(channelId); it != m_channelGuild.end())
        {
            if (auto guild = FindGuild(it->second))
            {
                for (auto& c : guild->channels)
                {
                    if (c.id == channelId)
                    {
                        if (guildId) *guildId = guild->id;
                        return &c;
                    }
                }
            }
        }
        for (auto& dm : m_dms)
        {
            if (dm.id == channelId)
            {
                if (guildId) guildId->clear();
                return &dm;
            }
        }
        return nullptr;
    }

    void MainWindow::OnMessageForUnread(JsonObject const& d, MessageData const* data)
    {
        auto channelId = Json::Str(d, L"channel_id");
        auto messageId = Json::Str(d, L"id");
        auto authorId = Json::Str(Json::Obj(d, L"author"), L"id");
        auto guildId = Json::Str(d, L"guild_id");

        auto channel = FindChannel(channelId);
        if (channel) channel->lastMessageId = messageId;

        bool fromMe = authorId == m_selfId;
        bool viewing = channelId == m_currentChannelId && m_windowActive && !m_background;
        if (fromMe || viewing)
        {
            m_readStates[channelId] = { messageId, 0 };
            if (viewing && !fromMe) Ack(channelId, messageId);
        }
        else
        {
            bool mentioned = guildId.empty() || Json::Bool(d, L"mention_everyone");
            if (auto list = Json::Arr(d, L"mentions"))
            {
                for (auto const& u : list)
                {
                    if (u.ValueType() == JsonValueType::Object && Json::Str(u.GetObject(), L"id") == m_selfId) mentioned = true;
                }
            }
            auto& state = m_readStates[channelId];   // creates an "unread" entry if the channel was never opened
            if (mentioned && !m_mutedChannels.contains(channelId))
            {
                ++state.mentions;
                Notify(data ? *data : BuildMessage(d), channelId, guildId);
            }
        }

        if (guildId.empty())
        {
            if (m_currentGuildId == HomeId) RefreshChannelList();   // DMs are sorted by last activity
            UpdateGuildRow(HomeId);
        }
        else
        {
            if (guildId == m_currentGuildId) UpdateChannelRow(channelId);
            UpdateGuildRow(guildId);
        }
    }

    void MainWindow::Ack(std::wstring const& channelId, std::wstring const& messageId)
    {
        if (channelId.empty() || messageId.empty()) return;

        auto& state = m_readStates[channelId];
        bool changed = state.lastRead != messageId || state.mentions != 0;
        state = { messageId, 0 };
        if (changed)
        {
            std::wstring guildId;
            FindChannel(channelId, &guildId);
            UpdateChannelRow(channelId);
            UpdateGuildRow(guildId.empty() ? std::wstring{ HomeId } : guildId);
        }

        // Batch acks: Discord rate-limits them, one POST per ~1.5 s is plenty.
        m_ackChannel = channelId;
        m_ackMessage = messageId;
        if (m_ackScheduled || !changed) return;
        m_ackScheduled = true;
        [](weak_ref<MainWindow> weak) -> fire_and_forget
        {
            co_await resume_after(std::chrono::milliseconds(1500));
            auto self = weak.get();
            if (!self) co_return;
            co_await wil::resume_foreground(self->m_dispatcher);
            self->m_ackScheduled = false;
            if (!self->m_rest) co_return;
            JsonObject body;
            body.Insert(L"token", JsonValue::CreateNullValue());
            try
            {
                co_await self->m_rest->PostJson(L"/channels/" + self->m_ackChannel + L"/messages/" + self->m_ackMessage + L"/ack", body);
            }
            catch (...)
            {
            }
        }(get_weak());
    }

    void MainWindow::OnRemoteAck(JsonObject const& d)
    {
        // Read on another device (phone, browser...).
        auto channelId = Json::Str(d, L"channel_id");
        m_readStates[channelId] = { Json::Str(d, L"message_id"), static_cast<int>(Json::Num(d, L"mention_count")) };
        std::wstring guildId;
        FindChannel(channelId, &guildId);
        UpdateChannelRow(channelId);
        UpdateGuildRow(guildId.empty() ? std::wstring{ HomeId } : guildId);
    }

    // ------------------------------------------------------------------ reactions

    void MainWindow::RenderGallery(Grid const& grid, MessageData const& data)
    {
        grid.Children().Clear();
        auto const& images = data.gallery;
        if (images.empty()) return;

        // Official mosaic shapes: 2 side by side, 3 = big left + 2 stacked, 4 = 2x2, more = rows of 3
        // (the first row takes the remainder). Built from stack panels: one cell, no spanning maths.
        constexpr double W = 520, Gap = 4;
        auto tile = [&](size_t index, double width, double height)
        {
            auto const& img = images[index];
            Image image;
            image.Stretch(Media::Stretch::UniformToFill);
            image.Source(::DiscordWin3::ImageCache::Get(img.url, static_cast<int>(width)));
            Border cell;
            cell.Width(width);
            cell.Height(height);
            cell.Background(SolidBrush(0x1A1A1E));
            cell.Child(image);
            auto full = img.full;
            cell.Tapped([full](IInspectable const&, Input::TappedRoutedEventArgs const&)
            {
                Windows::System::Launcher::LaunchUriAsync(Windows::Foundation::Uri{ full });
            });
            return cell;
        };
        auto row = []()
        {
            StackPanel p;
            p.Orientation(Orientation::Horizontal);
            p.Spacing(Gap);
            return p;
        };
        StackPanel rows;
        rows.Spacing(Gap);

        size_t n = images.size();
        if (n == 3)
        {
            double big = (W - Gap) * 2 / 3, narrow = W - Gap - big, h = 350;
            auto r = row();
            r.Children().Append(tile(0, big, h));
            StackPanel column;
            column.Spacing(Gap);
            column.Children().Append(tile(1, narrow, (h - Gap) / 2));
            column.Children().Append(tile(2, narrow, (h - Gap) / 2));
            r.Children().Append(column);
            rows.Children().Append(r);
        }
        else
        {
            size_t perRow = (n == 2 || n == 4) ? 2 : 3;
            size_t first = perRow == 2 ? 0 : n % 3;
            size_t index = 0;
            if (first)
            {
                auto r = row();
                double w = first == 1 ? W : (W - Gap) / 2;
                for (size_t i = 0; i < first; ++i) r.Children().Append(tile(index++, w, first == 1 ? 280 : 220));
                rows.Children().Append(r);
            }
            double w = (W - Gap * (perRow - 1)) / perRow;
            double h = perRow == 2 ? (n == 2 ? w : w * 0.75) : w;
            while (index < n)
            {
                auto r = row();
                for (size_t c = 0; c < perRow && index < n; ++c) r.Children().Append(tile(index++, w, h));
                rows.Children().Append(r);
            }
        }
        grid.Children().Append(rows);
    }

    void MainWindow::RenderReactions(StackPanel const& panel, MessageData const& data)
    {
        panel.Children().Clear();
        for (auto const& r : data.reactions)
        {
            StackPanel content;
            content.Orientation(Orientation::Horizontal);
            content.Spacing(6);
            if (!r.id.empty())
            {
                Image image;
                image.Width(18);
                image.Height(18);
                image.Source(::DiscordWin3::ImageCache::Get(std::wstring{ Discord::CdnBase } + L"/emojis/" + r.id + L".png?size=48", 18));
                content.Children().Append(image);
            }
            else
            {
                TextBlock emoji;
                emoji.Text(r.name);
                emoji.FontSize(15);
                content.Children().Append(emoji);
            }
            TextBlock count;
            count.Text(std::to_wstring(r.count));
            count.FontSize(13);
            count.FontWeight(Windows::UI::Text::FontWeight{ 600 });
            count.VerticalAlignment(VerticalAlignment::Center);
            count.Foreground(SolidBrush(r.me ? 0xDEE0FC : 0xB5BAC1).as<Media::Brush>());
            content.Children().Append(count);

            Button chip;
            chip.Content(content);
            chip.Padding({ 6, 2, 8, 2 });
            chip.MinHeight(0);
            chip.CornerRadius({ 8, 8, 8, 8 });
            chip.Background(SolidBrush(r.me ? 0x5865F2 : 0x2B2D31, r.me ? 0x40 : 0xFF));
            chip.BorderBrush(SolidBrush(r.me ? 0x5865F2 : 0x2B2D31));
            chip.BorderThickness({ 1, 1, 1, 1 });
            ToolTipService::SetToolTip(chip, box_value(r.id.empty() ? r.name : L":" + r.name + L":"));

            auto messageId = data.id;
            auto reaction = r;
            chip.Click([weak = get_weak(), messageId, reaction](IInspectable const&, RoutedEventArgs const&)
            {
                if (auto self = weak.get()) self->ToggleReaction(messageId, reaction);
            });
            panel.Children().Append(chip);
        }
        if (!data.reactions.empty())
        {
            // Trailing "add reaction" chip, like the official client.
            FontIcon icon;
            icon.Glyph(L"");
            icon.FontSize(15);
            icon.Foreground(SolidBrush(0xB5BAC1).as<Media::Brush>());
            Button add;
            add.Content(icon);
            add.Padding({ 7, 3, 7, 3 });
            add.MinHeight(0);
            add.CornerRadius({ 8, 8, 8, 8 });
            add.Background(SolidBrush(0x2B2D31));
            add.BorderThickness({ 0, 0, 0, 0 });
            add.Click([weak = get_weak()](IInspectable const& sender, RoutedEventArgs const& args)
            {
                if (auto self = weak.get()) self->OnHoverPicker(sender, args);
            });
            panel.Children().Append(add);
        }
    }

    fire_and_forget MainWindow::ToggleReaction(std::wstring messageId, ::DiscordWin3::Reaction reaction)
    {
        auto strong = get_strong();
        auto rest = m_rest;
        auto channelId = m_currentChannelId;
        if (!rest || channelId.empty()) co_return;

        if (!reaction.me && reaction.id.empty()) RememberEmoji(reaction.name);
        auto key = std::wstring{ Uri::EscapeComponent(reaction.ApiKey()) };
        auto path = L"/channels/" + channelId + L"/messages/" + messageId + L"/reactions/" + key + L"/@me";
        try
        {
            co_await rest->Call(reaction.me ? Windows::Web::Http::HttpMethod::Delete() : Windows::Web::Http::HttpMethod::Put(), path);
        }
        catch (...)
        {
        }
        // The gateway echo (MESSAGE_REACTION_ADD/REMOVE) updates the row.
    }

    void MainWindow::OnReactionEvent(std::wstring const& type, JsonObject const& d)
    {
        if (Json::Str(d, L"channel_id") != m_currentChannelId) return;
        int index = FindMessage(Json::Str(d, L"message_id"));
        if (index < 0) return;

        auto item = Impl(m_messageItems.GetAt(index));
        MessageData data = item->Data();
        auto emoji = Json::Obj(d, L"emoji");
        auto name = Json::Str(emoji, L"name");
        auto id = Json::Str(emoji, L"id");
        bool mine = Json::Str(d, L"user_id") == m_selfId;
        auto same = [&](::DiscordWin3::Reaction const& r) { return id.empty() ? (r.id.empty() && r.name == name) : r.id == id; };
        auto it = std::find_if(data.reactions.begin(), data.reactions.end(), same);

        if (type == L"MESSAGE_REACTION_ADD")
        {
            if (it == data.reactions.end()) data.reactions.push_back({ name, id, 1, mine });
            else { ++it->count; it->me |= mine; }
        }
        else if (type == L"MESSAGE_REACTION_REMOVE")
        {
            if (it == data.reactions.end()) return;
            --it->count;
            if (mine) it->me = false;
            if (it->count <= 0) data.reactions.erase(it);
        }
        else if (type == L"MESSAGE_REACTION_REMOVE_ALL")
        {
            data.reactions.clear();
        }
        else if (type == L"MESSAGE_REACTION_REMOVE_EMOJI")
        {
            if (it != data.reactions.end()) data.reactions.erase(it);
        }
        else
        {
            return;
        }
        m_messageItems.SetAt(index, make<MessageItem>(std::move(data), item->ShowsHeader(), item->Day()));
    }

    // ------------------------------------------------------------------ message actions

    void MainWindow::OnMessageMenuOpening(IInspectable const& sender)
    {
        auto flyout = sender.as<MenuFlyout>();
        flyout.Items().Clear();
        auto target = flyout.Target().try_as<FrameworkElement>();
        auto message = target ? target.DataContext().try_as<DiscordWin3::MessageItem>() : nullptr;
        if (!message) return;

        MessageData data = Impl(message)->Data();
        bool own = data.authorId == m_selfId;
        auto weak = get_weak();

        auto add = [&](std::wstring const& text, wchar_t const* glyph, std::function<void(MainWindow*)> action, uint32_t color = 0)
        {
            MenuFlyoutItem item;
            item.Text(text);
            if (glyph)
            {
                FontIcon icon;
                icon.Glyph(glyph);
                item.Icon(icon);
            }
            if (color) item.Foreground(SolidBrush(color));
            item.Click([weak, action](IInspectable const&, RoutedEventArgs const&)
            {
                if (auto self = weak.get()) action(self.get());
            });
            flyout.Items().Append(item);
        };

        // Quick reactions, like the hover bar of the official client.
        MenuFlyoutSubItem react;
        react.Text(I18n::Tr(I18n::S::AddReaction));
        FontIcon reactIcon;
        reactIcon.Glyph(L"");
        react.Icon(reactIcon);
        for (auto emoji : { L"👍", L"❤️", L"😂", L"😮", L"😢", L"🔥", L"👀", L"✅" })
        {
            MenuFlyoutItem item;
            item.Text(emoji);
            ::DiscordWin3::Reaction reaction{ emoji, L"", 0, false };
            for (auto const& r : data.reactions)
            {
                if (r.id.empty() && r.name == emoji) reaction.me = r.me;
            }
            auto messageId = data.id;
            item.Click([weak, messageId, reaction](IInspectable const&, RoutedEventArgs const&)
            {
                if (auto self = weak.get()) self->ToggleReaction(messageId, reaction);
            });
            react.Items().Append(item);
        }
        flyout.Items().Append(react);

        if (own)
        {
            add(I18n::Tr(I18n::S::EditMessage), L"", [data](MainWindow* w) { w->StartEdit(data); });
        }
        add(I18n::Tr(I18n::S::Reply), L"", [data](MainWindow* w) { w->StartReply(data); });
        add(I18n::Tr(I18n::S::ForwardAction), L"", [data](MainWindow* w) { w->ForwardMessage(data); });

        flyout.Items().Append(MenuFlyoutSeparator{});
        auto copy = [](std::wstring const& text)
        {
            Windows::ApplicationModel::DataTransfer::DataPackage package;
            package.SetText(text);
            Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
        };
        add(I18n::Tr(I18n::S::CopyText), L"", [data, copy](MainWindow*) { copy(data.rawContent); });
        add(I18n::Tr(I18n::S::CopyLink), L"", [data, copy](MainWindow* w)
        {
            auto guild = w->m_currentGuildId == HomeId ? std::wstring{ L"@me" } : w->m_currentGuildId;
            copy(L"https://discord.com/channels/" + guild + L"/" + w->m_currentChannelId + L"/" + data.id);
        });
        add(I18n::Tr(I18n::S::CopyId), L"", [data, copy](MainWindow*) { copy(data.id); });

        if (own)
        {
            flyout.Items().Append(MenuFlyoutSeparator{});
            add(I18n::Tr(I18n::S::DeleteMessage), L"", [data](MainWindow* w) { w->DeleteMessage(data.id); }, 0xF23F43);
        }
    }

    void MainWindow::StartReply(MessageData const& data)
    {
        m_editingId.clear();
        m_replyToId = data.id;
        ReplyText().Text(I18n::Fmt(I18n::S::ReplyingTo, data.authorName));
        ReplyBar().Visibility(Visibility::Visible);
        Composer().Focus(FocusState::Programmatic);
    }

    void MainWindow::StartEdit(MessageData const& data)
    {
        m_replyToId.clear();
        m_editingId = data.id;
        ReplyText().Text(I18n::Tr(I18n::S::EditingHint));
        ReplyBar().Visibility(Visibility::Visible);
        Composer().Text(data.rawContent);
        Composer().Focus(FocusState::Programmatic);
        Composer().SelectionStart(static_cast<int32_t>(data.rawContent.size()));
    }

    void MainWindow::ClearComposerMode()
    {
        m_replyToId.clear();
        m_editingId.clear();
        ReplyBar().Visibility(Visibility::Collapsed);
    }

    void MainWindow::OnCancelReply(IInspectable const&, RoutedEventArgs const&)
    {
        bool wasEditing = !m_editingId.empty();
        ClearComposerMode();
        if (wasEditing) Composer().Text(L"");
    }

    bool MainWindow::EditLastOwnMessage()
    {
        for (int i = static_cast<int>(m_messageItems.Size()) - 1; i >= 0; --i)
        {
            auto const& data = Impl(m_messageItems.GetAt(i))->Data();
            if (data.authorId == m_selfId)
            {
                StartEdit(data);
                return true;
            }
        }
        return false;
    }

    fire_and_forget MainWindow::DeleteMessage(std::wstring messageId)
    {
        auto strong = get_strong();
        auto channelId = m_currentChannelId;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(I18n::Tr(I18n::S::DeleteMessage)));
        dialog.Content(box_value(I18n::Tr(I18n::S::DeleteConfirm)));
        dialog.PrimaryButtonText(I18n::Tr(I18n::S::Delete));
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Close);
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary || !m_rest) co_return;

        try
        {
            co_await m_rest->Call(Windows::Web::Http::HttpMethod::Delete(), L"/channels/" + channelId + L"/messages/" + messageId);
        }
        catch (hresult_error const& e)
        {
            StatusText().Text(L"Suppression impossible : " + std::wstring{ e.message() }.substr(0, 80));
        }
    }

    // ------------------------------------------------------------------ notifications

    void MainWindow::InitNotifications()
    {
        using namespace Microsoft::Windows::AppNotifications;
        try
        {
            auto manager = AppNotificationManager::Default();
            manager.NotificationInvoked([weak = get_weak()](auto&&, AppNotificationActivatedEventArgs const& args)
            {
                auto self = weak.get();
                if (!self) return;
                auto arguments = args.Arguments();
                std::wstring channel = arguments.HasKey(L"channel") ? std::wstring{ arguments.Lookup(L"channel") } : L"";
                std::wstring guild = arguments.HasKey(L"guild") ? std::wstring{ arguments.Lookup(L"guild") } : L"";
                self->m_dispatcher.TryEnqueue([weak, guild, channel]()
                {
                    if (auto w = weak.get())
                    {
                        w->Activate();
                        w->OpenChannel(guild, channel);
                    }
                });
            });
            manager.Register();
            m_notificationsReady = true;

            Closed([](auto&&, auto&&)
            {
                try { AppNotificationManager::Default().Unregister(); } catch (...) {}
            });
        }
        catch (...)
        {
            m_notificationsReady = false;   // notifications are a bonus, never block startup
        }
    }

    void MainWindow::Notify(MessageData const& data, std::wstring const& channelId, std::wstring const& guildId)
    {
        if (m_windowActive && !m_background && channelId == m_currentChannelId) return;

        // Taskbar flash, like the official client on a mention.
        if (!m_windowActive || m_background)
        {
            FLASHWINFO flash{ sizeof(FLASHWINFO) };
            flash.hwnd = Microsoft::UI::GetWindowFromWindowId(AppWindow().Id());
            flash.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
            FlashWindowEx(&flash);
        }
        if (!m_notificationsReady) return;

        std::wstring title = data.authorName;
        if (!guildId.empty())
        {
            auto channel = m_channelNames.find(channelId);
            auto guild = FindGuild(guildId);
            title += L" (#" + (channel != m_channelNames.end() ? channel->second : std::wstring{}) +
                     (guild ? L", " + guild->name : std::wstring{}) + L")";
        }
        std::wstring text;
        for (auto const& s : data.body) text += s.text;
        if (text.empty()) text = data.imageUrl.empty() ? I18n::Tr(I18n::S::Attachment) : I18n::Tr(I18n::S::ImageWord);
        if (text.size() > 200) text = text.substr(0, 200) + L"…";

        try
        {
            using namespace Microsoft::Windows::AppNotifications;
            auto notification = Builder::AppNotificationBuilder()
                .AddArgument(L"channel", channelId)
                .AddArgument(L"guild", guildId.empty() ? std::wstring{ HomeId } : guildId)
                .AddText(title)
                .AddText(text)
                .BuildNotification();
            AppNotificationManager::Default().Show(notification);
        }
        catch (...)
        {
        }
    }

    void MainWindow::OpenChannel(std::wstring const& guildId, std::wstring const& channelId)
    {
        if (guildId.empty() || channelId.empty()) return;
        for (uint32_t i = 0; i < m_guildItems.Size(); ++i)
        {
            if (m_guildItems.GetAt(i).as<DiscordWin3::GuildItem>().Id() == guildId)
            {
                GuildList().SelectedIndex(static_cast<int>(i));
                break;
            }
        }
        for (uint32_t i = 0; i < m_channelItems.Size(); ++i)
        {
            if (m_channelItems.GetAt(i).as<DiscordWin3::ChannelItem>().Id() == channelId)
            {
                ChannelList().SelectedIndex(static_cast<int>(i));
                break;
            }
        }
    }

    // ------------------------------------------------------------------ home: presence & relationships

    void MainWindow::ParsePresence(Slim::Value const& p, std::wstring userId)
    {
        if (userId.empty())
        {
            auto user = Json::Obj(p, L"user");
            userId = user ? Json::Str(user, L"id") : Json::Str(p, L"user_id");
        }
        if (userId.empty()) return;

        Presence presence;
        presence.status = Json::Str(p, L"status");
        auto activities = Json::Arr(p, L"activities");
        presence.activity = ActivityText(activities);
        if (activities)
        {
            for (auto const& a : activities)
            {
                if (!a.IsObject()) continue;
                auto type = static_cast<int>(Json::Num(a, L"type", -1));
                if (type >= 0 && type <= 3)
                {
                    presence.hasActivity = true;
                    if (presence.game.empty())
                    {
                        presence.game = ActivityLine(a);
                    }
                }
            }
        }
        m_presence[userId] = std::move(presence);
    }

    void MainWindow::ParseRelationships(Slim::Value const& d)
    {
        m_relationships.clear();
        if (auto list = Json::Arr(d, L"relationships"))
        {
            for (auto const& r : list)
            {
                if (!r.IsObject()) continue;
                auto o = r;
                if (auto user = Json::Obj(o, L"user")) CacheUser(user);
                m_relationships[Json::Str(o, L"id")] = { static_cast<int>(Json::Num(o, L"type")), Json::Str(o, L"nickname") };
            }
        }

        // Friends' presences: plain "presences" or "merged_presences.friends" depending on capabilities.
        auto presences = Json::Arr(d, L"presences");
        if (!presences) presences = Json::Arr(Json::Obj(d, L"merged_presences"), L"friends");
        if (presences)
        {
            for (auto const& p : presences)
            {
                if (p.IsObject()) ParsePresence(p);
            }
        }
    }

    void MainWindow::OnPresenceUpdate(Slim::Value const& d)
    {
        auto user = Json::Obj(d, L"user");
        auto userId = Json::Str(user, L"id");
        bool friendOrDm = m_relationships.contains(userId);
        ChannelInfo* dm = nullptr;
        for (auto& c : m_dms)
        {
            if (c.recipientId == userId) { dm = &c; friendOrDm = true; }
        }
        if (!friendOrDm) return;   // guild-wide presences are not tracked (RAM)

        if (Json::Str(user, L"username").size()) CacheUser(user);
        ParsePresence(d, userId);

        if (dm && m_currentGuildId == HomeId) UpdateChannelRow(dm->id);
        if (m_showingFriends)
        {
            RefreshFriends();
            RefreshActiveNow();
        }
    }

    void MainWindow::OnRelationshipEvent(std::wstring const& type, Slim::Value const& d)
    {
        auto id = Json::Str(d, L"id");
        if (type == L"RELATIONSHIP_REMOVE")
        {
            m_relationships.erase(id);
        }
        else
        {
            if (auto user = Json::Obj(d, L"user")) CacheUser(user);
            auto& r = m_relationships[id];
            if (Json::Get(d, L"type")) r.type = static_cast<int>(Json::Num(d, L"type"));
            r.nickname = Json::Str(d, L"nickname");
        }
        if (m_showingFriends) RefreshFriends();
    }

    uint32_t MainWindow::PresenceColor(std::wstring const& userId) const
    {
        auto it = m_presence.find(userId);
        return StatusColor(it == m_presence.end() || it->second.status.empty() ? std::wstring{ L"offline" } : it->second.status);
    }

    std::wstring MainWindow::PresenceText(std::wstring const& userId, bool fallbackToStatus) const
    {
        auto it = m_presence.find(userId);
        if (it != m_presence.end() && !it->second.activity.empty()) return it->second.activity;
        if (!fallbackToStatus) return {};
        auto status = it == m_presence.end() ? std::wstring{} : it->second.status;
        if (status == L"online") return I18n::Tr(I18n::S::StatusOnline);
        if (status == L"idle") return I18n::Tr(I18n::S::StatusIdle);
        if (status == L"dnd") return I18n::Tr(I18n::S::StatusDnd);
        return I18n::Tr(I18n::S::StatusOffline);
    }

    // ------------------------------------------------------------------ home: friends page

    void MainWindow::UpdateHomeChrome()
    {
        bool home = m_currentGuildId == HomeId;
        GuildHeader().Visibility(Show(!home));
        QuickSwitchButton().Visibility(Show(home));
        HomeNav().Visibility(Show(home));
    }

    void MainWindow::ShowFriends(bool show)
    {
        m_showingFriends = show;
        FriendsView().Visibility(Show(show));
        FriendsNavButton().Background(show ? SolidBrush(0x2C2C31) : SolidBrush(0, 0));
        if (!show) return;

        // Leaving the conversation frees its rows (and their images).
        ++m_channelGeneration;
        m_currentChannelId.clear();
        m_messageItems.Clear();
        m_typing.clear();
        UpdateTypingText();
        ClearComposerMode();
        ChannelList().SelectedIndex(-1);
        TitleText().Text(I18n::Tr(I18n::S::Friends));
        TitleIcon().Source(nullptr);
        MembersPane().Visibility(Visibility::Visible);
        RefreshFriends();
        RefreshActiveNow();
    }

    void MainWindow::RefreshFriends()
    {
        auto lower = [](std::wstring s)
        {
            if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
            return s;
        };
        auto filter = lower(std::wstring{ FriendsSearch().Text() });
        bool adding = m_friendsTab == L"add";

        int pending = 0;
        for (auto const& [id, r] : m_relationships) if (r.type == 3 || r.type == 4) ++pending;
        TabPending().Visibility(Show(pending > 0));
        if (m_friendsTab == L"pending" && pending == 0) m_friendsTab = L"online";

        for (auto [tab, button] : { std::pair{ L"online", TabOnline() }, std::pair{ L"all", TabAll() }, std::pair{ L"pending", TabPending() } })
        {
            button.Background(m_friendsTab == tab ? SolidBrush(0x2C2C31) : SolidBrush(0, 0));
        }
        AddFriendPanel().Visibility(Show(adding));
        FriendsSearch().Visibility(Show(!adding));
        FriendsList().Visibility(Show(!adding));
        FriendsCount().Visibility(Show(!adding));
        if (adding) return;

        struct Row { std::wstring name; IInspectable item; };
        std::vector<Row> rows;
        for (auto const& [id, r] : m_relationships)
        {
            bool isFriend = r.type == 1;
            bool isPending = r.type == 3 || r.type == 4;
            auto presence = m_presence.find(id);
            auto status = presence == m_presence.end() ? std::wstring{} : presence->second.status;
            bool online = status == L"online" || status == L"idle" || status == L"dnd";

            if (m_friendsTab == L"online" && !(isFriend && online)) continue;
            if (m_friendsTab == L"all" && !isFriend) continue;
            if (m_friendsTab == L"pending" && !isPending) continue;

            auto user = m_users.find(id);
            std::wstring name = !r.nickname.empty() ? r.nickname : user != m_users.end() ? user->second.name : id;
            if (!filter.empty() && lower(name).find(filter) == std::wstring::npos) continue;

            std::wstring subtitle = isPending ? std::wstring{ I18n::Tr(r.type == 3 ? I18n::S::FriendIncoming : I18n::S::FriendOutgoing) }
                                              : PresenceText(id, true);
            auto tag = m_userTags.find(id);
            rows.push_back({ lower(name), make<FriendItem>(id, name, tag != m_userTags.end() ? tag->second : L"", subtitle,
                                                           user != m_users.end() ? user->second.avatarUrl : DefaultAvatar(id, L"0"),
                                                           PresenceColor(id), r.type) });
        }
        std::sort(rows.begin(), rows.end(), [](Row const& a, Row const& b) { return a.name < b.name; });

        std::vector<IInspectable> items;
        items.reserve(rows.size());
        for (auto& r : rows) items.push_back(std::move(r.item));
        m_friendItems.ReplaceAll(items);

        std::wstring label = I18n::Tr(m_friendsTab == L"online" ? I18n::S::StatusOnline : m_friendsTab == L"all" ? I18n::S::AllFriends : I18n::S::TabPending);
        FriendsCount().Text(label + L" — " + std::to_wstring(items.size()));
    }

    void MainWindow::RefreshActiveNow()
    {
        // Right column on the friends page: friends currently playing / listening / watching.
        std::vector<IInspectable> items;
        items.push_back(make<MemberItem>(std::wstring{ I18n::Tr(I18n::S::ActiveNow) }));
        for (auto const& [id, r] : m_relationships)
        {
            if (r.type != 1) continue;
            auto presence = m_presence.find(id);
            if (presence == m_presence.end() || !presence->second.hasActivity) continue;
            auto user = m_users.find(id);
            items.push_back(make<MemberItem>(user != m_users.end() ? user->second.name : id, 0u,
                                             user != m_users.end() ? user->second.avatarUrl : DefaultAvatar(id, L"0"),
                                             presence->second.status, presence->second.game));
            if (items.size() > 40) break;
        }
        if (items.size() == 1)
        {
            items.push_back(make<MemberItem>(std::wstring{ I18n::Tr(I18n::S::QuietNow) }));
        }
        m_memberListGuild.clear();
        m_memberItems.ReplaceAll(items);
    }

    void MainWindow::OnShowFriends(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_currentGuildId != HomeId)
        {
            GuildList().SelectedIndex(0);   // Home is always the first rail item
        }
        ShowFriends(true);
    }

    void MainWindow::OnFriendsTab(IInspectable const& sender, RoutedEventArgs const&)
    {
        m_friendsTab = unbox_value<hstring>(sender.as<Button>().Tag());
        RefreshFriends();
        if (m_friendsTab == L"add") AddFriendBox().Focus(FocusState::Programmatic);
    }

    void MainWindow::OnFriendsSearchChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        if (m_showingFriends) RefreshFriends();
    }

    fire_and_forget MainWindow::OnSendFriendRequest(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        std::wstring username{ AddFriendBox().Text() };
        while (!username.empty() && username.back() == L' ') username.pop_back();
        if (username.empty() || !m_rest) co_return;

        JsonObject body;
        body.Insert(L"username", JsonValue::CreateStringValue(username));
        body.Insert(L"discriminator", JsonValue::CreateNullValue());
        hstring error;
        try
        {
            co_await m_rest->PostJson(L"/users/@me/relationships", body);
        }
        catch (hresult_error const& e)
        {
            error = e.message();
        }
        co_await wil::resume_foreground(m_dispatcher);
        std::wstring message{ error };
        if (message.empty())
        {
            AddFriendResult().Foreground(SolidBrush(0x23A55A));
            AddFriendResult().Text(I18n::Fmt(I18n::S::FriendRequestSent, username));
            AddFriendBox().Text(L"");
        }
        else
        {
            AddFriendResult().Foreground(SolidBrush(0xF23F43));
            AddFriendResult().Text(message.find(L"captcha") != std::wstring::npos
                ? I18n::Tr(I18n::S::FriendRequestCaptcha)
                : I18n::Tr(I18n::S::FriendRequestFailed));
        }
    }

    void MainWindow::OnFriendMessage(IInspectable const& sender, RoutedEventArgs const&)
    {
        OpenDmWith(std::wstring{ unbox_value<hstring>(sender.as<Button>().Tag()) });
    }

    fire_and_forget MainWindow::OnFriendAccept(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        auto id = std::wstring{ unbox_value<hstring>(sender.as<Button>().Tag()) };
        if (!m_rest) co_return;
        try
        {
            co_await m_rest->Call(Windows::Web::Http::HttpMethod::Put(), L"/users/@me/relationships/" + id, JsonObject{});
        }
        catch (...)
        {
        }
    }

    fire_and_forget MainWindow::OnFriendRemove(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        auto id = std::wstring{ unbox_value<hstring>(sender.as<Button>().Tag()) };
        if (!m_rest) co_return;

        if (auto it = m_relationships.find(id); it != m_relationships.end() && it->second.type == 1)
        {
            auto user = m_users.find(id);
            ContentDialog dialog;
            dialog.XamlRoot(Content().XamlRoot());
            dialog.Title(box_value(I18n::Fmt(I18n::S::RemoveFriendTitle, user != m_users.end() ? user->second.name : id)));
            dialog.Content(box_value(I18n::Tr(I18n::S::RemoveFriendConfirm)));
            dialog.PrimaryButtonText(I18n::Tr(I18n::S::RemoveFriend));
            dialog.CloseButtonText(L"Annuler");
            dialog.DefaultButton(ContentDialogButton::Close);
            if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        }
        try
        {
            co_await m_rest->Call(Windows::Web::Http::HttpMethod::Delete(), L"/users/@me/relationships/" + id);
        }
        catch (...)
        {
        }
    }

    fire_and_forget MainWindow::OpenDmWith(std::wstring userId)
    {
        auto strong = get_strong();
        for (auto const& dm : m_dms)
        {
            if (dm.recipientId == userId)
            {
                OpenChannel(HomeId, dm.id);
                co_return;
            }
        }
        if (!m_rest) co_return;

        JsonArray recipients;
        recipients.Append(JsonValue::CreateStringValue(userId));
        JsonObject body;
        body.Insert(L"recipients", recipients);
        IJsonValue result{ nullptr };
        try
        {
            result = co_await m_rest->PostJson(L"/users/@me/channels", body);
        }
        catch (...)
        {
        }
        co_await wil::resume_foreground(m_dispatcher);
        if (!result || result.ValueType() != JsonValueType::Object) co_return;

        auto doc = Slim::Document::Parse(std::wstring{ result.Stringify() });
        if (!doc) co_return;
        auto o = doc->Root();
        auto info = ParseChannel(o);
        ParseDmChannel(o, info);
        m_channelNames[info.id] = info.name;
        auto id = info.id;
        if (!FindChannel(id)) m_dms.push_back(std::move(info));
        if (m_currentGuildId == HomeId) RefreshChannelList();
        OpenChannel(HomeId, id);
    }

    // ------------------------------------------------------------------ quick switcher (Ctrl+K)

    fire_and_forget MainWindow::OnQuickSwitch(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        struct Target { std::wstring label, lower, guild, channel; };
        auto targets = std::make_shared<std::vector<Target>>();
        auto lower = [](std::wstring s)
        {
            if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
            return s;
        };
        for (auto const& dm : m_dms)
        {
            targets->push_back({ L"@ " + dm.name, lower(dm.name), HomeId, dm.id });
        }
        for (auto const& g : m_guilds)
        {
            for (auto const& c : g.channels)
            {
                if (!IsTextLike(c.type) || !g.perms.CanView(c.overwrites)) continue;
                targets->push_back({ L"# " + c.name + L"   —   " + g.name, lower(c.name + L" " + g.name), g.id, c.id });
            }
        }

        AutoSuggestBox box;
        box.PlaceholderText(I18n::Tr(I18n::S::QuickSwitchPlaceholder));
        box.Width(460);
        auto matches = std::make_shared<std::vector<Target const*>>();

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(I18n::Tr(I18n::S::QuickSwitch)));
        dialog.Content(box);
        dialog.CloseButtonText(I18n::Tr(I18n::S::Close));

        box.TextChanged([targets, matches, lower](AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
        {
            if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput) return;
            auto query = lower(std::wstring{ sender.Text() });
            matches->clear();
            std::vector<IInspectable> labels;
            for (auto const& t : *targets)
            {
                if (query.empty() || t.lower.find(query) == std::wstring::npos) continue;
                matches->push_back(&t);
                labels.push_back(box_value(t.label));
                if (labels.size() >= 12) break;
            }
            sender.ItemsSource(single_threaded_vector(std::move(labels)));
        });

        auto choose = [weak = get_weak(), matches, dialog](std::wstring const& label)
        {
            auto self = weak.get();
            if (!self) return;
            for (auto t : *matches)
            {
                if (t->label == label || label.empty())
                {
                    dialog.Hide();
                    self->OpenChannel(t->guild, t->channel);
                    return;
                }
            }
        };
        box.SuggestionChosen([choose](AutoSuggestBox const&, AutoSuggestBoxSuggestionChosenEventArgs const& args)
        {
            choose(std::wstring{ unbox_value<hstring>(args.SelectedItem()) });
        });
        box.QuerySubmitted([choose](AutoSuggestBox const&, AutoSuggestBoxQuerySubmittedEventArgs const& args)
        {
            choose(args.ChosenSuggestion() ? std::wstring{ unbox_value<hstring>(args.ChosenSuggestion()) } : std::wstring{});
        });
        box.Loaded([](IInspectable const& sender, RoutedEventArgs const&)
        {
            sender.as<AutoSuggestBox>().Focus(FocusState::Programmatic);
        });

        co_await dialog.ShowAsync();
    }

    // ------------------------------------------------------------------ back / forward history

    void MainWindow::PushHistory()
    {
        if (m_navigatingHistory || m_currentChannelId.empty()) return;
        std::pair<std::wstring, std::wstring> entry{ m_currentGuildId, m_currentChannelId };
        if (!m_history.empty() && m_history[m_historyIndex] == entry) return;

        if (!m_history.empty()) m_history.resize(m_historyIndex + 1);
        m_history.push_back(entry);
        if (m_history.size() > 50) m_history.erase(m_history.begin());
        m_historyIndex = m_history.size() - 1;
        UpdateNavButtons();
    }

    void MainWindow::UpdateNavButtons()
    {
        BackButton().IsEnabled(m_historyIndex > 0 && !m_history.empty());
        ForwardButton().IsEnabled(m_historyIndex + 1 < m_history.size());
    }

    void MainWindow::OnNavigateBack(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_historyIndex == 0 || m_history.empty()) return;
        --m_historyIndex;
        m_navigatingHistory = true;
        OpenChannel(m_history[m_historyIndex].first, m_history[m_historyIndex].second);
        m_navigatingHistory = false;
        UpdateNavButtons();
    }

    void MainWindow::OnNavigateForward(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_historyIndex + 1 >= m_history.size()) return;
        ++m_historyIndex;
        m_navigatingHistory = true;
        OpenChannel(m_history[m_historyIndex].first, m_history[m_historyIndex].second);
        m_navigatingHistory = false;
        UpdateNavButtons();
    }

    void MainWindow::UpdateTitleBarRegions()
    {
        // The custom title bar is a drag area: let clicks through on the Back / Forward buttons.
        auto root = Content().try_as<FrameworkElement>();
        if (!root || !root.XamlRoot()) return;
        double scale = root.XamlRoot().RasterizationScale();
        auto bounds = NavButtons().TransformToVisual(nullptr).TransformBounds({ 0, 0,
            static_cast<float>(NavButtons().ActualWidth()), static_cast<float>(NavButtons().ActualHeight()) });
        Windows::Graphics::RectInt32 rect{ static_cast<int32_t>(bounds.X * scale), static_cast<int32_t>(bounds.Y * scale),
                                           static_cast<int32_t>(bounds.Width * scale), static_cast<int32_t>(bounds.Height * scale) };
        auto source = Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(AppWindow().Id());
        source.SetRegionRects(Microsoft::UI::Input::NonClientRegionKind::Passthrough, { rect });
    }

    // ------------------------------------------------------------------ translations

    void MainWindow::ApplyTexts()
    {
        using I18n::S;
        using I18n::Tr;
        auto tip = [](DependencyObject const& o, S key) { ToolTipService::SetToolTip(o, box_value(Tr(key))); };

        LoginWelcomeText().Text(Tr(S::LoginWelcome));
        LoginSubtitleText().Text(Tr(S::LoginSubtitle));
        LoginTokenLabel().Text(Tr(S::LoginTokenLabel));
        TokenBox().PlaceholderText(Tr(S::LoginTokenPlaceholder));
        TokenLoginButton().Content(box_value(Tr(S::LoginButton)));
        QrTitle().Text(Tr(S::QrTitle));
        QrHint().Text(Tr(S::QrHint));
        QrRetry().Content(box_value(Tr(S::QrRetry)));

        QuickSwitchText().Text(Tr(S::QuickSwitch));
        FriendsNavText().Text(Tr(S::Friends));
        DmHeaderText().Text(Tr(S::DirectMessages));
        FriendsTitleText().Text(Tr(S::Friends));
        TabOnline().Content(box_value(Tr(S::StatusOnline)));
        TabAll().Content(box_value(Tr(S::TabAll)));
        TabPending().Content(box_value(Tr(S::TabPending)));
        TabAdd().Content(box_value(Tr(S::TabAdd)));
        FriendsSearch().PlaceholderText(Tr(S::Search));
        AddFriendTitle().Text(Tr(S::TabAdd));
        AddFriendHintText().Text(Tr(S::AddFriendHint));
        AddFriendBox().PlaceholderText(Tr(S::AddFriendHint));
        SendFriendRequestButton().Content(box_value(Tr(S::SendFriendRequest)));

        tip(BackButton(), S::Back);
        tip(ForwardButton(), S::Forward);
        tip(MembersToggle(), S::ShowMembers);
        tip(AttachButton(), S::AttachFile);
        tip(ComposerEmojiButton(), S::PickEmoji);
        tip(CancelReplyButton(), S::CancelEsc);
        tip(SettingsButton(), S::Settings);
        tip(AddServerButton(), S::AddServer);
        tip(DiscoverButton(), S::Discover);
        UpdateVoiceButtons();
        tip(QuickSwitchButton(), S::QuickSwitch);
    }

    void MainWindow::OnAddServer(IInspectable const& sender, RoutedEventArgs const&)
    {
        // Join through an invite link (the server arrives by GUILD_CREATE on the gateway).
        StackPanel panel;
        panel.Spacing(8);
        panel.Width(320);
        TextBlock title;
        title.Text(I18n::Tr(I18n::S::AddServer));
        title.FontWeight(Windows::UI::Text::FontWeight{ 600 });
        TextBox box;
        box.PlaceholderText(I18n::Tr(I18n::S::JoinServerHint));
        Button join;
        join.Content(box_value(I18n::Tr(I18n::S::JoinServer)));
        join.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Microsoft::UI::Xaml::Style>());
        join.HorizontalAlignment(HorizontalAlignment::Stretch);
        TextBlock error;
        error.Foreground(SolidBrush(0xF23F42).as<Media::Brush>());
        error.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(title);
        panel.Children().Append(box);
        panel.Children().Append(join);
        panel.Children().Append(error);
        Flyout flyout;
        flyout.Content(panel);

        auto submit = [weak = get_weak(), box, error, flyout]() -> fire_and_forget
        {
            auto self = weak.get();
            if (!self || !self->m_rest) co_return;
            std::wstring code{ box.Text() };
            if (auto slash = code.find_last_of(L'/'); slash != std::wstring::npos) code = code.substr(slash + 1);
            if (auto query = code.find(L'?'); query != std::wstring::npos) code.resize(query);
            if (code.empty()) co_return;
            auto rest = self->m_rest;
            bool failed = false;
            try
            {
                co_await rest->PostJson(L"/invites/" + code, JsonObject{});
            }
            catch (...)
            {
                failed = true;
            }
            co_await wil::resume_foreground(self->m_dispatcher);
            if (!failed)
            {
                flyout.Hide();
                co_return;
            }
            // Discord may require a captcha for joins from other clients: hand over to the browser.
            Windows::System::Launcher::LaunchUriAsync(Windows::Foundation::Uri{ L"https://discord.gg/" + code });
            flyout.Hide();
        };
        join.Click([submit](auto&&, auto&&) { submit(); });
        box.KeyDown([submit](IInspectable const&, Input::KeyRoutedEventArgs const& e)
        {
            if (e.Key() == Windows::System::VirtualKey::Enter) submit();
        });
        flyout.ShowAt(sender.as<FrameworkElement>());
    }

    void MainWindow::OnDiscover(IInspectable const&, RoutedEventArgs const&)
    {
        Windows::System::Launcher::LaunchUriAsync(Windows::Foundation::Uri{ L"https://discord.com/discovery" });
    }

    void MainWindow::OnSettings(IInspectable const& sender, RoutedEventArgs const&)
    {
        MenuFlyout menu;
        MenuFlyoutSubItem language;
        language.Text(I18n::Tr(I18n::S::Language));
        FontIcon globe;
        globe.Glyph(L"");
        language.Icon(globe);
        for (int i = 0; i < static_cast<int>(I18n::Lang::Count); ++i)
        {
            auto lang = static_cast<I18n::Lang>(i);
            RadioMenuFlyoutItem item;
            item.Text(I18n::NativeName(lang));
            item.GroupName(L"lang");
            item.IsChecked(lang == I18n::Current());
            item.Click([weak = get_weak(), lang](auto&&, auto&&)
            {
                auto self = weak.get();
                if (!self) return;
                I18n::SetLanguage(lang);
                self->ApplyTexts();
                // Rebuild what carries translated text.
                self->RefreshGuildRail();
                self->RefreshChannelList();
                if (self->m_showingFriends) { self->RefreshFriends(); self->RefreshActiveNow(); }
                else if (!self->m_currentChannelId.empty())
                {
                    auto item = self->ChannelList().SelectedItem().try_as<DiscordWin3::ChannelItem>();
                    std::wstring channel = self->m_currentChannelId;
                    self->m_currentChannelId.clear();
                    self->LoadChannel(channel, item ? std::wstring{ item.Name() } : std::wstring{});
                }
                self->UpdateTitle();
            });
            language.Items().Append(item);
        }
        menu.Items().Append(language);

        MenuFlyoutSubItem theme;
        theme.Text(I18n::Tr(I18n::S::ThemeLabel));
        FontIcon palette;
        palette.Glyph(L"");
        theme.Icon(palette);
        for (int i = 0; i < static_cast<int>(::DiscordWin3::Theme::Kind::Count); ++i)
        {
            auto kind = static_cast<::DiscordWin3::Theme::Kind>(i);
            RadioMenuFlyoutItem item;
            item.Text(::DiscordWin3::Theme::Name(kind));
            item.GroupName(L"theme");
            item.IsChecked(kind == ::DiscordWin3::Theme::Current());
            item.Click([kind](auto&&, auto&&) { ::DiscordWin3::Theme::Set(kind); });
            theme.Items().Append(item);
        }
        menu.Items().Append(theme);
        menu.Items().Append(MenuFlyoutSeparator{});

        MenuFlyoutItem logout;
        logout.Text(I18n::Tr(I18n::S::Logout));
        FontIcon door;
        door.Glyph(L"");
        logout.Icon(door);
        logout.Foreground(SolidBrush(0xF23F43));
        logout.Click([weak = get_weak()](auto&&, auto&&)
        {
            if (auto self = weak.get()) self->OnLogout(nullptr, nullptr);
        });
        menu.Items().Append(logout);
        menu.ShowAt(sender.as<FrameworkElement>());
    }

    // ------------------------------------------------------------------ hover bar

    ::DiscordWin3::MessageData const* MainWindow::MessageFromSender(IInspectable const& sender)
    {
        auto element = sender.try_as<FrameworkElement>();
        auto item = element ? element.DataContext().try_as<DiscordWin3::MessageItem>() : nullptr;
        return item ? &Impl(item)->Data() : nullptr;
    }

    void MainWindow::OnHoverReaction(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto data = MessageFromSender(sender);
        auto emoji = unbox_value_or<hstring>(sender.as<Button>().Content(), L"");
        if (!data || emoji.empty()) return;
        ::DiscordWin3::Reaction reaction{ std::wstring{ emoji }, L"", 0, false };
        for (auto const& r : data->reactions)
        {
            if (r.id.empty() && r.name == reaction.name) reaction.me = r.me;
        }
        ToggleReaction(data->id, reaction);
    }

    void MainWindow::OnHoverPicker(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto data = MessageFromSender(sender);
        if (!data) return;
        auto messageId = data->id;
        auto existing = data->reactions;
        ShowEmojiPicker(sender.as<FrameworkElement>(), [weak = get_weak(), messageId, existing](::DiscordWin3::Reaction reaction)
        {
            auto self = weak.get();
            if (!self) return;
            for (auto const& r : existing)
            {
                if (r.name == reaction.name && r.id == reaction.id) reaction.me = r.me;
            }
            self->ToggleReaction(messageId, reaction);
        });
    }

    void MainWindow::OnHoverEdit(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (auto data = MessageFromSender(sender)) StartEdit(*data);
    }

    void MainWindow::OnHoverReply(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (auto data = MessageFromSender(sender)) StartReply(*data);
    }

    void MainWindow::OnHoverForward(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (auto data = MessageFromSender(sender)) ForwardMessage(*data);
    }

    void MainWindow::OnHoverMore(IInspectable const& sender, RoutedEventArgs const&)
    {
        // Same menu as right-click, anchored on the "..." button.
        auto element = sender.as<FrameworkElement>();
        Primitives::FlyoutShowOptions options;
        options.Placement(Primitives::FlyoutPlacementMode::BottomEdgeAlignedRight);
        m_messageMenu.ShowAt(element, options);
    }

    void MainWindow::RememberEmoji(std::wstring const& emoji)
    {
        std::erase(m_recentEmojis, emoji);
        m_recentEmojis.insert(m_recentEmojis.begin(), emoji);
        if (m_recentEmojis.size() > 3) m_recentEmojis.resize(3);
    }

    // ------------------------------------------------------------------ emoji picker

    void MainWindow::ShowEmojiPicker(FrameworkElement const& anchor, std::function<void(::DiscordWin3::Reaction const&)> onPick)
    {
        static constexpr wchar_t const* Common[] = {
            L"😀", L"😂", L"🤣", L"😊", L"😍", L"🥰", L"😘", L"😎", L"🤔", L"😐", L"🙄", L"😏", L"😮", L"😢", L"😭", L"😡",
            L"🥺", L"😴", L"🤯", L"🥳", L"😇", L"🤡", L"💀", L"👻", L"👍", L"👎", L"👏", L"🙏", L"💪", L"👀", L"🤝", L"👋",
            L"✌️", L"🤞", L"👌", L"🫡", L"❤️", L"🧡", L"💛", L"💚", L"💙", L"💜", L"🖤", L"💔", L"🔥", L"✨", L"⭐", L"🎉",
            L"💯", L"✅", L"❌", L"⚠️", L"❓", L"💤", L"🎮", L"🏆", L"⚽", L"🍕", L"🍔", L"☕", L"🍺", L"🐱", L"🐶", L"🦆",
        };

        Flyout flyout;
        StackPanel root;
        root.Width(9 * 40 + 16);
        root.Spacing(6);

        auto section = [&](std::wstring const& title)
        {
            TextBlock header;
            header.Text(title);
            header.FontSize(12);
            header.FontWeight(Windows::UI::Text::FontWeight{ 600 });
            header.Foreground(SolidBrush(0x949BA4));
            root.Children().Append(header);
            VariableSizedWrapGrid grid;
            grid.Orientation(Orientation::Horizontal);
            grid.MaximumRowsOrColumns(9);
            grid.ItemWidth(40);
            grid.ItemHeight(40);
            root.Children().Append(grid);
            return grid;
        };
        auto addButton = [&](VariableSizedWrapGrid const& grid, UIElement const& content, ::DiscordWin3::Reaction reaction, std::wstring const& tooltip)
        {
            Button b;
            b.Content(content);
            b.Width(38);
            b.Height(38);
            b.Padding({ 0, 0, 0, 0 });
            b.Background(SolidBrush(0, 0));
            b.BorderThickness({ 0, 0, 0, 0 });
            ToolTipService::SetToolTip(b, box_value(tooltip));
            b.Click([flyout, onPick, reaction](auto&&, auto&&)
            {
                flyout.Hide();
                onPick(reaction);
            });
            grid.Children().Append(b);
        };
        auto textEmoji = [](std::wstring const& e)
        {
            TextBlock t;
            t.Text(e);
            t.FontSize(22);
            t.HorizontalAlignment(HorizontalAlignment::Center);
            return t;
        };

        auto recent = section(I18n::Tr(I18n::S::FrequentEmojis));
        for (auto const& e : m_recentEmojis) addButton(recent, textEmoji(e), { e, L"", 0, false }, e);
        for (auto e : Common)
        {
            if (std::find(m_recentEmojis.begin(), m_recentEmojis.end(), e) == m_recentEmojis.end())
                addButton(recent, textEmoji(e), { e, L"", 0, false }, e);
        }

        if (auto guild = FindGuild(m_currentGuildId); guild && !guild->emojis.empty())
        {
            auto custom = section(I18n::Tr(I18n::S::ServerEmojis));
            size_t shown = 0;
            for (auto const& emoji : guild->emojis)
            {
                Image image;
                image.Width(28);
                image.Height(28);
                image.Source(::DiscordWin3::ImageCache::Get(std::wstring{ Discord::CdnBase } + L"/emojis/" + emoji.id + L".png?size=48", 28));
                addButton(custom, image, { emoji.name, emoji.id, 0, false }, L":" + emoji.name + L":");
                if (++shown >= 150) break;   // keep the flyout light
            }
        }

        ScrollViewer scroller;
        scroller.MaxHeight(420);
        scroller.Content(root);
        flyout.Content(scroller);
        flyout.ShowAt(anchor);
    }

    void MainWindow::OnComposerEmoji(IInspectable const& sender, RoutedEventArgs const&)
    {
        ShowEmojiPicker(sender.as<FrameworkElement>(), [weak = get_weak()](::DiscordWin3::Reaction const& r)
        {
            auto self = weak.get();
            if (!self) return;
            std::wstring insert = r.id.empty() ? r.name : L"<:" + r.name + L":" + r.id + L">";
            auto box = self->Composer();
            std::wstring text{ box.Text() };
            auto pos = std::min<size_t>(static_cast<size_t>(box.SelectionStart()), text.size());
            text.insert(pos, insert);
            box.Text(text);
            box.SelectionStart(static_cast<int32_t>(pos + insert.size()));
            box.Focus(FocusState::Programmatic);
            if (r.id.empty()) self->RememberEmoji(r.name);
        });
    }

    // ------------------------------------------------------------------ forwarding

    fire_and_forget MainWindow::ForwardMessage(MessageData data)
    {
        auto strong = get_strong();
        auto sourceChannel = m_currentChannelId;
        auto sourceGuild = m_currentGuildId;

        struct Target { std::wstring label, lower, channel; };
        auto targets = std::make_shared<std::vector<Target>>();
        auto lower = [](std::wstring s)
        {
            if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
            return s;
        };
        for (auto const& dm : m_dms) targets->push_back({ L"@ " + dm.name, lower(dm.name), dm.id });
        for (auto const& g : m_guilds)
        {
            for (auto const& c : g.channels)
            {
                if (IsTextLike(c.type) && g.perms.CanView(c.overwrites))
                    targets->push_back({ L"# " + c.name + L"   —   " + g.name, lower(c.name + L" " + g.name), c.id });
            }
        }

        AutoSuggestBox box;
        box.PlaceholderText(I18n::Tr(I18n::S::QuickSwitchPlaceholder));
        box.Width(460);
        auto matches = std::make_shared<std::vector<Target const*>>();
        auto chosen = std::make_shared<std::wstring>();

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(I18n::Tr(I18n::S::ForwardTitle)));
        dialog.Content(box);
        dialog.CloseButtonText(I18n::Tr(I18n::S::Cancel));

        box.TextChanged([targets, matches, lower](AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
        {
            if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput) return;
            auto query = lower(std::wstring{ sender.Text() });
            matches->clear();
            std::vector<IInspectable> labels;
            for (auto const& t : *targets)
            {
                if (query.empty() || t.lower.find(query) == std::wstring::npos) continue;
                matches->push_back(&t);
                labels.push_back(box_value(t.label));
                if (labels.size() >= 12) break;
            }
            sender.ItemsSource(single_threaded_vector(std::move(labels)));
        });
        auto choose = [matches, chosen, dialog](std::wstring const& label)
        {
            for (auto t : *matches)
            {
                if (t->label == label || label.empty())
                {
                    *chosen = t->channel;
                    dialog.Hide();
                    return;
                }
            }
        };
        box.SuggestionChosen([choose](AutoSuggestBox const&, AutoSuggestBoxSuggestionChosenEventArgs const& args)
        {
            choose(std::wstring{ unbox_value<hstring>(args.SelectedItem()) });
        });
        box.QuerySubmitted([choose](AutoSuggestBox const&, AutoSuggestBoxQuerySubmittedEventArgs const& args)
        {
            choose(args.ChosenSuggestion() ? std::wstring{ unbox_value<hstring>(args.ChosenSuggestion()) } : std::wstring{});
        });
        box.Loaded([](IInspectable const& sender, RoutedEventArgs const&)
        {
            sender.as<AutoSuggestBox>().Focus(FocusState::Programmatic);
        });

        co_await dialog.ShowAsync();
        if (chosen->empty() || !m_rest) co_return;

        // Message forwarding (2024): a reference of type 1 = FORWARD.
        JsonObject reference;
        reference.Insert(L"type", JsonValue::CreateNumberValue(1));
        reference.Insert(L"message_id", JsonValue::CreateStringValue(data.id));
        reference.Insert(L"channel_id", JsonValue::CreateStringValue(sourceChannel));
        if (sourceGuild != HomeId) reference.Insert(L"guild_id", JsonValue::CreateStringValue(sourceGuild));
        JsonObject body;
        body.Insert(L"message_reference", reference);
        body.Insert(L"nonce", JsonValue::CreateStringValue(NowNonce()));
        try
        {
            co_await m_rest->PostJson(L"/channels/" + *chosen + L"/messages", body);
        }
        catch (hresult_error const& e)
        {
            StatusText().Text(I18n::Fmt(I18n::S::SendFailed, std::wstring{ e.message() }.substr(0, 80)));
        }
    }

    // ------------------------------------------------------------------ attachments

    fire_and_forget MainWindow::StageFile(std::wstring path, std::wstring displayName)
    {
        auto strong = get_strong();
        PendingAttachment pending;
        pending.path = path;
        try
        {
            auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(path);
            auto props = co_await file.GetBasicPropertiesAsync();
            pending.size = props.Size();
            pending.filename = displayName.empty() ? std::wstring{ file.Name() } : displayName;
            std::wstring type{ file.ContentType() };
            pending.image = type.starts_with(L"image/");
        }
        catch (...)
        {
            co_return;
        }
        co_await wil::resume_foreground(m_dispatcher);
        if (m_pending.size() >= 10) co_return;   // Discord limit per message
        m_pending.push_back(std::move(pending));
        RenderPending();
    }

    void MainWindow::RenderPending()
    {
        PendingPanel().Children().Clear();
        PendingScroller().Visibility(Show(!m_pending.empty()));
        for (size_t i = 0; i < m_pending.size(); ++i)
        {
            auto const& p = m_pending[i];

            Grid card;
            card.Width(200);
            card.Height(200);
            card.CornerRadius({ 8, 8, 8, 8 });
            card.Background(SolidBrush(0x1A1A1E));
            card.Padding({ 8, 8, 8, 8 });
            RowDefinition r0, r1;
            r0.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            r1.Height(GridLengthHelper::Auto());
            card.RowDefinitions().Append(r0);
            card.RowDefinitions().Append(r1);

            if (p.image)
            {
                Image preview;
                preview.Stretch(Media::Stretch::Uniform);
                Media::Imaging::BitmapImage bitmap;
                bitmap.DecodePixelWidth(184);
                preview.Source(bitmap);
                [](Media::Imaging::BitmapImage bitmap, std::wstring path) -> fire_and_forget
                {
                    try
                    {
                        auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(path);
                        auto stream = co_await file.OpenReadAsync();
                        co_await bitmap.SetSourceAsync(stream);
                    }
                    catch (...)
                    {
                    }
                }(bitmap, p.path);
                card.Children().Append(preview);
            }
            else
            {
                FontIcon icon;
                icon.Glyph(L"");
                icon.FontSize(56);
                card.Children().Append(icon);
            }
            if (p.spoiler)
            {
                Border veil;
                veil.Background(SolidBrush(0x111214, 0xE6));
                veil.CornerRadius({ 6, 6, 6, 6 });
                TextBlock label;
                label.Text(L"SPOILER");
                label.FontWeight(Windows::UI::Text::FontWeight{ 700 });
                label.HorizontalAlignment(HorizontalAlignment::Center);
                label.VerticalAlignment(VerticalAlignment::Center);
                veil.Child(label);
                card.Children().Append(veil);
            }

            TextBlock name;
            name.Text(p.filename);
            name.FontSize(13);
            name.Margin({ 0, 6, 0, 0 });
            name.TextTrimming(TextTrimming::CharacterEllipsis);
            Grid::SetRow(name, 1);
            card.Children().Append(name);

            // Edit / remove buttons on the top-right corner, like the official client.
            StackPanel actions;
            actions.Orientation(Orientation::Horizontal);
            actions.HorizontalAlignment(HorizontalAlignment::Right);
            actions.VerticalAlignment(VerticalAlignment::Top);
            actions.Margin({ 0, -20, -16, 0 });
            actions.CornerRadius({ 6, 6, 6, 6 });
            actions.Background(SolidBrush(0x1E1F22));
            auto action = [&](wchar_t const* glyph, I18n::S tip, uint32_t color, std::function<void(MainWindow*)> run)
            {
                Button b;
                FontIcon icon;
                icon.Glyph(glyph);
                icon.FontSize(15);
                if (color) icon.Foreground(SolidBrush(color));
                b.Content(icon);
                b.Width(34);
                b.Height(32);
                b.Padding({ 0, 0, 0, 0 });
                b.Background(SolidBrush(0, 0));
                b.BorderThickness({ 0, 0, 0, 0 });
                ToolTipService::SetToolTip(b, box_value(I18n::Tr(tip)));
                b.Click([weak = get_weak(), run](auto&&, auto&&) { if (auto self = weak.get()) run(self.get()); });
                actions.Children().Append(b);
            };
            action(L"", I18n::S::EditAttachment, 0, [i](MainWindow* w) { w->EditPending(i); });
            action(L"", I18n::S::RemoveAttachment, 0xF23F43, [i](MainWindow* w)
            {
                if (i < w->m_pending.size()) w->m_pending.erase(w->m_pending.begin() + i);
                w->RenderPending();
            });
            card.Children().Append(actions);

            PendingPanel().Children().Append(card);
        }
    }

    fire_and_forget MainWindow::EditPending(size_t index)
    {
        auto strong = get_strong();
        if (index >= m_pending.size()) co_return;
        auto current = m_pending[index];

        StackPanel form;
        form.Spacing(8);
        form.Width(400);
        if (current.image)
        {
            Image preview;
            preview.MaxHeight(160);
            preview.Stretch(Media::Stretch::Uniform);
            Media::Imaging::BitmapImage bitmap;
            bitmap.DecodePixelWidth(400);
            preview.Source(bitmap);
            try
            {
                auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(current.path);
                co_await bitmap.SetSourceAsync(co_await file.OpenReadAsync());
            }
            catch (...)
            {
            }
            form.Children().Append(preview);
        }
        auto label = [&](I18n::S key)
        {
            TextBlock t;
            t.Text(I18n::Tr(key));
            t.FontWeight(Windows::UI::Text::FontWeight{ 600 });
            t.Margin({ 0, 8, 0, 0 });
            form.Children().Append(t);
        };
        label(I18n::S::FileName);
        TextBox name;
        name.Text(current.filename);
        form.Children().Append(name);
        label(I18n::S::AltText);
        TextBox description;
        description.PlaceholderText(I18n::Tr(I18n::S::AddDescription));
        description.Text(current.description);
        description.AcceptsReturn(true);
        description.TextWrapping(TextWrapping::Wrap);
        description.Height(80);
        form.Children().Append(description);
        CheckBox spoiler;
        spoiler.Content(box_value(I18n::Tr(I18n::S::MarkSpoiler)));
        spoiler.IsChecked(current.spoiler);
        spoiler.Margin({ 0, 8, 0, 0 });
        form.Children().Append(spoiler);

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(I18n::Tr(I18n::S::EditAttachment)));
        dialog.Content(form);
        dialog.PrimaryButtonText(I18n::Tr(I18n::S::Save));
        dialog.CloseButtonText(I18n::Tr(I18n::S::Cancel));
        dialog.DefaultButton(ContentDialogButton::Primary);
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary || index >= m_pending.size()) co_return;

        auto& p = m_pending[index];
        std::wstring newName{ name.Text() };
        if (!newName.empty()) p.filename = newName;
        p.description = description.Text();
        p.spoiler = spoiler.IsChecked() && spoiler.IsChecked().Value();
        RenderPending();
    }

    fire_and_forget MainWindow::SendWithAttachments(std::wstring text, std::wstring replyToId)
    {
        using namespace Windows::Web::Http;
        auto strong = get_strong();
        auto rest = m_rest;
        auto channelId = m_currentChannelId;
        auto files = std::move(m_pending);
        m_pending.clear();
        RenderPending();
        if (!rest || channelId.empty() || files.empty()) co_return;

        uint64_t total = 0;
        for (auto const& f : files) total += f.size;
        if (total > 20ull * 1024 * 1024)
        {
            StatusText().Text(I18n::Tr(I18n::S::FileTooBig));
            m_pending = std::move(files);
            RenderPending();
            co_return;
        }
        StatusText().Text(I18n::Fmt(I18n::S::Uploading, files.size() == 1 ? files[0].filename : std::to_wstring(files.size())));

        hstring error;
        try
        {
            JsonArray attachments;
            HttpMultipartFormDataContent form;
            for (size_t i = 0; i < files.size(); ++i)
            {
                auto const& f = files[i];
                std::wstring name = (f.spoiler && !f.filename.starts_with(L"SPOILER_") ? L"SPOILER_" : L"") + f.filename;
                JsonObject a;
                a.Insert(L"id", JsonValue::CreateStringValue(std::to_wstring(i)));
                a.Insert(L"filename", JsonValue::CreateStringValue(name));
                if (!f.description.empty()) a.Insert(L"description", JsonValue::CreateStringValue(f.description));
                attachments.Append(a);

                auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(f.path);
                HttpStreamContent content{ co_await file.OpenReadAsync() };
                auto type = file.ContentType().empty() ? hstring{ L"application/octet-stream" } : file.ContentType();
                content.Headers().ContentType(Headers::HttpMediaTypeHeaderValue{ type });
                form.Add(content, L"files[" + std::to_wstring(i) + L"]", name);
            }

            JsonObject payload;
            payload.Insert(L"content", JsonValue::CreateStringValue(text));
            payload.Insert(L"nonce", JsonValue::CreateStringValue(NowNonce()));
            payload.Insert(L"attachments", attachments);
            if (!replyToId.empty())
            {
                JsonObject reference;
                reference.Insert(L"message_id", JsonValue::CreateStringValue(replyToId));
                reference.Insert(L"channel_id", JsonValue::CreateStringValue(channelId));
                payload.Insert(L"message_reference", reference);
            }
            form.Add(HttpStringContent{ payload.Stringify(), Windows::Storage::Streams::UnicodeEncoding::Utf8, L"application/json" },
                     L"payload_json");
            co_await rest->PostContent(L"/channels/" + channelId + L"/messages", form);
        }
        catch (hresult_error const& e)
        {
            error = e.message();
        }
        co_await wil::resume_foreground(m_dispatcher);
        StatusText().Text(error.empty() ? std::wstring{} : I18n::Fmt(I18n::S::SendFailed, std::wstring{ error }.substr(0, 80)));
    }

    fire_and_forget MainWindow::OnComposerPaste(IInspectable const&, TextControlPasteEventArgs const& e)
    {
        using namespace Windows::ApplicationModel::DataTransfer;
        auto strong = get_strong();
        auto content = Clipboard::GetContent();
        if (content.Contains(StandardDataFormats::StorageItems()))
        {
            e.Handled(true);
            for (auto const& item : co_await content.GetStorageItemsAsync())
            {
                if (auto file = item.try_as<Windows::Storage::StorageFile>()) StageFile(std::wstring{ file.Path() });
            }
            co_return;
        }
        if (!content.Contains(StandardDataFormats::Bitmap())) co_return;

        // Screenshot in the clipboard -> image.png, like the official client.
        e.Handled(true);
        try
        {
            using namespace Windows::Graphics::Imaging;
            auto reference = co_await content.GetBitmapAsync();
            auto stream = co_await reference.OpenReadAsync();
            auto decoder = co_await BitmapDecoder::CreateAsync(stream);
            auto bitmap = co_await decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied);

            wchar_t temp[MAX_PATH]{};
            GetTempPathW(MAX_PATH, temp);
            auto folder = co_await Windows::Storage::StorageFolder::GetFolderFromPathAsync(temp);
            auto file = co_await folder.CreateFileAsync(L"discordwin3-paste.png", Windows::Storage::CreationCollisionOption::GenerateUniqueName);
            {
                auto output = co_await file.OpenAsync(Windows::Storage::FileAccessMode::ReadWrite);
                auto encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), output);
                encoder.SetSoftwareBitmap(bitmap);
                co_await encoder.FlushAsync();
            }
            StageFile(std::wstring{ file.Path() }, L"image.png");
        }
        catch (...)
        {
        }
    }

    void MainWindow::OnChatDragOver(IInspectable const&, DragEventArgs const& e)
    {
        using namespace Windows::ApplicationModel::DataTransfer;
        if (!m_currentChannelId.empty() && e.DataView().Contains(StandardDataFormats::StorageItems()))
        {
            e.AcceptedOperation(DataPackageOperation::Copy);
        }
    }

    fire_and_forget MainWindow::OnChatDrop(IInspectable const&, DragEventArgs const& e)
    {
        using namespace Windows::ApplicationModel::DataTransfer;
        auto strong = get_strong();
        if (m_currentChannelId.empty() || !e.DataView().Contains(StandardDataFormats::StorageItems())) co_return;
        auto deferral = e.GetDeferral();
        auto items = co_await e.DataView().GetStorageItemsAsync();
        deferral.Complete();
        for (auto const& item : items)
        {
            if (auto file = item.try_as<Windows::Storage::StorageFile>()) StageFile(std::wstring{ file.Path() });
        }
    }
}

namespace winrt::DiscordWin3::implementation
{
    // ------------------------------------------------------------------ media (video player, download)

    void MainWindow::OnPlayVideo(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto button = sender.as<Button>();
        auto data = MessageFromSender(sender);
        auto host = Media::VisualTreeHelper::GetParent(button).try_as<Grid>();
        if (!data || !host || data->mediaUrl.empty()) return;

        // Surface = player + Discord-style control bar (the stock transport controls look out of place).
        Grid surface;
        surface.Tag(box_value(L"video"));
        surface.Background(SolidBrush(0x000000));

        MediaPlayerElement player;
        player.Source(Windows::Media::Core::MediaSource::CreateFromUri(Uri{ data->mediaUrl }));
        player.AreTransportControlsEnabled(false);
        player.AutoPlay(true);
        player.Stretch(Media::Stretch::Uniform);
        surface.Children().Append(player);

        // Bottom bar on a dark gradient: play/pause, progress, time, mute, fullscreen.
        Grid bar;
        bar.VerticalAlignment(VerticalAlignment::Bottom);
        bar.Padding({ 8, 18, 8, 6 });
        bar.ColumnSpacing(6);
        Media::LinearGradientBrush shade;
        shade.StartPoint({ 0, 0 });
        shade.EndPoint({ 0, 1 });
        Media::GradientStop top, bottom;
        top.Color(Windows::UI::Color{ 0, 0, 0, 0 });
        top.Offset(0);
        bottom.Color(Windows::UI::Color{ 0xCC, 0, 0, 0 });
        bottom.Offset(1);
        shade.GradientStops().Append(top);
        shade.GradientStops().Append(bottom);
        bar.Background(shade);
        for (auto width : { GridLengthHelper::Auto(), GridLengthHelper::FromValueAndType(1, GridUnitType::Star),
                            GridLengthHelper::Auto(), GridLengthHelper::Auto(), GridLengthHelper::Auto() })
        {
            ColumnDefinition c;
            c.Width(width);
            bar.ColumnDefinitions().Append(c);
        }

        auto iconButton = [](wchar_t const* glyph, int column)
        {
            Button b;
            FontIcon icon;
            icon.Glyph(glyph);
            icon.FontSize(14);
            icon.Foreground(SolidBrush(0xFFFFFF));
            b.Content(icon);
            b.Width(30);
            b.Height(30);
            b.Padding({ 0, 0, 0, 0 });
            b.CornerRadius({ 6, 6, 6, 6 });
            b.Background(SolidBrush(0, 0));
            b.BorderThickness({ 0, 0, 0, 0 });
            Grid::SetColumn(b, column);
            return std::pair{ b, icon };
        };
        auto [playButton, playIcon] = iconButton(L"", 0);       // pause (it autoplays)
        auto [muteButton, muteIcon] = iconButton(L"", 3);
        auto [fullButton, fullIcon] = iconButton(L"", 4);

        Slider progress;
        progress.Minimum(0);
        progress.Maximum(1);
        progress.StepFrequency(0.1);
        progress.IsThumbToolTipEnabled(false);
        progress.VerticalAlignment(VerticalAlignment::Center);
        progress.Resources().Insert(box_value(L"SliderTrackValueFill"), SolidBrush(0x5865F2));
        progress.Resources().Insert(box_value(L"SliderTrackValueFillPointerOver"), SolidBrush(0x5865F2));
        progress.Resources().Insert(box_value(L"SliderTrackValueFillPressed"), SolidBrush(0x5865F2));
        progress.Resources().Insert(box_value(L"SliderTrackFill"), SolidBrush(0xFFFFFF, 0x4D));
        progress.Resources().Insert(box_value(L"SliderThumbBackground"), SolidBrush(0xFFFFFF));
        Grid::SetColumn(progress, 1);

        TextBlock time;
        time.FontSize(12);
        time.Foreground(SolidBrush(0xFFFFFF));
        time.VerticalAlignment(VerticalAlignment::Center);
        time.Text(L"0:00");
        Grid::SetColumn(time, 2);

        bar.Children().Append(playButton);
        bar.Children().Append(progress);
        bar.Children().Append(time);
        bar.Children().Append(muteButton);
        bar.Children().Append(fullButton);
        surface.Children().Append(bar);

        auto session = player.MediaPlayer().PlaybackSession();
        auto updating = std::make_shared<bool>(false);
        auto format = [](double seconds)
        {
            int s = static_cast<int>(seconds);
            wchar_t buf[16];
            swprintf_s(buf, L"%d:%02d", s / 60, s % 60);
            return std::wstring{ buf };
        };

        // 4 Hz UI refresh while the surface lives; stops itself once the row recycled it.
        auto timer = m_dispatcher.CreateTimer();
        timer.Interval(std::chrono::milliseconds(250));
        weak_ref<Grid> weakSurface{ surface };
        timer.Tick([weakSurface, session, progress, time, playIcon, updating, format](Microsoft::UI::Dispatching::DispatcherQueueTimer const& t, auto&&)
        {
            auto s = weakSurface.get();
            if (!s || !Media::VisualTreeHelper::GetParent(s))
            {
                t.Stop();
                return;
            }
            double position = std::chrono::duration<double>(session.Position()).count();
            double duration = std::chrono::duration<double>(session.NaturalDuration()).count();
            *updating = true;
            if (duration > 0) progress.Maximum(duration);
            progress.Value(position);
            *updating = false;
            time.Text(format(position) + L" / " + format(duration));
            bool playing = session.PlaybackState() == Windows::Media::Playback::MediaPlaybackState::Playing;
            playIcon.Glyph(playing ? L"" : L"");
        });
        timer.Start();

        progress.ValueChanged([session, updating](auto&&, Primitives::RangeBaseValueChangedEventArgs const& e)
        {
            if (!*updating)
                session.Position(std::chrono::duration_cast<TimeSpan>(std::chrono::duration<double>(e.NewValue())));
        });
        playButton.Click([player](auto&&, auto&&)
        {
            auto mp = player.MediaPlayer();
            if (mp.PlaybackSession().PlaybackState() == Windows::Media::Playback::MediaPlaybackState::Playing) mp.Pause();
            else mp.Play();
        });
        muteButton.Click([player, muteIcon](auto&&, auto&&)
        {
            auto mp = player.MediaPlayer();
            mp.IsMuted(!mp.IsMuted());
            muteIcon.Glyph(mp.IsMuted() ? L"" : L"");
        });

        // Fullscreen: move the surface to the window-wide layer and switch the window presenter.
        fullButton.Click([weak = get_weak(), weakSurface, fullIcon](auto&&, auto&&)
        {
            auto self = weak.get();
            auto s = weakSurface.get();
            if (!self || !s) return;
            using Microsoft::UI::Windowing::AppWindowPresenterKind;
            auto layer = self->FullscreenHost();
            if (layer.Visibility() == Visibility::Collapsed)
            {
                self->m_videoHome = Media::VisualTreeHelper::GetParent(s).try_as<Panel>();
                if (self->m_videoHome)
                {
                    uint32_t index;
                    if (self->m_videoHome.Children().IndexOf(s, index)) self->m_videoHome.Children().RemoveAt(index);
                }
                layer.Children().Append(s);
                layer.Visibility(Visibility::Visible);
                self->AppWindow().SetPresenter(AppWindowPresenterKind::FullScreen);
                fullIcon.Glyph(L"");
            }
            else
            {
                layer.Children().Clear();
                layer.Visibility(Visibility::Collapsed);
                if (self->m_videoHome) self->m_videoHome.Children().Append(s);
                self->m_videoHome = nullptr;
                self->AppWindow().SetPresenter(AppWindowPresenterKind::Overlapped);
                fullIcon.Glyph(L"");
            }
        });

        // Controls fade in on hover (always visible while paused).
        bar.Opacity(1);
        surface.PointerEntered([bar](auto&&, auto&&) { bar.Opacity(1); });
        surface.PointerExited([bar, session](auto&&, auto&&)
        {
            if (session.PlaybackState() == Windows::Media::Playback::MediaPlaybackState::Playing) bar.Opacity(0);
        });
        if (!m_reduceMotion)
        {
            ScalarTransition fade;
            fade.Duration(std::chrono::milliseconds(150));
            bar.OpacityTransition(fade);
        }

        host.Children().Append(surface);
        button.Visibility(Visibility::Collapsed);
    }

    fire_and_forget MainWindow::OnDownloadMedia(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        auto data = MessageFromSender(sender);
        if (!data) co_return;
        std::wstring url = data->mediaUrl.empty() ? data->imageUrl : data->mediaUrl;
        if (url.empty()) co_return;

        // File name = last path segment, without the signed query string.
        std::wstring name = url.substr(0, url.find(L'?'));
        name = name.substr(name.find_last_of(L'/') + 1);
        auto dot = name.find_last_of(L'.');
        std::wstring extension = dot == std::wstring::npos ? L".bin" : name.substr(dot);

        Microsoft::Windows::Storage::Pickers::FileSavePicker picker{ AppWindow().Id() };
        picker.SuggestedFileName(dot == std::wstring::npos ? name : name.substr(0, dot));
        picker.FileTypeChoices().Insert(extension, single_threaded_vector<hstring>({ hstring{ extension } }));
        auto result = co_await picker.PickSaveFileAsync();
        if (!result) co_return;
        std::wstring path{ result.Path() };

        try
        {
            // Streamed to disk in 64 KB chunks: a 20 MB video never sits in RAM.
            Windows::Web::Http::HttpClient client;
            auto input = co_await client.GetInputStreamAsync(Uri{ url });
            std::ofstream out{ std::filesystem::path{ path }, std::ios::binary | std::ios::trunc };
            Windows::Storage::Streams::Buffer buffer{ 64 * 1024 };
            for (;;)
            {
                auto chunk = co_await input.ReadAsync(buffer, buffer.Capacity(), Windows::Storage::Streams::InputStreamOptions::Partial);
                if (chunk.Length() == 0) break;
                out.write(reinterpret_cast<char const*>(chunk.data()), chunk.Length());
            }
        }
        catch (...)
        {
        }
    }
}

namespace winrt::DiscordWin3::implementation
{
    // ------------------------------------------------------------------ voice

    void MainWindow::SendVoiceState()
    {
        if (!m_gateway) return;
        JsonObject d;
        d.Insert(L"guild_id", m_voiceGuild.empty() ? JsonValue::CreateNullValue() : JsonValue::CreateStringValue(m_voiceGuild));
        d.Insert(L"channel_id", m_voiceChannel.empty() ? JsonValue::CreateNullValue() : JsonValue::CreateStringValue(m_voiceChannel));
        d.Insert(L"self_mute", JsonValue::CreateBooleanValue(m_selfMute || m_selfDeaf));
        d.Insert(L"self_deaf", JsonValue::CreateBooleanValue(m_selfDeaf));
        d.Insert(L"self_video", JsonValue::CreateBooleanValue(false));
        m_gateway->SendOp(4, d);
    }

    void MainWindow::JoinVoice(std::wstring guildId, std::wstring channelId)
    {
        if (channelId.empty() || (channelId == m_voiceChannel && m_voice)) return;
        if (!m_voiceChannel.empty()) LeaveVoice();

        m_voiceGuild = std::move(guildId);
        m_voiceChannel = std::move(channelId);
        m_voiceSession.clear();
        m_voiceToken.clear();
        m_voiceEndpoint.clear();
        SendVoiceState();

        auto name = m_channelNames.find(m_voiceChannel);
        auto guild = FindGuild(m_voiceGuild);
        VoiceChannelText().Text((name != m_channelNames.end() ? name->second : std::wstring{}) +
                                (guild ? L" / " + guild->name : std::wstring{}));
        VoiceStatusText().Text(I18n::Tr(I18n::S::VoiceConnecting));
        VoiceStatusText().Foreground(SolidBrush(0xF0B232));
        VoicePanel().Visibility(Visibility::Visible);
        ShowCallView(true);
    }

    void MainWindow::LeaveVoice()
    {
        StopScreenShare(true);
        if (m_voiceConnected && !m_selfDeaf) Voice::Play(Voice::Sound::SelfLeave);
        m_voiceConnected = false;
        if (m_voice)
        {
            m_voice->Stop();
            m_voice.reset();
        }
        if (!m_voiceChannel.empty())
        {
            m_voiceChannel.clear();
            SendVoiceState();   // channel_id: null = disconnect
        }
        m_voiceGuild.clear();
        m_voiceSession.clear();
        m_voiceToken.clear();
        m_voiceEndpoint.clear();
        auto speaking = std::move(m_speakingUsers);
        m_speakingUsers.clear();
        for (auto const& user : speaking) UpdateVoiceUserRow(user);
        VoicePanel().Visibility(Visibility::Collapsed);
        ShowCallView(false);
        m_dmCallUsers.clear();
    }

    void MainWindow::OnOwnVoiceState(Slim::Value const& d)
    {
        auto channel = Json::Str(d, L"channel_id");
        if (channel.empty())
        {
            // Disconnected by a moderator or from another device.
            StopScreenShare(false);
            if (m_voiceConnected) Voice::Play(Voice::Sound::SelfLeave);
            m_voiceConnected = false;
            if (m_voice) { m_voice->Stop(); m_voice.reset(); }
            m_voiceChannel.clear();
            VoicePanel().Visibility(Visibility::Collapsed);
            return;
        }
        if (channel != m_voiceChannel) return;   // a call on another device
        m_voiceSession = Json::Str(d, L"session_id");
        TryStartVoice();
    }

    void MainWindow::OnVoiceServerUpdate(Slim::Value const& d)
    {
        if (m_voiceChannel.empty()) return;
        // Server moved (region change / failover): reconnect to the new one.
        if (m_voice)
        {
            m_voice->Stop();
            m_voice.reset();
        }
        m_voiceToken = Json::Str(d, L"token");
        m_voiceEndpoint = Json::Str(d, L"endpoint");   // null while Discord allocates a server
        TryStartVoice();
    }

    void MainWindow::TryStartVoice()
    {
        if (m_voice || m_voiceChannel.empty() || m_voiceSession.empty() || m_voiceToken.empty() || m_voiceEndpoint.empty()) return;

        Voice::VoiceParams params;
        params.serverId = m_voiceGuild.empty() ? m_voiceChannel : m_voiceGuild;
        params.channelId = m_voiceChannel;
        params.userId = m_selfId;
        params.sessionId = m_voiceSession;
        params.token = m_voiceToken;
        params.endpoint = m_voiceEndpoint;

        auto weak = get_weak();
        auto dq = m_dispatcher;
        Voice::VoiceConnection::Callbacks cb;
        cb.onState = [weak, dq](Voice::VoiceConnection::State state, std::wstring const& detail)
        {
            dq.TryEnqueue([weak, state, detail]()
            {
                auto self = weak.get();
                if (!self || self->m_voiceChannel.empty()) return;
                using State = Voice::VoiceConnection::State;
                if (state == State::Connected)
                {
                    if (!self->m_voiceConnected && !self->m_selfDeaf) Voice::Play(Voice::Sound::SelfJoin);
                    self->m_voiceConnected = true;
                    self->VoiceStatusText().Text(I18n::Tr(I18n::S::VoiceConnected));
                    self->VoiceStatusText().Foreground(SolidBrush(0x23A55A));
                }
                else if (state == State::Failed)
                {
                    self->VoiceStatusText().Text(I18n::Fmt(I18n::S::VoiceFailed, detail));
                    self->VoiceStatusText().Foreground(SolidBrush(0xF23F43));
                    if (self->m_voice) { self->m_voice->Stop(); self->m_voice.reset(); }
                }
            });
        };
        cb.onSpeaking = [weak, dq](std::wstring const& userId, bool speaking)
        {
            dq.TryEnqueue([weak, userId, speaking]()
            {
                auto self = weak.get();
                if (!self) return;
                if (speaking) self->m_speakingUsers.insert(userId);
                else self->m_speakingUsers.erase(userId);
                self->UpdateVoiceUserRow(userId);
            });
        };

        m_voice = std::make_shared<Voice::VoiceConnection>(std::move(params), std::move(cb));
        m_voice->SetMuted(m_selfMute || m_selfDeaf);
        m_voice->SetDeafened(m_selfDeaf);
        m_voice->Start();
    }

    void MainWindow::UpdateVoiceUserRow(std::wstring const& userId)
    {
        // Call screen tile (green ring).
        for (uint32_t i = 0; i < m_callItems.Size(); ++i)
        {
            auto tile = get_self<implementation::ParticipantItem>(m_callItems.GetAt(i).as<DiscordWin3::ParticipantItem>());
            if (std::wstring{ tile->UserId() } != userId) continue;
            m_callItems.SetAt(i, make<ParticipantItem>(userId, tile->NameText(), tile->AvatarUrl(),
                                                       m_speakingUsers.contains(userId), tile->Muted(), tile->Deaf()));
            break;
        }
        std::wstring id = L"voice:" + userId;
        for (uint32_t i = 0; i < m_channelItems.Size(); ++i)
        {
            auto existing = get_self<implementation::ChannelItem>(m_channelItems.GetAt(i).as<DiscordWin3::ChannelItem>());
            if (existing->Id() != id) continue;
            auto item = make_self<ChannelItem>(existing->Id(), existing->Name(), existing->Glyph(), existing->Kind(), existing->AvatarUrl());
            item->SetSpeaking(m_speakingUsers.contains(userId));
            m_channelItems.SetAt(i, item.as<IInspectable>());
            return;
        }
    }

    void MainWindow::UpdateVoiceButtons()
    {
        MuteIcon().Foreground(m_selfMute || m_selfDeaf ? SolidBrush(0xF23F43) : SolidBrush(0xDBDEE1));
        DeafenIcon().Foreground(m_selfDeaf ? SolidBrush(0xF23F43) : SolidBrush(0xDBDEE1));
        CallMuteIcon().Foreground(MuteIcon().Foreground());
        CallDeafenIcon().Foreground(DeafenIcon().Foreground());
        ToolTipService::SetToolTip(CallMuteButton(), box_value(I18n::Tr(m_selfMute ? I18n::S::UnmuteMic : I18n::S::MuteMic)));
        ToolTipService::SetToolTip(CallDeafenButton(), box_value(I18n::Tr(m_selfDeaf ? I18n::S::Undeafen : I18n::S::Deafen)));
        ToolTipService::SetToolTip(CallShareButton(), box_value(I18n::Tr(I18n::S::ShareScreen)));
        ToolTipService::SetToolTip(CallStatsButton(), box_value(I18n::Tr(I18n::S::StatsForNerds)));
        ToolTipService::SetToolTip(CallLeaveButton(), box_value(I18n::Tr(I18n::S::VoiceDisconnect)));
        RefreshCallParticipants();   // own mute / deaf icons
        ToolTipService::SetToolTip(MuteButton(), box_value(I18n::Tr(m_selfMute ? I18n::S::UnmuteMic : I18n::S::MuteMic)));
        ToolTipService::SetToolTip(DeafenButton(), box_value(I18n::Tr(m_selfDeaf ? I18n::S::Undeafen : I18n::S::Deafen)));
        ToolTipService::SetToolTip(VoiceDisconnectButton(), box_value(I18n::Tr(I18n::S::VoiceDisconnect)));
        ToolTipService::SetToolTip(CallButton(), box_value(I18n::Tr(I18n::S::StartCall)));
    }

    void MainWindow::OnToggleMute(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_selfDeaf) m_selfDeaf = false;   // like Discord: unmuting also undeafens
        m_selfMute = !m_selfMute;
        if (!m_voiceChannel.empty()) Voice::Play(m_selfMute ? Voice::Sound::Mute : Voice::Sound::Unmute);
        if (m_voice) { m_voice->SetMuted(m_selfMute || m_selfDeaf); m_voice->SetDeafened(m_selfDeaf); }
        if (!m_voiceChannel.empty()) SendVoiceState();
        UpdateVoiceButtons();
    }

    void MainWindow::OnToggleDeafen(IInspectable const&, RoutedEventArgs const&)
    {
        m_selfDeaf = !m_selfDeaf;
        if (!m_voiceChannel.empty()) Voice::Play(m_selfDeaf ? Voice::Sound::Deafen : Voice::Sound::Undeafen);
        if (m_voice) { m_voice->SetMuted(m_selfMute || m_selfDeaf); m_voice->SetDeafened(m_selfDeaf); }
        if (!m_voiceChannel.empty()) SendVoiceState();
        UpdateVoiceButtons();
    }

    void MainWindow::OnVoiceDisconnect(IInspectable const&, RoutedEventArgs const&)
    {
        LeaveVoice();
    }

    void MainWindow::OnStartCall(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_currentGuildId != HomeId || m_currentChannelId.empty()) return;
        auto channel = m_currentChannelId;
        JoinVoice(L"", channel);
        // Ring the other side(s), like pressing the phone icon in the official client.
        if (m_rest)
        {
            JsonObject body;
            body.Insert(L"recipients", JsonValue::CreateNullValue());
            m_rest->PostJson(L"/channels/" + channel + L"/call/ring", body);
        }
    }
}

namespace winrt::DiscordWin3::implementation
{
    // ------------------------------------------------------------------ call screen

    void MainWindow::ShowCallView(bool show)
    {
        m_showingCall = show && !m_voiceChannel.empty();
        CallView().Visibility(Show(m_showingCall));
        if (!m_showingCall)
        {
            m_callItems.Clear();   // tiles hold large avatars: drop them when hidden
            if (m_statsTimer) m_statsTimer.Stop();
            return;
        }
        if (m_showingFriends) ShowFriends(false);
        auto name = m_channelNames.find(m_voiceChannel);
        CallTitle().Text(name != m_channelNames.end() ? name->second : std::wstring{});
        TitleText().Text(CallTitle().Text());
        RefreshCallParticipants();
        if (CallStatsButton().IsChecked().Value())
        {
            UpdateStatsText();
            m_statsTimer.Start();
        }
    }

    void MainWindow::OnOpenCallView(IInspectable const&, Input::TappedRoutedEventArgs const&)
    {
        ShowCallView(true);
    }

    void MainWindow::RefreshCallParticipants()
    {
        if (!m_showingCall) return;

        std::vector<std::wstring> users;
        if (auto guild = FindGuild(m_voiceGuild))
        {
            for (auto const& [userId, channelId] : guild->voice)
            {
                if (channelId == m_voiceChannel) users.push_back(userId);
            }
        }
        else
        {
            users.assign(m_dmCallUsers.begin(), m_dmCallUsers.end());
        }
        if (std::find(users.begin(), users.end(), m_selfId) == users.end()) users.insert(users.begin(), m_selfId);

        auto& members = m_members[m_voiceGuild];
        std::vector<IInspectable> tiles;
        for (auto const& id : users)
        {
            auto user = m_users.find(id);
            auto member = members.find(id);
            std::wstring name = member != members.end() && !member->second.nick.empty() ? member->second.nick
                : user != m_users.end() ? user->second.name : id;
            std::wstring avatar = member != members.end() && !member->second.avatarUrl.empty() ? member->second.avatarUrl
                : user != m_users.end() ? user->second.avatarUrl : DefaultAvatar(id, L"0");
            auto flags = m_voiceFlags.find(id);
            bool muted = id == m_selfId ? (m_selfMute || m_selfDeaf) : (flags != m_voiceFlags.end() && flags->second.first);
            bool deaf = id == m_selfId ? m_selfDeaf : (flags != m_voiceFlags.end() && flags->second.second);
            tiles.push_back(make<ParticipantItem>(id, name, avatar, m_speakingUsers.contains(id), muted, deaf));
        }
        m_callItems.ReplaceAll(tiles);
    }

    void MainWindow::OnToggleStats(IInspectable const&, RoutedEventArgs const&)
    {
        bool on = CallStatsButton().IsChecked().Value();
        StatsPanel().Visibility(Show(on));
        if (on)
        {
            UpdateStatsText();
            m_statsTimer.Start();
        }
        else
        {
            m_statsTimer.Stop();
        }
    }

    void MainWindow::UpdateStatsText()
    {
        StatsTitle().Text(I18n::Tr(I18n::S::StatsForNerds));
        if (!m_voice)
        {
            StatsText().Text(L"state         : " + std::wstring{ m_voiceChannel.empty() ? L"idle" : L"waiting for voice server" } +
                             L"\nsession       : " + (m_voiceSession.empty() ? L"-" : L"ok") +
                             L"\nvoice server  : " + (m_voiceEndpoint.empty() ? L"-" : m_voiceEndpoint));
            return;
        }
        auto s = m_voice->GetStats();
        auto kb = [](uint64_t bytes)
        {
            wchar_t buf[32];
            swprintf_s(buf, L"%.1f KB", bytes / 1024.0);
            return std::wstring{ buf };
        };
        uint64_t expected = s.packetsReceived + s.packetsLost;
        wchar_t loss[32];
        swprintf_s(loss, L"%.2f %%", expected ? 100.0 * s.packetsLost / expected : 0.0);

        PROCESS_MEMORY_COUNTERS_EX memory{ sizeof(memory) };
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));

        using State = Voice::VoiceConnection::State;
        std::wstring text;
        text += L"state         : " + std::wstring{ s.state == State::Connected ? L"connected" : s.state == State::Failed ? L"failed" : L"connecting" };
        text += L"\nendpoint      : " + s.endpoint;
        text += L"\nping (rtt)    : " + (s.rttMs >= 0 ? std::to_wstring(s.rttMs) + L" ms" : std::wstring{ L"-" });
        text += L"\ntransport     : " + s.mode;
        text += L"\ne2ee (DAVE)   : " + (s.daveVersion == 0 ? std::wstring{ L"off" }
                                        : L"v" + std::to_wstring(s.daveVersion) + (s.daveReady ? L" - keys ready" : L" - waiting for MLS keys"));
        text += L"\ncodec         : opus 48 kHz stereo 64 kbps";
        text += L"\nssrc          : " + std::to_wstring(s.ssrc);
        text += L"\nparticipants  : " + std::to_wstring(s.participants);
        text += L"\nsent          : " + std::to_wstring(s.packetsSent) + L" pkts / " + kb(s.bytesSent);
        text += L"\nreceived      : " + std::to_wstring(s.packetsReceived) + L" pkts / " + kb(s.bytesReceived);
        text += L"\nlost          : " + std::to_wstring(s.packetsLost) + L" (" + loss + L")";
        text += L"\nauth failures : " + std::to_wstring(s.transportFailures);
        text += L"\ne2ee failures : decrypt " + std::to_wstring(s.e2eeDecryptFailures) + L" / encrypt skipped " + std::to_wstring(s.e2eeEncryptSkipped);
        text += L"\nmic           : " + std::wstring{ m_selfDeaf ? L"deafened" : m_selfMute ? L"muted" : s.speaking ? L"speaking" : L"silent" };
        text += L"\napp memory    : " + std::to_wstring(memory.PrivateUsage >> 20) + L" MB private";
        StatsText().Text(text);
    }

}

namespace winrt::DiscordWin3::implementation
{
    // ------------------------------------------------------------------ Go Live (screen share)

    void MainWindow::OnShareScreen(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_screen || !m_streamKey.empty())
        {
            StopScreenShare(true);
            return;
        }
        StartScreenShare();
    }

    fire_and_forget MainWindow::StartScreenShare()
    {
        auto strong = get_strong();
        if (m_voiceChannel.empty() || !m_voice || !m_gateway)
        {
            StatusText().Text(I18n::Tr(I18n::S::ShareNeedsVoice));
            co_return;
        }

        // System picker: whole screens and individual windows, like Discord's source selector.
        auto hwnd = Microsoft::UI::GetWindowFromWindowId(AppWindow().Id());
        Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
        try
        {
            item = co_await Voice::ScreenShare::PickAsync(hwnd);
        }
        catch (...)
        {
        }
        co_await wil::resume_foreground(m_dispatcher);
        if (!item || m_voiceChannel.empty()) co_return;

        m_captureItem = item;
        bool guild = !m_voiceGuild.empty();
        m_streamKey = guild ? L"guild:" + m_voiceGuild + L":" + m_voiceChannel + L":" + m_selfId
                            : L"call:" + m_voiceChannel + L":" + m_selfId;
        m_streamServerId.clear();
        m_streamToken.clear();
        m_streamEndpoint.clear();

        JsonObject create;
        create.Insert(L"type", JsonValue::CreateStringValue(guild ? L"guild" : L"call"));
        create.Insert(L"guild_id", guild ? JsonValue::CreateStringValue(m_voiceGuild) : JsonValue::CreateNullValue());
        create.Insert(L"channel_id", JsonValue::CreateStringValue(m_voiceChannel));
        create.Insert(L"preferred_region", JsonValue::CreateNullValue());
        m_gateway->SendOp(18, create);

        JsonObject unpause;
        unpause.Insert(L"stream_key", JsonValue::CreateStringValue(m_streamKey));
        unpause.Insert(L"paused", JsonValue::CreateBooleanValue(false));
        m_gateway->SendOp(22, unpause);

        CallShareButton().Background(SolidBrush(0x5865F2));
        ToolTipService::SetToolTip(CallShareButton(), box_value(I18n::Tr(I18n::S::StopSharing)));
    }

    void MainWindow::OnStreamEvent(std::wstring const& type, Slim::Value const& d)
    {
        auto key = Json::Str(d, L"stream_key");
        if (key.empty() || key != m_streamKey) return;   // someone else's stream (watching comes later)

        if (type == L"STREAM_CREATE")
        {
            m_streamServerId = Json::Str(d, L"rtc_server_id");
            TryStartStream();
        }
        else if (type == L"STREAM_SERVER_UPDATE")
        {
            m_streamToken = Json::Str(d, L"token");
            m_streamEndpoint = Json::Str(d, L"endpoint");
            TryStartStream();
        }
        else if (type == L"STREAM_DELETE")
        {
            StopScreenShare(false);   // ended by Discord (moderator, server change...)
        }
    }

    void MainWindow::TryStartStream()
    {
        if (m_stream || m_streamServerId.empty() || m_streamToken.empty() || m_streamEndpoint.empty() || !m_captureItem) return;

        Voice::VoiceParams params;
        params.serverId = m_streamServerId;
        params.channelId = m_voiceChannel;
        params.userId = m_selfId;
        params.sessionId = m_voiceSession;
        params.token = m_streamToken;
        params.endpoint = m_streamEndpoint;
        params.stream = true;
        params.daveGroupId = std::to_wstring(Json::U64(m_streamServerId) - 1);   // stream MLS group = rtc_server_id - 1

        auto weak = get_weak();
        auto dq = m_dispatcher;
        Voice::VoiceConnection::Callbacks cb;
        cb.onState = [weak, dq](Voice::VoiceConnection::State state, std::wstring const& detail)
        {
            dq.TryEnqueue([weak, state, detail]()
            {
                auto self = weak.get();
                if (!self || !self->m_stream) return;
                if (state == Voice::VoiceConnection::State::Connected)
                {
                    // Stream transport is up: start capturing + encoding into it.
                    std::weak_ptr<Voice::VoiceConnection> stream = self->m_stream;
                    self->m_screen = std::make_unique<Voice::ScreenShare>();
                    bool ok = self->m_screen->Start(self->m_captureItem, [stream](uint8_t const* data, size_t length, uint32_t timestamp)
                    {
                        if (auto s = stream.lock()) s->SendVideoFrame(data, length, timestamp);
                    });
                    if (!ok)
                    {
                        self->StatusText().Text(I18n::Fmt(I18n::S::VoiceFailed, self->m_screen->Error()));
                        self->StopScreenShare(true);
                        return;
                    }
                    if (!self->m_selfDeaf) Voice::Play(Voice::Sound::StreamStart);
                    self->CallTitle().Text(self->CallTitle().Text() + L"  •  " + I18n::Tr(I18n::S::Live));
                }
                else if (state == Voice::VoiceConnection::State::Failed)
                {
                    self->StatusText().Text(I18n::Fmt(I18n::S::VoiceFailed, detail));
                    self->StopScreenShare(true);
                }
            });
        };
        m_stream = std::make_shared<Voice::VoiceConnection>(std::move(params), std::move(cb));
        m_stream->Start();
    }

    void MainWindow::StopScreenShare(bool notifyServer)
    {
        bool wasLive = m_screen && m_screen->Running();
        if (m_screen)
        {
            m_screen->Stop();
            m_screen.reset();
        }
        if (m_stream)
        {
            m_stream->Stop();
            m_stream.reset();
        }
        if (notifyServer && !m_streamKey.empty() && m_gateway)
        {
            JsonObject del;
            del.Insert(L"stream_key", JsonValue::CreateStringValue(m_streamKey));
            m_gateway->SendOp(19, del);
        }
        m_streamKey.clear();
        m_streamServerId.clear();
        m_streamToken.clear();
        m_streamEndpoint.clear();
        m_captureItem = nullptr;
        if (wasLive && !m_selfDeaf) Voice::Play(Voice::Sound::StreamStop);
        CallShareButton().Background(SolidBrush(0x2B2D31));
        ToolTipService::SetToolTip(CallShareButton(), box_value(I18n::Tr(I18n::S::ShareScreen)));
        if (m_showingCall)
        {
            auto name = m_channelNames.find(m_voiceChannel);
            CallTitle().Text(name != m_channelNames.end() ? name->second : std::wstring{});
        }
    }
}
