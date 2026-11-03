#include "app.hpp"
#include "autostart.hpp"
#include "com_util.hpp"

#include <windows.h>

#include <array>
#include <exception>
#include <filesystem>
#include <string_view>

namespace
{
    std::filesystem::path module_path()
    {
        std::array<wchar_t, 32768> buffer{};
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        return std::filesystem::path{std::wstring_view{buffer.data(), length}};
    }

    /// Handles --autostart on|off for scripted installs. Returns true when the command line was such a request.
    bool handle_command_line(std::wstring_view command_line, const std::filesystem::path &exe, int &exit_code)
    {
        const bool on = command_line.find(L"--autostart on") != std::wstring_view::npos;
        const bool off = command_line.find(L"--autostart off") != std::wstring_view::npos;
        if (!on && !off)
        {
            return false;
        }
        try
        {
            if (on)
            {
                AcerFan::autostart::enable(exe);
            }
            else
            {
                AcerFan::autostart::disable();
            }
            exit_code = 0;
        }
        catch (const AcerFan::ComError &)
        {
            exit_code = 2;
        }
        return true;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int)
{
    try
    {
        const AcerFan::ComApartment apartment{COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE};
        // Process-wide COM security for WMI; must precede any interface marshalling. Failure (already set) is fine.
        CoInitializeSecurity(
            nullptr,
            -1,
            nullptr,
            nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE,
            nullptr
        );

        const std::filesystem::path exe = module_path();
        if (int exit_code = 0; handle_command_line(command_line, exe, exit_code))
        {
            return exit_code;
        }

        // One instance per machine: two controllers would fight over the fans.
        HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Global\\AcerFanControl");
        if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            MessageBoxW(nullptr, L"AcerFan is already running.", L"AcerFan", MB_OK | MB_ICONINFORMATION);
            if (mutex != nullptr)
            {
                CloseHandle(mutex);
            }
            return 0;
        }

        int result = 0;
        {
            AcerFan::App app{instance, exe};
            result = app.run();
        }
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return result;
    }
    catch (const std::exception &error)
    {
        MessageBoxA(nullptr, error.what(), "AcerFan", MB_OK | MB_ICONERROR);
        return 1;
    }
}
