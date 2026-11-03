/**
 * @file text.hpp
 * @brief Locale-independent number parsers for the settings file and the firmware output.
 */
#ifndef ACERFAN_TEXT_HPP
#define ACERFAN_TEXT_HPP

#include <cstdint>
#include <optional>
#include <string_view>

namespace AcerFan
{
    /** @brief Returns `text` without its leading and trailing spaces and tabs. */
    [[nodiscard]] std::string_view trim(std::string_view text) noexcept;

    /**
     * @brief Parses a decimal integer. Spaces and tabs around the number are allowed.
     * @return The value, or std::nullopt for an empty, non-numeric, partly numeric or out-of-range text.
     */
    [[nodiscard]] std::optional<int> parse_int(std::string_view text) noexcept;

    /**
     * @brief Parses an unsigned 64-bit decimal integer with the rules of parse_int().
     * @return The value, or std::nullopt. A sign is not allowed.
     */
    [[nodiscard]] std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept;
} // namespace AcerFan

#endif // ACERFAN_TEXT_HPP
