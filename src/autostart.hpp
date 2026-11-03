/**
 * @file autostart.hpp
 * @brief "Start with Windows" through a Task Scheduler logon task.
 * @details AcerFan needs administrator rights. A Run-key entry prompts UAC at every logon. A logon task with the
 *          highest run level starts AcerFan elevated without a prompt.
 */
#ifndef ACERFAN_AUTOSTART_HPP
#define ACERFAN_AUTOSTART_HPP

#include "error.hpp"

#include <filesystem>

namespace AcerFan::autostart
{
    /** @brief Returns true when the AcerFan logon task exists and starts `exe`. */
    [[nodiscard]] bool is_enabled(const std::filesystem::path &exe);

    /** @brief Creates or replaces the logon task that starts `exe` for the current user. */
    [[nodiscard]] Result<void> enable(const std::filesystem::path &exe);

    /** @brief Removes the logon task. A missing task counts as success. */
    [[nodiscard]] Result<void> disable();
} // namespace AcerFan::autostart

#endif // ACERFAN_AUTOSTART_HPP
