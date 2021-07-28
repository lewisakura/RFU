#pragma once

#include <cstdint>
#include <Shlwapi.h>

namespace sigscan
{
	bool compare(const char* location, const char* aob, const char* mask);
	bool compare_reverse(const char* location, const char* aob, const char* mask);
	uint8_t* scan(const char* aob, const char* mask, uintptr_t start, uintptr_t end);
	uint8_t* scan(LPCWSTR hmodule, const char* aob, const char* mask);
}
