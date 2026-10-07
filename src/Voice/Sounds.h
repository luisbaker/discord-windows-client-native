#pragma once

namespace DiscordWin3::Voice
{
    // Short original UI chimes for voice events, synthesized once (no audio assets shipped).
    enum class Sound
    {
        SelfJoin,      // you connected
        SelfLeave,     // you disconnected
        UserJoin,      // someone joined your channel
        UserLeave,     // someone left your channel
        Mute,
        Unmute,
        Deafen,
        Undeafen,
        StreamStart,
        StreamStop,
    };

    void Play(Sound sound);
}
