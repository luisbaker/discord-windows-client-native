#pragma once

// Discord 2025 colour themes (values resolved from the official design tokens).
// The app brushes in App.xaml are recoloured in place, so every {StaticResource} follows.
namespace DiscordWin3::Theme
{
    enum class Kind { Dark, Darker, Midnight, Count };

    void Initialize();          // saved choice (default Midnight), applied to the app resources
    void Set(Kind kind);        // applies + persists
    Kind Current();
    wchar_t const* Name(Kind kind);
}
