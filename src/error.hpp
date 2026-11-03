/**
 * @file error.hpp
 * @brief The Error value and the Result alias of every fallible Windows, COM and firmware call.
 */
#ifndef ACERFAN_ERROR_HPP
#define ACERFAN_ERROR_HPP

#include <windows.h>

#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace AcerFan
{
    /**
     * @brief A failed Windows, COM, WMI or firmware call.
     */
    struct Error
    {
        /// The failed action, for example "connect to root\WMI".
        std::string context;
        /// The HRESULT of the failed call.
        HRESULT code{E_FAIL};

        /** @brief Formats the error as "<context> (0x<code>)" for the log and the tray menu. */
        [[nodiscard]] std::string message() const
        {
            return std::format("{} (0x{:08X})", context, static_cast<unsigned long>(code));
        }
    };

    /// The value of a fallible call, or the Error that stopped it.
    template <class T> using Result = std::expected<T, Error>;

    /** @brief The failure result of `context` with the HRESULT `code`. */
    [[nodiscard]] inline std::unexpected<Error> fail(std::string_view context, HRESULT code)
    {
        return std::unexpected(Error{std::string{context}, code});
    }

    /**
     * @brief The failure result of `context` with the last Win32 error of the calling thread.
     * @warning Call it directly after the failed Win32 call, before another call replaces the last error.
     */
    [[nodiscard]] inline std::unexpected<Error> fail_last_error(std::string_view context)
    {
        const HRESULT code = HRESULT_FROM_WIN32(GetLastError());
        return fail(context, code);
    }

    /**
     * @brief Converts an HRESULT into a Result.
     * @return Success for a success code, else an Error with `context`.
     */
    [[nodiscard]] inline Result<void> check_hr(HRESULT hr, std::string_view context)
    {
        if (FAILED(hr))
        {
            return fail(context, hr);
        }
        return {};
    }
} // namespace AcerFan

/**
 * @brief Declares `var` with the value of a Result, or returns its Error from the enclosing function.
 * @details The enclosing function must return a Result.
 */
#define ACERFAN_TRY(var, expr)                                                                                         \
    auto &&acerfan_try_##var = (expr);                                                                                 \
    if (!acerfan_try_##var)                                                                                            \
        return std::unexpected(std::move(acerfan_try_##var).error());                                                  \
    auto var = std::move(*acerfan_try_##var)

/**
 * @brief Returns the Error of a Result from the enclosing function, and discards the value of a success.
 * @details It expands to one scoped statement, so it is safe as the body of a brace-less `if` or `for`. The enclosing
 *          function must return a Result.
 */
#define ACERFAN_TRY_VOID(expr)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        auto &&acerfan_try_result = (expr);                                                                            \
        if (!acerfan_try_result)                                                                                       \
            return std::unexpected(std::move(acerfan_try_result).error());                                             \
    } while (false)

#endif // ACERFAN_ERROR_HPP
