#include "app.hpp"
#include "autostart.hpp"
#include "com_util.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <array>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace AcerFan
{
    namespace
    {
        constexpr int EXIT_CODE_OK = 0;
        constexpr int EXIT_CODE_ERROR = 1;
        constexpr int EXIT_CODE_AUTOSTART_FAILED = 2;
        constexpr std::size_t MAX_PATH_CHARS = 32768;
        constexpr const wchar_t *INSTANCE_MUTEX = L"Global\\AcerFanControl";

        std::filesystem::path module_path()
        {
            std::array<wchar_t, MAX_PATH_CHARS> buffer{};
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            return std::filesystem::path{std::wstring_view{buffer.data(), length}};
        }

        // Runs the "--autostart on" or "--autostart off" request of a scripted install. Returns the exit code, or
        // std::nullopt when the command line holds no such request.
        std::optional<int> run_autostart_request(std::wstring_view command_line, const std::filesystem::path &exe)
        {
            const bool on = command_line.find(L"--autostart on") != std::wstring_view::npos;
            const bool off = command_line.find(L"--autostart off") != std::wstring_view::npos;
            if (!on && !off)
            {
                return std::nullopt;
            }
            const Result<void> done = on ? autostart::enable(exe) : autostart::disable();
            return done ? EXIT_CODE_OK : EXIT_CODE_AUTOSTART_FAILED;
        }

        void show_error(const std::string &text) noexcept
        {
            MessageBoxA(nullptr, text.c_str(), "AcerFan", MB_OK | MB_ICONERROR);
        }

        int run(HINSTANCE instance, std::wstring_view command_line)
        {
            const ComApartment apartment{COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE};
            if (const Result<void> joined = apartment.status(); !joined)
            {
                show_error(joined.error().message());
                return EXIT_CODE_ERROR;
            }
            // Process-wide COM security for WMI, before any interface marshalling. A failure means that something in
            // the process already set it, which WMI accepts.
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
            if (const std::optional<int> exit_code = run_autostart_request(command_line, exe))
            {
                return *exit_code;
            }

            // Two controllers fight over the fans, so one instance runs per machine.
            HANDLE raw_mutex = CreateMutexW(nullptr, TRUE, INSTANCE_MUTEX);
            const bool already_running = GetLastError() == ERROR_ALREADY_EXISTS;
            const UniqueKernelHandle mutex{raw_mutex};
            if (!mutex || already_running)
            {
                MessageBoxW(nullptr, L"AcerFan is already running.", L"AcerFan", MB_OK | MB_ICONINFORMATION);
                return EXIT_CODE_OK;
            }

            int exit_code = EXIT_CODE_OK;
            {
                App app{instance, exe};
                exit_code = app.run();
            }
            ReleaseMutex(mutex.get());
            return exit_code;
        }
    } // namespace
} // namespace AcerFan

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int)
{
    try
    {
        return AcerFan::run(instance, command_line);
    }
    catch (const std::exception &error)
    {
        AcerFan::show_error(error.what());
        return 1;
    }
}
