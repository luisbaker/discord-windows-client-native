#include "pch.h"
#include "App.xaml.h"
#include "Theme.h"
#include "Fonts.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::DiscordWin3::implementation
{
    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        ::DiscordWin3::Theme::Initialize();   // recolour shared brushes before the first page is built
        ::DiscordWin3::Fonts::Initialize();    // gg sans (fetched once from discord.com, cached locally)
        m_window = make<MainWindow>();
        m_window.Activate();
    }
}
