#pragma once

// Discord's UI font (gg sans). It is not shipped with this project: like a browser on discord.com,
// the app fetches the public web font files from Discord's servers on first run, unpacks the WOFF2
// files with DirectWrite and keeps one local collection next to the exe (Fonts/ggsans.ttc, never committed).
namespace DiscordWin3::Fonts
{
    // Call before the first window is built: uses the cached collection when present (applied to the
    // app-wide font resources), otherwise downloads it in the background for the next start.
    void Initialize();
}
