#include "logger.hpp"

#include <windows.h>

#include <format>
#include <fstream>
#include <system_error>
#include <utility>

namespace AcerFan
{
    Logger::Logger(std::filesystem::path file, std::uintmax_t max_bytes) noexcept
        : m_file(std::move(file)), m_max_bytes(max_bytes)
    {
    }

    void Logger::write(std::string_view message) noexcept
    {
        try
        {
            const std::lock_guard lock(m_mutex);

            std::error_code error;
            if (std::filesystem::file_size(m_file, error) > m_max_bytes && !error)
            {
                std::filesystem::path old = m_file;
                old += ".old";
                std::filesystem::rename(m_file, old, error);
            }

            SYSTEMTIME now{};
            GetLocalTime(&now);
            std::ofstream out(m_file, std::ios::app | std::ios::binary);
            out << std::format(
                "{:04}-{:02}-{:02} {:02}:{:02}:{:02}  {}\r\n",
                now.wYear,
                now.wMonth,
                now.wDay,
                now.wHour,
                now.wMinute,
                now.wSecond,
                message
            );
        }
        catch (...)
        {
            // A log failure must never stop the fan control.
        }
    }
} // namespace AcerFan
