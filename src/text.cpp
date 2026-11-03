#include "text.hpp"

#include <charconv>
#include <system_error>

namespace AcerFan
{
    namespace
    {
        constexpr std::string_view BLANKS = " \t";

        template <class T> std::optional<T> parse_number(std::string_view text) noexcept
        {
            text = trim(text);
            T value{};
            const char *const end = text.data() + text.size();
            const auto [stop, error] = std::from_chars(text.data(), end, value);
            if (text.empty() || error != std::errc{} || stop != end)
            {
                return std::nullopt;
            }
            return value;
        }
    } // namespace

    std::string_view trim(std::string_view text) noexcept
    {
        const auto first = text.find_first_not_of(BLANKS);
        if (first == std::string_view::npos)
        {
            return {};
        }
        const auto last = text.find_last_not_of(BLANKS);
        return text.substr(first, last - first + 1);
    }

    std::optional<int> parse_int(std::string_view text) noexcept
    {
        return parse_number<int>(text);
    }

    std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept
    {
        return parse_number<std::uint64_t>(text);
    }
} // namespace AcerFan
