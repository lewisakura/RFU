#include <Windows.h>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <TlHelp32.h>

#pragma comment(lib, "Shlwapi.lib")
#include <Shlwapi.h>

#include "ui.h"
#include "settings.h"
#include "rfu.h"
#include "procutil.h"
#include "sigscan.h"

HANDLE SingletonMutex;

std::vector<HANDLE> GetRobloxProcesses(bool include_client = true, bool include_studio = true)
{
	std::vector<HANDLE> result;
	if (include_client)
	{
		for (auto* handle : ProcUtil::GetProcessesByImageName(L"RobloxPlayerBeta.exe")) result.emplace_back(handle);
		for (auto* handle : ProcUtil::GetProcessesByImageName(L"Windows10Universal.exe")) result.emplace_back(handle);
	}
	if (include_studio) for (auto* handle : ProcUtil::GetProcessesByImageName(L"RobloxStudioBeta.exe")) result.emplace_back(handle);
	return result;
}

HANDLE GetRobloxProcess()
{
	auto processes = GetRobloxProcesses();

	if (processes.empty())
		return nullptr;

	if (processes.size() == 1) {
#pragma warning( push )
#pragma warning( disable : 26816 ) // no way around this warning
		return processes[0];
#pragma warning( pop )
	}

	printf("Multiple processes found! Select a process to inject into (%u - %zu):\n", 1, processes.size());
	for (auto i = 0; i < processes.size(); i++)  // NOLINT(clang-diagnostic-sign-compare)
	{
		try
		{
			ProcUtil::ProcessInfo info(processes[i], true);
			printf("[%d] [%s] %s\n", i + 1, info.name.c_str(), info.window_title.c_str());
		}
		catch (ProcUtil::WindowsException& e)
		{
			printf("[%d] Invalid process %p (%s, %lX)\n", i + 1, processes[i], e.what(), e.GetLastError());
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

	return processes[selection - 1];
}

size_t FindTaskSchedulerFrameDelayOffset(HANDLE process, const void* scheduler)
{
	const size_t search_offset = 0x100; // ProcUtil::IsProcess64Bit(process) ? 0x200 : 0x100;

	uint8_t buffer[0x100];
	if (!ProcUtil::Read(process, static_cast<const uint8_t*>(scheduler) + search_offset, buffer, sizeof buffer))
		return -1;

	/* Find the frame delay variable inside TaskScheduler (ugly, but it should survive updates unless the variable is removed or shifted)
	   (variable was at +0x150 (32-bit) and +0x180 (studio 64-bit) as of 2/13/2020) */
	for (auto i = 0; i < sizeof buffer - sizeof(double); i += 4)  // NOLINT(clang-diagnostic-sign-compare)
	{
		static const auto frame_delay = 1.0 / 60.0;
		auto difference = *reinterpret_cast<double*>(buffer + i) - frame_delay;
		difference = difference < 0 ? -difference : difference;
		if (difference < std::numeric_limits<double>::epsilon()) return search_offset + i;
	}

	return -1;
}

const void* FindTaskScheduler(HANDLE process, const char** error = nullptr)
{
	try
	{
		ProcUtil::ProcessInfo info;

		// TODO: remove this retry code? (see RobloxProcess::Tick)
		auto tries = 5;
		auto wait_time = 100;

		while (true)
		{
			printf("[%p] Init ProcessInfo\n", process);
			info = ProcUtil::ProcessInfo(process);
			if (info.hmodule.base != nullptr)
				break;

			if (tries--)
			{
				printf("[%p] Retrying in %dms...\n", process, wait_time);
				Sleep(wait_time);
				wait_time *= 2;
			}
			else
			{
				if (error) *error = "Failed to get process base! Restart RFU or, if you are on a 64-bit operating system, make sure you are using the 64-bit version of RFU.";
				return nullptr;
			}
		}

		const auto* const start = static_cast<const uint8_t*>(info.hmodule.base);
		const auto* const end = start + info.hmodule.size;

		printf("[%p] Process Base: %p\n", process, start);  // NOLINT(clang-diagnostic-format-pedantic)
																  // (keeps telling me to change %p -> %s and vice versa)

		if (ProcUtil::IsProcess64Bit(process))
		{
			printf("[%p] Is 64bit\n", process);
			// 40 53 48 83 EC 20 0F B6 D9 E8 ?? ?? ?? ?? 86 58 04 48 83 C4 20 5B C3
			if (const auto* const result = static_cast<const uint8_t*>(ProcUtil::ScanProcess(
				process, "\x40\x53\x48\x83\xEC\x20\x0F\xB6\xD9\xE8\x00\x00\x00\x00\x86\x58\x04\x48\x83\xC4\x20\x5B\xC3",
				"xxxxxxxxxx????xxxxxxxxx", start, end)))
			{
				const auto* const gts_fn = result + 14 + ProcUtil::Read<int32_t>(process, result + 10);

				printf("[%p] GetTaskScheduler: %p\n", process, gts_fn); // NOLINT(clang-diagnostic-format-pedantic)
																			  // (keeps telling me to change %p -> %s and vice versa)

				uint8_t buffer[0x100];
				if (ProcUtil::Read(process, gts_fn, buffer, sizeof buffer))
				{
					if (auto* const inst = sigscan::scan("\x48\x8B\x05\x00\x00\x00\x00\x48\x83\xC4\x38", "xxx????xxxx",
						reinterpret_cast<uintptr_t>(buffer),
						reinterpret_cast<uintptr_t>(buffer) + 0x100))
						// mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
					{
						const auto* const remote = gts_fn + (inst - buffer);
						return remote + 7 + *reinterpret_cast<int32_t*>(inst + 3);
					}
				}
			}
		}
		else
		{
			printf("[%p] Is 32bit\n", process);
			if (const auto* const result = static_cast<const uint8_t*>(ProcUtil::ScanProcess(
				process, "\x55\x8B\xEC\x83\xE4\xF8\x83\xEC\x08\xE8\x00\x00\x00\x00\x8D\x0C\x24", "xxxxxxxxxx????xxx",
				start, end)))
			{
				const auto* const gts_fn = result + 14 + ProcUtil::Read<int32_t>(process, result + 10);

				printf("[%p] GetTaskScheduler: %p\n", process, gts_fn); // NOLINT(clang-diagnostic-format-pedantic)
																			  // (keeps telling me to change %p -> %s and vice versa)

				uint8_t buffer[0x100];
				if (ProcUtil::Read(process, gts_fn, buffer, sizeof buffer))
				{
					if (auto* const inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx",
						reinterpret_cast<uintptr_t>(buffer),
						reinterpret_cast<uintptr_t>(buffer) + 0x100))
						// mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
					{
						//printf("[%p] Inst: %p\n", process, gts_fn + (inst - buffer));
						return reinterpret_cast<const void*>(*reinterpret_cast<uint32_t*>(inst + 1));
					}
				}
			}
		}
	}
	catch ([[maybe_unused]] ProcUtil::WindowsException& e)
	{
		printf("[%p] WindowsException occurred, GetLastError() = %lu\n", process, GetLastError());
	}

	return nullptr;
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
	HANDLE handle = nullptr;
	const void* ts_ptr = nullptr; // task scheduler pointer
	const void* fd_ptr = nullptr; // frame delay pointer

	int retries_left = 0;

	bool Attach(HANDLE process, int retry_count)
	{
		handle = process;
		retries_left = retry_count;

		Tick();

		return ts_ptr != nullptr && fd_ptr != nullptr;
	}

	void Tick()
	{
		if (retries_left < 0) return; // we tried

		if (!ts_ptr)
		{
			const char* error = nullptr;
			ts_ptr = FindTaskScheduler(handle, &error);

			if (!ts_ptr)
			{
				if (error) retries_left = 0; // if FindTaskScheduler returned an error it already retried 5 times. TODO: remove
				if (retries_left-- <= 0)
					NotifyError("RFU Error", error ? error : "Unable to find TaskScheduler! This is probably due to a Roblox update -- watch the github for any patches or a fix.");
				return;
			}
		}

		if (ts_ptr && !fd_ptr)
		{
			try
			{
				if (const auto* const scheduler = static_cast<const uint8_t*>(ProcUtil::ReadPointer(handle, ts_ptr)))
				{
					printf("[%p] Scheduler: %p\n", handle, scheduler); // NOLINT(clang-diagnostic-format-pedantic)
																			 // (keeps telling me to change %p -> %s and vice versa)

					const auto delay_offset = FindTaskSchedulerFrameDelayOffset(handle, scheduler);
					if (delay_offset == -1)  // NOLINT(clang-diagnostic-sign-compare)
					{
						if (retries_left-- <= 0)
							NotifyError("RFU Error", "Variable scan failed! This is probably due to a Roblox update -- watch the github for any patches or a fix.");
						return;
					}

					printf("[%p] Frame Delay Offset: %zu (%x)\n", handle, delay_offset, delay_offset);

					fd_ptr = scheduler + delay_offset;

					SetFPSCap(Settings::FPSCap);
				}
			}
			catch (ProcUtil::WindowsException& e)
			{
				printf("[%p] RobloxProcess::Tick failed: %s (%lu)\n", handle, e.what(), e.GetLastError());
				if (retries_left-- <= 0)
					NotifyError("RFU Error", "An exception occurred while performing the variable scan.");
			}
		}
	}

	void SetFPSCap(double cap) const
	{
		if (fd_ptr)
		{
			try
			{
				static const auto min_frame_delay = 1.0 / 10000.0;
				const auto frame_delay = cap <= 0.0 ? min_frame_delay : 1.0 / cap;

				ProcUtil::Write(handle, fd_ptr, frame_delay);
			}
			catch (ProcUtil::WindowsException& e)
			{
				printf("[%p] RobloxProcess::SetFPSCap failed: %s (%lu)\n", handle, e.what(), e.GetLastError());
			}
		}
	}
};

std::unordered_map<DWORD, RobloxProcess> attached_processes;  // NOLINT(clang-diagnostic-exit-time-destructors)

void SetFPSCapExternal(const double value)
{
	for (auto& it : attached_processes)
	{
		it.second.SetFPSCap(value);
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

		RegSetValueExA(hK, RFU_REGKEY, 0, REG_SZ, reinterpret_cast<BYTE*>(corrected), filePathSize);  // NOLINT(clang-diagnostic-shorten-64-to-32)
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
			auto id = GetProcessId(process);

			if (attached_processes.find(id) == attached_processes.end())
			{
				printf("Injecting into new process %p (pid %lu)\n", process, id);
				RobloxProcess roblox_process;

				roblox_process.Attach(process, 2);

				attached_processes[id] = roblox_process;

				printf("New size: %zu\n", attached_processes.size());
			}
			else
			{
				CloseHandle(process);
			}
		}

		for (auto it = attached_processes.begin(); it != attached_processes.end();)
		{
			auto* const process = it->second.handle;

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

		UI::AttachedProcessesCount = attached_processes.size();  // NOLINT(clang-diagnostic-shorten-64-to-32)

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

		HANDLE process;

		do
		{
			Sleep(100);
			process = GetRobloxProcess();
		} 		while (!process);

		printf("Found Roblox...\n");
		printf("Attaching...\n");

		if (!RobloxProcess().Attach(process, 0))
		{
			printf("\nERROR: unable to attach to process\n");
			pause();
			return 0;
		}

		CloseHandle(process);

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