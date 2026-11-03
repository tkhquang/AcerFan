/**
 * @file autostart.hpp
 * @brief "Start with Windows" through a Task Scheduler logon task.
 * @details AcerFan needs administrator rights. A Run-key entry would prompt UAC at every logon; a logon task with
 *          the highest run level starts it elevated without a prompt.
 */
#ifndef ACERFAN_AUTOSTART_HPP
#define ACERFAN_AUTOSTART_HPP

#include <filesystem>

namespace AcerFan::autostart
{
    /// True when the AcerFan logon task exists and starts `exe`.
    [[nodiscard]] bool is_enabled(const std::filesystem::path &exe);

    /// Creates or replaces the logon task for the current user. Throws ComError on failure.
    void enable(const std::filesystem::path &exe);

    /// Removes the logon task if it exists. Throws ComError on failure.
    void disable();
} // namespace AcerFan::autostart

#endif // ACERFAN_AUTOSTART_HPP
