#include <Windows.h>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <fstream>
#include <TlHelp32.h>

#pragma comment(lib, "Shlwapi.lib")
#include <Shlwapi.h>
#include <unordered_set>

#include "ui.h"
#include "settings.h"
#include "rfu.h"
#include "procutil.h"
#include "sigscan.h"
#include "nlohmann.hpp"

#define ROBLOX_BASIC_ACCESS (PROCESS_QUERY_INFORMATION | PROCESS_VM_READ)
#define	ROBLOX_WRITE_ACCESS (PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE)

enum class RobloxHandleType
{
    None,
    Client,
    UWP,
    Studio
};

struct RobloxProcessHandle
{
    DWORD id;
    HANDLE handle;
    RobloxHandleType type;
    bool can_write;

    RobloxProcessHandle(DWORD process_id = 0, RobloxHandleType type = RobloxHandleType::None, bool open = false) : id(process_id), handle(NULL), type(type), can_write(false)
    {
        if (open) Open();
    };

    RobloxProcessHandle(const RobloxProcessHandle &) = delete;
    RobloxProcessHandle &operator=(const RobloxProcessHandle &) = delete;

    RobloxProcessHandle(RobloxProcessHandle &&other) noexcept
    {
        std::swap(id, other.id);
        std::swap(handle, other.handle);
        std::swap(type, other.type);
        std::swap(can_write, other.can_write);
    }

    RobloxProcessHandle &operator=(RobloxProcessHandle &&other) noexcept
    {
        if (this != &other)
        {
            if (handle) CloseHandle(handle);
            id = std::exchange(other.id, {});
            handle = std::exchange(other.handle, {});
            type = std::exchange(other.type, {});
            can_write = std::exchange(other.can_write, {});
        }
        return *this;
    }

    ~RobloxProcessHandle()
    {
        if (handle)
        {
            //printf("[%p] Closing handle with type=%u, can_write=%u\n", handle, type, can_write);
            CloseHandle(handle);
        }
    }

    bool IsValid() const
    {
        return id != 0;
    }

    bool IsOpen() const
    {
        return handle != nullptr;
    }

    bool Open()
    {
        can_write = type == RobloxHandleType::Studio;
        handle = OpenProcess(can_write ? ROBLOX_WRITE_ACCESS : ROBLOX_BASIC_ACCESS, FALSE, id);
        return handle != nullptr;
    }

    HANDLE CreateWriteHandle() const
    {
        HANDLE new_handle = nullptr;
        DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &new_handle, ROBLOX_WRITE_ACCESS, FALSE,
                        NULL);
        return new_handle;
    }

    bool UpgradeHandle() noexcept
    {
        if (can_write) return true;
        HANDLE new_handle = CreateWriteHandle();
        if (!new_handle) return false;
        CloseHandle(handle);
        handle = new_handle;
        can_write = true;
        return true;
    }

    template <typename T>
    void Write(const void* location, const T& value)
    {
        if (can_write)
        {
            printf("[%p] Writing to %p\n", handle, location);
            ProcUtil::Write<T>(handle, location, value);
        }
        else
        {
            auto write_handle = CreateWriteHandle();
            if (!write_handle) throw ProcUtil::WindowsException("failed to create write handle");
            printf("[%p] Writing to %p with handle %p\n", handle, location, write_handle);
            ProcUtil::Write<T>(write_handle, location, value);
            CloseHandle(write_handle);
        }
    }
};

HANDLE SingletonMutex;

std::vector<RobloxProcessHandle> GetRobloxProcesses(bool open_all = true, bool include_client = true, bool include_studio = true)
{
    std::vector<RobloxProcessHandle> result;
    if (include_client)
    {
        for (auto pid : ProcUtil::GetProcessIdsByImageName(L"RobloxPlayerBeta.exe")) result.emplace_back(pid, RobloxHandleType::Client, open_all);
        for (auto pid : ProcUtil::GetProcessIdsByImageName(L"Windows10Universal.exe")) result.emplace_back(pid, RobloxHandleType::UWP, open_all);
    }
    if (include_studio)
    {
        for (auto pid : ProcUtil::GetProcessIdsByImageName(L"RobloxStudioBeta.exe")) result.emplace_back(pid, RobloxHandleType::Studio, open_all);
    }
    return result;
}

