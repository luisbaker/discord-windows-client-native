#pragma once

#include <psapi.h>
#include <cstdio>

// Opt-in memory checkpoints: set DISCORDWIN3_MEMLOG=1 to append "tag private ws" lines
// to %TEMP%\discordwin3-mem.log. Zero cost when the variable is not set.
namespace DiscordWin3
{
    inline void MemLog(wchar_t const* tag, size_t extra = 0)
    {
        static int const enabled = GetEnvironmentVariableW(L"DISCORDWIN3_MEMLOG", nullptr, 0) > 0 ? 1 : 0;
        if (!enabled) return;

        PROCESS_MEMORY_COUNTERS_EX counters{ sizeof(counters) };
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
        wchar_t path[MAX_PATH]{};
        GetTempPathW(MAX_PATH, path);
        wcscat_s(path, L"discordwin3-mem.log");
        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"a, ccs=UTF-8") == 0 && f)
        {
            fwprintf(f, L"%-28s private=%6zu MB  ws=%6zu MB  extra=%zu\n", tag,
                     counters.PrivateUsage >> 20, counters.WorkingSetSize >> 20, extra);
            fclose(f);
        }
    }
}
