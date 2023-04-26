#include "sigscan.h"

#include <cstdio>
#include <Windows.h>
#include <Psapi.h>
#pragma comment(lib, "Psapi.lib")

namespace sigscan
{
    bool compare(const char* location, const char* aob, const char* mask)
    {
        for (; *mask; ++aob, ++mask, ++location)
        {
            if (*mask == 'x' && *location != *aob)
            {
                return false;
            }
        }

        return true;
    }

    bool compare_reverse(const char* location, const char* aob, const char* mask)
    {
        const auto* mask_iter = mask + strlen(mask) - 1;
        for (; mask_iter >= mask; --aob, --mask_iter, --location)
        {
            if (*mask_iter == 'x' && *location != *aob)
            {
                return false;
            }
        }

        return true;
    }

    uint8_t* scan(const char* aob, const char* mask, uintptr_t start, uintptr_t end)
    {
        if (start <= end)
        {
            for (; start < end - strlen(mask); ++start)
            {
                if (compare(reinterpret_cast<char*>(start), aob, mask))
                {
                    return reinterpret_cast<uint8_t*>(start);
                }
            }
        }
        else
        {
            for (; start >= end; --start)
            {
                if (compare_reverse(reinterpret_cast<char*>(start), aob, mask))
                {
                    return reinterpret_cast<uint8_t*>(start) - strlen(mask) - 1;
                }
            }
        }

        return nullptr;
    };

    uint8_t* scan(LPCWSTR hmodule, const char* aob, const char* mask)
    {
        MODULEINFO info;

        if (GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(hmodule), &info, sizeof info))
        {
            printf("scan(): got module info\n");
            return scan(aob, mask, reinterpret_cast<unsigned>(info.lpBaseOfDll),
                        // NOLINT(clang-diagnostic-void-pointer-to-int-cast)
                        reinterpret_cast<uintptr_t>(&info.lpBaseOfDll + info.SizeOfImage));
        }

        printf("scan(): failed\n");
        return nullptr;
    }
}
