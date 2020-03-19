#pragma once

#define RFU_VERSION "5.0.1"
#define RFU_GITHUB_REPO "LewisTehMinerz/RFU"
#define RFU_REGKEY "RFU"

bool CheckForUpdates();
bool RunsOnStartup();
void SetRunOnStartup(bool shouldRun);
void SetFPSCapExternal(double value);