#pragma once

namespace DiscordWin3::Discord
{
    inline constexpr uint64_t PermAdministrator = 1ull << 3;
    inline constexpr uint64_t PermViewChannel = 1ull << 10;

    // Compact copy of a channel permission overwrite (the JSON is dropped after parsing).
    struct Overwrite
    {
        uint64_t id;
        uint64_t allow;
        uint64_t deny;
    };

    std::vector<Overwrite> ParseOverwrites(winrt::Windows::Data::Json::JsonArray const& array);

    // Everything needed to resolve the current user's permissions in one guild.
    struct GuildPermissions
    {
        uint64_t guildId = 0;
        uint64_t selfId = 0;
        uint64_t ownerId = 0;
        std::unordered_map<uint64_t, uint64_t> rolePermissions;
        std::vector<uint64_t> selfRoles;
        bool known = false; // false -> member roles unknown, show everything

        // Standard Discord algorithm: base (roles) -> @everyone overwrite -> role overwrites -> member overwrite.
        bool CanView(std::vector<Overwrite> const& overwrites) const;
    };
}
