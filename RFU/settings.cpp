#include "settings.h"
#include "mapping.h"

#include <string>
#include <fstream>
#include <vector>

#include "rfu.h"

FileMapping ipc; // NOLINT(clang-diagnostic-exit-time-destructors)

const char* advance(const char* ptr)
{
    while (isspace(*ptr)) ptr++;
    return ptr;
}

// scuffed array parse. e.g. [1, 2, 3, 4, 5]
std::vector<double> ParseDoubleArray(const std::string& value, size_t max_elements = 0)
{
    std::vector<double> result;

    auto ptr = advance(value.c_str());
    if (*ptr != '[') throw std::invalid_argument("unexpected character");

    while (++ptr < value.c_str() + value.size())
    {
        ptr = advance(ptr);
        if (*ptr == ']') break;

        errno = 0;

        char* end_ptr;
        double element = std::strtod(ptr, &end_ptr);
        if (errno != 0) throw std::invalid_argument("conversion error");
        if (std::isnan(element)) throw std::invalid_argument("element is nan");
        if (std::isinf(element)) throw std::invalid_argument("element is infinite");

        if (max_elements == 0 || result.size() < max_elements) result.push_back(element);

        ptr = advance(end_ptr);
        if (*ptr == ']') break;
        if (*ptr != ',') throw std::invalid_argument("unexpected character");
    }

    return result;
}

bool ParseBool(const std::string& value)
{
    if (_stricmp(value.c_str(), "true") == 0) return true;
    if (_stricmp(value.c_str(), "false") == 0) return false;
    return std::stoi(value) != 0;
}

std::string BoolToString(bool value)
{
    return value ? "true" : "false";
}

std::string DoubleArrayToString(const std::vector<double>& array)
{
    std::string buffer = "[";
    for (size_t i = 0; i < array.size(); i++)
    {
        if (i > 0) buffer += ", ";
        buffer += std::to_string(array[i]);
    }
    buffer += "]";
    return buffer;
}

namespace Settings
{
    bool VSyncEnabled = false;
    std::vector<double> FPSCapValues = {30, 60, 75, 120, 144, 165, 240, 360};
    uint32_t FPSCapSelection = 0;
    double FPSCap = 0.0;
    bool UnlockClient = true;
    bool UnlockStudio = false;
    bool CheckForUpdates = true;
    bool NonBlockingErrors = true;
    bool SilentErrors = false;
    UnlockMethodType UnlockMethod = UnlockMethodType::Hybrid;

    bool Init()
    {
        if (!Load()) Save();
        Update();
        return true;
    }

    // very basic settings parser/writer

    bool Load()
    {
        std::ifstream file("settings");
        if (!file.is_open()) return false;

        printf("Loading settings from file...\n");

        std::string line;

        while (std::getline(file, line))
        {
            const auto eq = line.find('=');
            if (eq != std::string::npos)
            {
                auto key = line.substr(0, eq);
                auto value = line.substr(eq + 1);

                try
                {
                    if (key == "VSyncEnabled")
                        VSyncEnabled = ParseBool(value);
                    else if (key == "FPSCapValues")
                        FPSCapValues = ParseDoubleArray(value, 100);
                    else if (key == "FPSCapSelection")
                        FPSCapSelection = std::stoul(value);
                    else if (key == "FPSCap")
                        FPSCap = std::stod(value);
                    else if (key == "UnlockClient")
                        UnlockClient = ParseBool(value);
                    else if (key == "UnlockStudio")
                        UnlockStudio = ParseBool(value);
                    else if (key == "CheckForUpdates")
                        CheckForUpdates = ParseBool(value);
                    else if (key == "NonBlockingErrors")
                        NonBlockingErrors = ParseBool(value);
                    else if (key == "SilentErrors")
                        SilentErrors = ParseBool(value);
                    else if (key == "UnlockMethod")
                        if (auto parsed = std::stoul(value); parsed < static_cast<uint32_t>(UnlockMethodType::Count))
                            UnlockMethod = static_cast<UnlockMethodType>(parsed);
                }
                catch ([[maybe_unused]] std::exception& e)
                {
                    // catch string conversion errors
                }
            }
        }

        if (FPSCapSelection > 0 && FPSCapSelection > FPSCapValues.size())
        {
            FPSCapSelection = 0;
        }

        for (auto& value : FPSCapValues)
        {
            value = std::fmin(std::fmax(value, -2147483648.0), 2147483647.0);
        }

        FPSCap = FPSCapSelection == 0 ? 0.0 : FPSCapValues[FPSCapSelection - 1];

        Update();

        return true;
    }

    bool Save()
    {
        std::ofstream file("settings");
        if (!file.is_open()) return false;

        printf("Saving settings to file...\n");

        file << "UnlockClient=" << BoolToString(UnlockClient) << std::endl;
        file << "UnlockStudio=" << BoolToString(UnlockStudio) << std::endl;
        file << "FPSCapValues=" << DoubleArrayToString(FPSCapValues) << std::endl;
        file << "FPSCapSelection=" << std::to_string(FPSCapSelection) << std::endl;
        file << "FPSCap=" << std::to_string(FPSCap) << std::endl;
        file << "CheckForUpdates=" << BoolToString(CheckForUpdates) << std::endl;
        file << "NonBlockingErrors=" << BoolToString(NonBlockingErrors) << std::endl;
        file << "SilentErrors=" << BoolToString(SilentErrors) << std::endl;
        file << "UnlockMethod=" << std::to_string(static_cast<uint32_t>(UnlockMethod)) << std::endl;

        return true;
    }

    bool Update()
    {
        RFU_SetFPSCap(FPSCap);
        return true;
    }
}
