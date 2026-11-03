/**
 * @file com_util.hpp
 * @brief RAII wrappers of BSTR, VARIANT and the COM apartment, and the UTF-8 conversions.
 */
#ifndef ACERFAN_COM_UTIL_HPP
#define ACERFAN_COM_UTIL_HPP

#include "error.hpp"

#include <windows.h>
#include <oleauto.h>

#include <new>
#include <string>
#include <string_view>

namespace AcerFan
{
    /**
     * @brief An owning BSTR.
     */
    class Bstr
    {
    public:
        /** @brief Holds no string, for use as an out parameter through put(). */
        Bstr() noexcept = default;

        /**
         * @brief Copies `text` into a new BSTR.
         * @details A failed allocation throws std::bad_alloc.
         */
        explicit Bstr(std::wstring_view text) : m_value(SysAllocStringLen(text.data(), static_cast<UINT>(text.size())))
        {
            if (m_value == nullptr)
            {
                throw std::bad_alloc();
            }
        }

        /** @brief Frees the string. */
        ~Bstr() noexcept { SysFreeString(m_value); }

        Bstr(const Bstr &) = delete;
        Bstr &operator=(const Bstr &) = delete;
        Bstr(Bstr &&) = delete;
        Bstr &operator=(Bstr &&) = delete;

        /** @brief Frees the current string and returns its slot for an out parameter. */
        [[nodiscard]] BSTR *put() noexcept
        {
            SysFreeString(m_value);
            m_value = nullptr;
            return &m_value;
        }

        /// Returns the owned string, or nullptr.
        [[nodiscard]] BSTR get() const noexcept { return m_value; }

    private:
        BSTR m_value{nullptr};
    };

    /**
     * @brief A VARIANT that clears itself.
     */
    class Variant
    {
    public:
        /** @brief Holds VT_EMPTY. */
        Variant() noexcept { VariantInit(&m_value); }

        /** @brief Clears the value. */
        ~Variant() noexcept { VariantClear(&m_value); }

        Variant(const Variant &) = delete;
        Variant &operator=(const Variant &) = delete;
        Variant(Variant &&) = delete;
        Variant &operator=(Variant &&) = delete;

        /** @brief Clears the current value and returns the VARIANT for an out parameter. */
        [[nodiscard]] VARIANT *put() noexcept
        {
            VariantClear(&m_value);
            return &m_value;
        }

        /// Returns the held value.
        [[nodiscard]] const VARIANT &get() const noexcept { return m_value; }

    private:
        VARIANT m_value{};
    };

    /**
     * @brief Joins the calling thread to a COM apartment for the lifetime of the object.
     */
    class ComApartment
    {
    public:
        /** @brief Calls CoInitializeEx with `model`. status() reports a failure. */
        explicit ComApartment(DWORD model) noexcept : m_hr(CoInitializeEx(nullptr, model)) {}

        /** @brief Leaves the apartment that the constructor joined. */
        ~ComApartment() noexcept
        {
            if (SUCCEEDED(m_hr))
            {
                CoUninitialize();
            }
        }

        ComApartment(const ComApartment &) = delete;
        ComApartment &operator=(const ComApartment &) = delete;
        ComApartment(ComApartment &&) = delete;
        ComApartment &operator=(ComApartment &&) = delete;

        /**
         * @brief Reports whether the thread is in a COM apartment.
         * @details RPC_E_CHANGED_MODE counts as success: the thread already belongs to an apartment of another model.
         */
        [[nodiscard]] Result<void> status() const
        {
            if (FAILED(m_hr) && m_hr != RPC_E_CHANGED_MODE)
            {
                return fail("join a COM apartment", m_hr);
            }
            return {};
        }

    private:
        HRESULT m_hr{E_FAIL};
    };

    /** @brief Converts UTF-16 to UTF-8. */
    [[nodiscard]] inline std::string to_utf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }
        const int length = static_cast<int>(text.size());
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), length, out.data(), size, nullptr, nullptr);
        return out;
    }

    /** @brief Converts UTF-8 to UTF-16. */
    [[nodiscard]] inline std::wstring to_wide(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }
        const int length = static_cast<int>(text.size());
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
        std::wstring out(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), length, out.data(), size);
        return out;
    }
} // namespace AcerFan

#endif // ACERFAN_COM_UTIL_HPP