RobloxProcessHandle GetRobloxProcess()
{
    auto processes = GetRobloxProcesses();

    if (processes.empty())
        return NULL;

    if (processes.size() == 1)
    {
#pragma warning( push )
#pragma warning( disable : 26816 ) // no way around this warning
        return std::move(processes[0]);
#pragma warning( pop )
    }

    printf("Multiple processes found! Select a process to inject into (%u - %zu):\n", 1, processes.size());
    for (auto i = 0; i < processes.size(); i++) // NOLINT(clang-diagnostic-sign-compare)
    {
        try
        {
            ProcUtil::ProcessInfo info(processes[i].handle, true);
            printf("[%d] [%s] %s\n", i + 1, info.name.c_str(), info.window_title.c_str());
        }
        catch (ProcUtil::WindowsException& e)
        {
            printf("[%d] Invalid process %p (%s, %lX)\n", i + 1, processes[i].handle, e.what(), e.GetLastError());
        }
    }

    int selection;

    while (true)
    {
        printf("\n>");
        std::cin >> selection;

        if (std::cin.fail())
        {
            std::cin.clear();
            std::cin.ignore(std::cin.rdbuf()->in_avail());
            printf("Invalid input, try again\n");
            continue;
        }

        if (selection < 1 || selection > processes.size()) // NOLINT(clang-diagnostic-sign-compare)
        {
            printf("Please enter a number between %u and %zu\n", 1, processes.size());
            continue;
        }

        break;
    }

    return std::move(processes[selection - 1]);
}

size_t FindTaskSchedulerFrameDelayOffset(HANDLE process, const void* scheduler)
{
    const size_t search_offset = 0x100; // ProcUtil::IsProcess64Bit(process) ? 0x200 : 0x100;

    uint8_t buffer[0x100];
    if (!ProcUtil::Read(process, static_cast<const uint8_t*>(scheduler) + search_offset, buffer, sizeof buffer))
        return -1;

    /* Find the frame delay variable inside TaskScheduler (ugly, but it should survive updates unless the variable is removed or shifted)
       (variable was at +0x150 (32-bit) and +0x180 (studio 64-bit) as of 2/13/2020) */
    for (auto i = 0; i < sizeof buffer - sizeof(double); i += 4) // NOLINT(clang-diagnostic-sign-compare)
    {
        static const auto frame_delay = 1.0 / 60.0;
        auto difference = *reinterpret_cast<double*>(buffer + i) - frame_delay;
        difference = difference < 0 ? -difference : difference;
        if (difference < std::numeric_limits<double>::epsilon()) return search_offset + i;
    }

    return -1;
}

void NotifyError(const char* title, const char* error)
{
    if (Settings::SilentErrors || Settings::NonBlockingErrors)
    {
        // lol
        auto* const console = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO info{};
        GetConsoleScreenBufferInfo(console, &info);

        const WORD color = info.wAttributes & (0xFF00 | (FOREGROUND_RED | FOREGROUND_INTENSITY));
        SetConsoleTextAttribute(console, color);

        printf("[ERROR] %s\n", error);

        SetConsoleTextAttribute(console, info.wAttributes);

        if (!Settings::SilentErrors)
        {
            UI::SetConsoleVisible(true);
        }
    }
    else
    {
        MessageBoxA(UI::Window, error, title, MB_OK);
    }
}

struct RobloxProcess
{
    RobloxProcessHandle process{};
    ProcUtil::ModuleInfo main_module{};
    std::vector<const void*> ts_ptr_candidates; // task scheduler pointer
    const void* fd_ptr = nullptr; // frame delay pointer
    bool use_flags_file = false;

    int retries_left = 0;

    bool BlockingLoadModuleInfo()
    {
        int tries = 5;
        int wait_time = 100;

        printf("[%p] Finding process base...\n", process.handle);

        while (true)
        {
            auto info = ProcUtil::ProcessInfo(process.handle);

            if (info.hmodule.base != nullptr)
            {
                main_module = info.hmodule;
                return true;
            }

            if (tries--)
            {
                printf("[%p] Retrying in %dms...\n", process.handle, wait_time);
                Sleep(wait_time);
                wait_time *= 2;
            }
            else
            {
                return false;
            }
        }
    }

