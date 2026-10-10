#include "pch.h"
#include "Permissions.h"
#include "Json.h"

using namespace winrt::Windows::Data::Json;

namespace DiscordWin3::Discord
{
    std::vector<Overwrite> ParseOverwrites(::DiscordWin3::Slim::Value const& array)
    {
        std::vector<Overwrite> result;
        for (auto o : array)
        {
            if (!o.IsObject())
            {
                continue;
            }
            result.push_back({ Json::U64(o, L"id"), Json::U64(o, L"allow"), Json::U64(o, L"deny") });
        }
        return result;
    }

    bool GuildPermissions::CanView(std::vector<Overwrite> const& overwrites) const
    {
        return Has(overwrites, PermViewChannel);
    }

    bool GuildPermissions::Has(std::vector<Overwrite> const& overwrites, uint64_t bit) const
    {
        if (!known || selfId == ownerId)
        {
            return true;
        }

        uint64_t perms = 0;
        if (auto it = rolePermissions.find(guildId); it != rolePermissions.end())
        {
            perms = it->second; // @everyone role shares the guild id
        }
        for (auto role : selfRoles)
        {
            if (auto it = rolePermissions.find(role); it != rolePermissions.end())
            {
                perms |= it->second;
            }
        }
        if (perms & PermAdministrator)
        {
            return true;
        }

        uint64_t roleAllow = 0, roleDeny = 0;
        uint64_t memberAllow = 0, memberDeny = 0;
        for (auto const& o : overwrites)
        {
            if (o.id == guildId)
            {
                perms = (perms & ~o.deny) | o.allow;
            }
            else if (o.id == selfId)
            {
                memberAllow = o.allow;
                memberDeny = o.deny;
            }
            else if (std::find(selfRoles.begin(), selfRoles.end(), o.id) != selfRoles.end())
            {
                roleAllow |= o.allow;
                roleDeny |= o.deny;
            }
        }
        perms = (perms & ~roleDeny) | roleAllow;
        perms = (perms & ~memberDeny) | memberAllow;
        return (perms & bit) == bit;
    }
}
