#pragma once

// ReSharper disable CppClangTidyCppcoreguidelinesMacroUsage
#define RFU_VERSION "5.5.0"
#define RFU_GITHUB_REPO "lewisakura/RFU"
#define RFU_REGKEY "RFU"
// ReSharper enable CppClangTidyCppcoreguidelinesMacroUsage

bool CheckForUpdates();
bool RunsOnStartup();
void SetRunOnStartup(bool shouldRun);
void RFU_SetFPSCap(double value);
void RFU_OnUIClose();
void RFU_OnUIUnlockMethodChange();