    bool Attach(RobloxProcessHandle handle, int retry_count)
    {
        process = std::move(handle);
        retries_left = retry_count;

        if (!BlockingLoadModuleInfo())
        {
            NotifyError("RFU Error",
                        "Failed to get process base! Restart RFU or, if you are on a 64-bit computer, make sure you are using the 64-bit version of RFU.");
            retries_left = -1;
            return false;
        }

        printf("[%p] Process base: %p (size %zu)\n", process.handle, main_module.base, main_module.size);

        if (main_module.size < 1024 * 1024 * 10)
        {
            printf("[%p] Appears to be security daemon, ignoring (%zu)\n", process.handle, main_module.size);
            retries_left = -1;
            return false;
        }

        OnUnlockMethodUpdate();
        Tick();

        return !ts_ptr_candidates.empty() && fd_ptr != nullptr;
    }

    void Tick()
    {
        if (use_flags_file) return;

        if (retries_left < 0) return; // we tried

        if (ts_ptr_candidates.empty())
        {
            const char* error = nullptr;
            FindTaskScheduler();

            if (ts_ptr_candidates.empty())
            {
                if (error) retries_left = 0;
                // if FindTaskScheduler returned an error it already retried 5 times. TODO: remove
                if (retries_left-- <= 0)
                    NotifyError("RFU Error",
                                error
                                    ? error
                                    : "Unable to find TaskScheduler! This is probably due to a Roblox update -- watch the github for any patches or a fix.");
                return;
            }
        }

        if (!ts_ptr_candidates.empty() && !fd_ptr)
        {
            try
            {
                size_t fail_count = 0;

                for (const void* ts_ptr : ts_ptr_candidates)
                {
                    if (const auto* const scheduler = static_cast<const uint8_t*>(ProcUtil::ReadPointer(
                        process.handle, ts_ptr)))
                    {
                        printf("[%p] Potential task scheduler: %p\n", process.handle, scheduler);
                        // NOLINT(clang-diagnostic-format-pedantic)
                        // (keeps telling me to change %p -> %s and vice versa)

                        const auto delay_offset = FindTaskSchedulerFrameDelayOffset(process.handle, scheduler);
                        if (delay_offset == -1) // NOLINT(clang-diagnostic-sign-compare)
                        {
                            fail_count++;
                            continue;
                        }

                        printf("[%p] Frame delay offset: %zu (%x)\n", process.handle, delay_offset, delay_offset);

                        fd_ptr = scheduler + delay_offset;

                        SetFPSCap(Settings::FPSCap);
                        return;
                    }

                    printf("[%p] *ts_ptr (%p) == nullptr\n", process.handle, ts_ptr);
                }

                if (fail_count > 0)
                {
                    // valod pointer but no frame delay var
                    if (retries_left-- <= 0)
                        NotifyError("RFU Error",
                                    "Variable scan failed! This is probably due to a Roblox update or because your framerate is not ~60 FPS.");
                }
            }
            catch (ProcUtil::WindowsException& e)
            {
                printf("[%p] RobloxProcess::Tick failed: %s (%lu)\n", process.handle, e.what(), e.GetLastError());
                if (retries_left-- <= 0)
                    NotifyError("RFU Error", "An exception occurred while performing the variable scan.");
            }
        }
    }

    bool IsLikelyAntiCheatProtected() const
    {
        return process.type != RobloxHandleType::Studio && ProcUtil::IsProcess64Bit(process.handle);
    }

    std::filesystem::path GetClientAppSettingsFilePath() const
    {
        return main_module.path.parent_path() / "ClientSettings" / "ClientAppSettings.json";
    }

    std::optional<int> FetchTargetFpsDiskValue(nlohmann::json* object_out = nullptr) const
    {
        std::ifstream file(GetClientAppSettingsFilePath());

        if (file.is_open())
        {
            nlohmann::json object = nlohmann::json::parse(file, nullptr, false);
            if (!object.is_discarded())
            {
                std::optional<int> result{};

                if (object.contains("DFIntTaskSchedulerTargetFps"))
                {
                    auto target_fps = object["DFIntTaskSchedulerTargetFps"];
                    if (target_fps.is_number_integer())
                    {
                        result = target_fps.get<int>();
                    }
                }

                if (object_out)
                    *object_out = std::move(object);

                return result;
            }
        }

        return std::nullopt;
    }

