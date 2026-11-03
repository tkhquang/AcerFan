/**
 * @file logger.hpp
 * @brief Thread-safe, size-capped text log next to the executable.
 */
#ifndef ACERFAN_LOGGER_HPP
#define ACERFAN_LOGGER_HPP

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string_view>

namespace AcerFan
{
    class Logger
    {
    public:
        /// Logs to `file`; once it grows past `max_bytes` it is renamed to "<file>.old" and a new one starts.
        explicit Logger(std::filesystem::path file, std::uintmax_t max_bytes = 1024 * 1024);

        /// Appends one timestamped line. Never throws; a failed write is dropped.
        void write(std::string_view message) noexcept;

        [[nodiscard]] const std::filesystem::path &file() const noexcept { return m_file; }

    private:
        std::mutex m_mutex;
        std::filesystem::path m_file;
        std::uintmax_t m_max_bytes;
    };
} // namespace AcerFan

#endif // ACERFAN_LOGGER_HPP
