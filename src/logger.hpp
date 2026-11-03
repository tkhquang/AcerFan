/**
 * @file logger.hpp
 * @brief The thread-safe text log next to the executable.
 */
#ifndef ACERFAN_LOGGER_HPP
#define ACERFAN_LOGGER_HPP

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string_view>

namespace AcerFan
{
    /**
     * @brief A thread-safe text log with a size cap.
     * @details Before a line, a file larger than the cap moves to "<file>.old", which replaces an older copy.
     */
    class Logger
    {
    public:
        /// Default size cap of the log file, in bytes.
        static constexpr std::uintmax_t DEFAULT_MAX_BYTES{1024 * 1024};

        /** @brief Logs to `file` with the size cap `max_bytes`. */
        explicit Logger(std::filesystem::path file, std::uintmax_t max_bytes = DEFAULT_MAX_BYTES) noexcept;

        /** @brief Appends one timestamped line. A failed write drops the line. */
        void write(std::string_view message) noexcept;

        /// Returns the path of the log file.
        [[nodiscard]] const std::filesystem::path &file() const noexcept { return m_file; }

    private:
        std::mutex m_mutex;
        std::filesystem::path m_file;
        std::uintmax_t m_max_bytes{DEFAULT_MAX_BYTES};
    };
} // namespace AcerFan

#endif // ACERFAN_LOGGER_HPP