    bool IsTargetFpsFlagActive() const
    {
        auto value = FetchTargetFpsDiskValue();
        return value.has_value() && *value > 0;
    }

    void WriteFlagsFile(int cap) const
    {
        // todo: read from registry like a normal person

        if (cap == 0) cap = 99999999; // because 0 is not a valid value

        auto settings_file_path = GetClientAppSettingsFilePath();
        printf("[%p] Updating DFIntTaskSchedulerTargetFps in %ls to %d\n", process.handle, settings_file_path.c_str(),
               cap);

        nlohmann::json object{};

        // read
        auto current_cap = FetchTargetFpsDiskValue(&object);
        if (current_cap.has_value() && *current_cap == cap)
        {
            return;
        }

        // update
        object["DFIntTaskSchedulerTargetFps"] = cap;

        // write
        {
            std::error_code ec{};
            create_directory(settings_file_path.parent_path(), ec);

            std::ofstream file(settings_file_path);
            if (!file.is_open())
            {
                NotifyError("RFU Error",
                            "Failed to write ClientAppSettings.json! If running the Windows Store version of Roblox, try running Roblox FPS Unlocker as administrator or using a different unlock method.");
                return;
            }
            file << object.dump(4);
        }

        // prompt
        char message[512]{};
        sprintf_s(message,
                  "Set DFIntTaskSchedulerTargetFps to %d in %ls\n\nPlease restart Roblox for changes to take effect.",
                  cap, settings_file_path.c_str());
        MessageBoxA(UI::Window, message, "RFU", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    }

    void SetFPSCapInMemory(double cap) const
    {
        if (fd_ptr)
        {
            try
            {
                static const auto min_frame_delay = 1.0 / 10000.0;
                const auto frame_delay = cap <= 0.0 ? min_frame_delay : 1.0 / cap;

                ProcUtil::Write(process.handle, fd_ptr, frame_delay);
            }
            catch (ProcUtil::WindowsException& e)
            {
                printf("[%p] RobloxProcess::SetFPSCap failed: %s (%lu)\n", process.handle, e.what(), e.GetLastError());
            }
        }
    }

    bool FindTaskScheduler()
    {
        try
        {
            // TODO: remove this retry code? (see RobloxProcess::Tick)
            auto tries = 5;
            auto wait_time = 100;

            const auto* const start = static_cast<const uint8_t*>(main_module.base);
            const auto* const end = start + main_module.size;

            if (ProcUtil::IsProcess64Bit(process.handle))
            {
                printf("[%p] Is 64bit\n", process.handle);
                // 40 53 48 83 EC 20 0F B6 D9 E8 ?? ?? ?? ?? 86 58 04 48 83 C4 20 5B C3
                if (const auto* const result = static_cast<const uint8_t*>(ProcUtil::ScanProcess(
                    process.handle,
                    "\x40\x53\x48\x83\xEC\x20\x0F\xB6\xD9\xE8\x00\x00\x00\x00\x86\x58\x04\x48\x83\xC4\x20\x5B\xC3",
                    "xxxxxxxxxx????xxxxxxxxx", start, end)))
                {
                    const auto* const gts_fn = result + 14 + ProcUtil::Read<int32_t>(process.handle, result + 10);

                    printf("[%p] GetTaskScheduler (Studio): %p\n", process.handle, gts_fn);
                    // NOLINT(clang-diagnostic-format-pedantic)

                    uint8_t buffer[0x100];
                    if (ProcUtil::Read(process.handle, gts_fn, buffer, sizeof buffer))
                    {
                        if (auto* const inst = sigscan::scan("\x48\x8B\x05\x00\x00\x00\x00\x48\x83\xC4\x28",
                                                             "xxx????xxxx",
                                                             reinterpret_cast<uintptr_t>(buffer),
                                                             reinterpret_cast<uintptr_t>(buffer) + 0x100))
                        // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
                        {
                            const auto* const remote = gts_fn + (inst - buffer);
                            ts_ptr_candidates = {remote + 7 + *reinterpret_cast<int32_t*>(inst + 3)};
                            return true;
                        }
                    }
                }
                else
                {
                    // Byfron

                    std::unordered_set<const void*> candidates{};
                    auto i = start;
                    auto stop = (std::min)(end, start + 40 * 1024 * 1024);

                    while (i < stop)
                    {
                        auto result = static_cast<const uint8_t*>(ProcUtil::ScanProcess(
                            process.handle, "\x48\x8B\x05\x00\x00\x00\x00\x48\x83\xC4\x48\xC3",
                            "xxx????xxxxx", i, stop));
                        if (!result) break;
                        candidates.insert(result + 7 + ProcUtil::Read<int32_t>(process.handle, result + 3));
                        if (candidates.size() >= 5) break;
                        i = result + 1;
                    }

                    printf("[%p] GetTaskScheduler (Byfron): found %zu candidates\n", process.handle, candidates.size());

                    ts_ptr_candidates = std::vector(candidates.begin(), candidates.end());
                    return true;
                }
            }
            else
            {
                printf("[%p] Is 32bit\n", process.handle);

                // 55 8B EC 83 E4 F8 83 EC 08 E8 ?? ?? ?? ?? 8D 0C 24
                if (const auto* const result = static_cast<const uint8_t*>(ProcUtil::ScanProcess(
                    process.handle, "\x55\x8B\xEC\x83\xE4\xF8\x83\xEC\x08\xE8\xDE\xAD\xBE\xEF\x8D\x0C\x24",
                    "xxxxxxxxxx????xxx",
                    start, end)))
                {
                    const auto* const gts_fn = result + 14 + ProcUtil::Read<int32_t>(process.handle, result + 10);

                    printf("[%p] GetTaskScheduler (LTCG): %p\n", process.handle, gts_fn);
                    // NOLINT(clang-diagnostic-format-pedantic)

                    uint8_t buffer[0x100];
                    if (ProcUtil::Read(process.handle, gts_fn, buffer, sizeof buffer))
                    {
                        if (auto* const inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx",
                                                             reinterpret_cast<uintptr_t>(buffer),
                                                             reinterpret_cast<uintptr_t>(buffer) + 0x100))
                        // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
                        {
                            //printf("[%p] Inst: %p\n", process, gts_fn + (inst - buffer));
                            ts_ptr_candidates = {
                                reinterpret_cast<const void*>(*reinterpret_cast<uint32_t*>(inst + 1))
                            }; // NOLINT(performance-no-int-to-ptr)
                            return true;
                        }
                    }
                }
                // ReSharper disable once CppDeclarationHidesLocal
                // 55 8B EC 83 EC 10 56 E8 ?? ?? ?? ?? 8B F0 8D 45 F0
                else if (const auto* const result = static_cast<const uint8_t*>(ProcUtil::ScanProcess(
                    // NOLINT(clang-diagnostic-shadow)
                    process.handle, "\x55\x8B\xEC\x83\xEC\x10\x56\xE8\x00\x00\x00\x00\x8B\xF0\x8D\x45\xF0",
                    "xxxxxxxx????xxxxx",
                    start, end)))
                {
                    auto gts_fn = result + 12 + ProcUtil::Read<int32_t>(process.handle, result + 8);

                    printf("[%p] GetTaskScheduler (Non-LTCG): %p\n", process.handle, gts_fn);

                    uint8_t buffer[0x100];
                    if (ProcUtil::Read(process.handle, gts_fn, buffer, sizeof(buffer)))
                    {
                        if (auto inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx",
                                                      reinterpret_cast<uintptr_t>(buffer),
                                                      reinterpret_cast<uintptr_t>(buffer) + 0x100))
                        // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
                        {
                            ts_ptr_candidates = {
                                reinterpret_cast<const void*>(*reinterpret_cast<uint32_t*>(inst + 1))
                            }; // NOLINT(performance-no-int-to-ptr)
                            return true;
                        }
                    }
                }
                // ReSharper disable once CppDeclarationHidesLocal
                // 55 8B EC 83 E4 F8 83 EC 14 56 E8 ?? ?? ?? ?? 8D 4C 24 10
                else if (const auto* const result = static_cast<const uint8_t*>(ProcUtil::ScanProcess(
                    // NOLINT(clang-diagnostic-shadow)
                    process.handle, "\x55\x8B\xEC\x83\xE4\xF8\x83\xEC\x14\x56\xE8\x00\x00\x00\x00\x8D\x4C\x24\x10",
                    "xxxxxxxxxxx????xxxx",
                    start, end)))
                {
                    auto gts_fn = result + 15 + ProcUtil::Read<int32_t>(process.handle, result + 11);

                    printf("[%p] GetTaskScheduler (UWP): %p\n", process.handle, gts_fn);

                    uint8_t buffer[0x100];
                    if (ProcUtil::Read(process.handle, gts_fn, buffer, sizeof(buffer)))
                    {
                        if (auto inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx",
                                                      reinterpret_cast<uintptr_t>(buffer),
                                                      reinterpret_cast<uintptr_t>(buffer) + 0x100))
                        // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
                        {
                            ts_ptr_candidates = {
                                reinterpret_cast<const void*>(*reinterpret_cast<uint32_t*>(inst + 1))
                            }; // NOLINT(performance-no-int-to-ptr)
                            return true;
                        }
                    }
                }
            }
        }
        catch ([[maybe_unused]] ProcUtil::WindowsException& e)
        {
            printf("[%p] WindowsException occurred, GetLastError() = %lu\n", process.handle, GetLastError());
        }

        return false;
    }

    void SetFPSCap(double cap) const
    {
        if (use_flags_file)
        {
            WriteFlagsFile(cap);
        }
        else
        {
            SetFPSCapInMemory(cap);
        }
    }

    void OnClose() const
    {
        SetFPSCapInMemory(60.0);
    }

    void OnUnlockMethodUpdate()
    {
        if (Settings::UnlockMethod == Settings::UnlockMethodType::FlagsFile
            || (Settings::UnlockMethod == Settings::UnlockMethodType::Hybrid && IsLikelyAntiCheatProtected()))
        {
            printf("[%p] Using FlagsFile mode\n", process.handle);
            use_flags_file = true;
            WriteFlagsFile(Settings::FPSCap);
        }
        else
        {
            printf("[%p] Using MemoryWrite mode\n", process.handle);
            if (IsTargetFpsFlagActive()) WriteFlagsFile(-1);
            Tick();
        }
    }
};

