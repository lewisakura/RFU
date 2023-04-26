#pragma once

#pragma pack(push, 1)
#include <cstdint>
#include <vector>

struct SettingsIPC
{
    bool vsync_enabled;
    double fps_cap;

    struct
    {
        int scan_result;
        void* scheduler;
        int sfd_offset;
        int present_count;
    } debug;
};
#pragma pack(pop)

namespace Settings
{
    enum class UnlockMethodType
    {
        Hybrid,
        MemoryWrite,
        FlagsFile,

        Count
    };

    extern bool VSyncEnabled;
    extern std::vector<double> FPSCapValues;
    extern uint32_t FPSCapSelection;
    extern double FPSCap;
    extern bool UnlockClient;
    extern bool UnlockStudio;
    extern bool CheckForUpdates;
    extern bool NonBlockingErrors;
    extern bool SilentErrors;
    extern UnlockMethodType UnlockMethod;

    bool Init();
    bool Load();
    bool Save();

    bool Update();

#ifndef RFU_NO_DLL
    SettingsIPC* GetIPC();
#endif
}