std::unordered_map<DWORD, RobloxProcess> attached_processes; // NOLINT(clang-diagnostic-exit-time-destructors)

void RFU_SetFPSCap(const double value)
{
    for (auto& it : attached_processes)
    {
        it.second.SetFPSCap(value);
    }
}

void RFU_OnUIClose()
{
    for (auto& it : attached_processes)
    {
        it.second.OnClose();
    }
}

void RFU_OnUIUnlockMethodChange()
{
    for (auto& it : attached_processes)
    {
        it.second.OnUnlockMethodUpdate();
    }
}

bool RunsOnStartup()
{
    HKEY hK;
    RegOpenKeyA(HKEY_CURRENT_USER, R"(Software\Microsoft\Windows\CurrentVersion\Run)", &hK);
    const auto result = RegQueryValueExA(hK, RFU_REGKEY, nullptr, nullptr, nullptr, nullptr);
    const auto returnVal = result != ERROR_NO_MATCH && result != ERROR_FILE_NOT_FOUND;

    RegCloseKey(hK);

    return returnVal;
}

void SetRunOnStartup(bool shouldRun)
{
    HKEY hK;
    RegOpenKeyA(HKEY_CURRENT_USER, R"(Software\Microsoft\Windows\CurrentVersion\Run)", &hK);
    if (shouldRun)
    {
        auto* const ourModule = GetModuleHandle(nullptr);
        char path[MAX_PATH];

        GetModuleFileNameA(ourModule, path, sizeof path);

        // path size + quotes + space + --silent + zero terminator
        const auto filePathSize = strlen(path) + 2 + 1 + 7 + 1;
        char corrected[sizeof path + 2 + 1 + 7 + 1];
        sprintf_s(corrected, "\"%s\" --silent\0", path);

        RegSetValueExA(hK, RFU_REGKEY, 0, REG_SZ, reinterpret_cast<BYTE*>(corrected), filePathSize);
        // NOLINT(clang-diagnostic-shorten-64-to-32)
        RegCloseKey(hK);
    }
    else
    {
        RegDeleteValueA(hK, RFU_REGKEY);
    }

    RegCloseKey(hK);
}

void pause()
{
    printf("Press enter to continue . . .");
    (void)getchar();
}

DWORD WINAPI WatchThread(LPVOID)
{
    printf("Watch thread started\n");

    while (true)
    {
        auto processes = GetRobloxProcesses(Settings::UnlockClient, Settings::UnlockStudio);

        for (auto& process : processes)
        {
            auto id = GetProcessId(process.handle);

            if (!attached_processes.contains(id))
            {
                printf("Injecting into new process %p (pid %lu)\n", process.handle, id);
                RobloxProcess roblox_process;

                roblox_process.Attach(std::move(process), 2);

                attached_processes[id] = std::move(roblox_process);

                printf("New size: %zu\n", attached_processes.size());
            }
            else
            {
                CloseHandle(process.handle);
            }
        }

        for (auto it = attached_processes.begin(); it != attached_processes.end();)
        {
            auto* const process = it->second.process.handle;

            DWORD code;
            GetExitCodeProcess(process, &code);

            if (code != STILL_ACTIVE)
            {
                printf("Purging dead process %p (pid %lu) (code %lX)\n", process, GetProcessId(process), code);
                it = attached_processes.erase(it);
                CloseHandle(process);
                printf("New size: %zu\n", attached_processes.size());
            }
            else
            {
                it->second.Tick();
                ++it;
            }
        }

        UI::AttachedProcessesCount = attached_processes.size(); // NOLINT(clang-diagnostic-shorten-64-to-32)

        Sleep(2000);
    }
}

bool CheckRunning()
{
    SingletonMutex = CreateMutexA(nullptr, FALSE, "RFUMutex");

    if (!SingletonMutex)
    {
        MessageBoxA(nullptr, "Unable to create mutex", "Error", MB_OK);
        return false;
    }

    return GetLastError() == ERROR_ALREADY_EXISTS;
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // set current directory to executable location (fixes settings issues when starting up)
    //
    // yes, I could make a separate --startup flag to do this, but there is no legitimate reason to have the
    // settings file not be beside the executable anyway
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    PathRemoveFileSpecA(self);
    // I'm aware this is deprecated but as long as it works I will keep using it for backwards compat

    SetCurrentDirectoryA(self);

    if (!Settings::Init())
    {
        char buffer[64];
        sprintf_s(buffer, "Unable to initiate settings\nGetLastError() = %X", GetLastError());
        MessageBoxA(nullptr, buffer, "Error", MB_OK);
        return 0;
    }

    UI::IsConsoleOnly = strstr(lpCmdLine, "--console") != nullptr;
    UI::IsSilent = strstr(lpCmdLine, "--silent") != nullptr;

    if (UI::IsConsoleOnly)
    {
        UI::ToggleConsole();

        printf("Waiting for Roblox...\n");

        RobloxProcessHandle process;

        do
        {
            Sleep(100);
            process = GetRobloxProcess();
        }
        while (!process.handle);

        printf("Found Roblox...\n");
        printf("Attaching...\n");

        if (!RobloxProcess().Attach(std::move(process), 0))
        {
            printf("\nERROR: unable to attach to process\n");
            pause();
            return 0;
        }

        CloseHandle(process.handle);

        printf("\nSuccess! The injector will close in 3 seconds...\n");

        Sleep(3000);

        return 0;
    }

    if (CheckRunning())
    {
        MessageBoxA(nullptr, "RFU is already running", "Error", MB_OK);
    }
    else
    {
        UI::CreateHiddenConsole();

        // if we aren't silent, we need to show the console initially for output
        if (!UI::IsSilent)
        {
            UI::ToggleConsole();
        }

        // check for updates regardless
        if (Settings::CheckForUpdates)
        {
            printf("Checking for updates...\n");
            if (CheckForUpdates()) return 0;
        }

        // and carry on.
        if (!UI::IsSilent)
        {
            printf("Minimizing to system tray in 2 seconds...\n");
            Sleep(2000);

            UI::ToggleConsole();
        }

        return UI::Start(hInstance, WatchThread);
    }

    return 0;
}
