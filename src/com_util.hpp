/**
 * @file com_util.hpp
 * @brief Small RAII helpers for COM: errors, BSTR, VARIANT, apartment, and UTF-8 conversion.
 */
#ifndef ACERFAN_COM_UTIL_HPP
#define ACERFAN_COM_UTIL_HPP

#include <windows.h>
#include <oleauto.h>

#include <format>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace AcerFan
{
    /// A failed COM, WMI or firmware call, with its HRESULT.
    class ComError : public std::runtime_error
    {
    public:
        ComError(const std::string &what, HRESULT hr)
            : std::runtime_error(std::format("{} (0x{:08X})", what, static_cast<unsigned long>(hr))), m_code(hr)
        {
        }

        [[nodiscard]] HRESULT code() const noexcept { return m_code; }

    private:
        HRESULT m_code;
    };

    /// Throws ComError when `hr` is a failure code.
    inline void check_hr(HRESULT hr, const std::string &what)
    {
        if (FAILED(hr))
        {
            throw ComError(what, hr);
        }
    }

    /// Owning BSTR.
    class Bstr
    {
    public:
        explicit Bstr(std::wstring_view text)
            : m_value(SysAllocStringLen(text.data(), static_cast<UINT>(text.size())))
        {
            if (m_value == nullptr)
            {
                throw std::bad_alloc();
            }
        }

        ~Bstr() { SysFreeString(m_value); }

        Bstr(const Bstr &) = delete;
        Bstr &operator=(const Bstr &) = delete;
        Bstr(Bstr &&) = delete;
        Bstr &operator=(Bstr &&) = delete;

        [[nodiscard]] BSTR get() const noexcept { return m_value; }

    private:
        BSTR m_value;
    };

    /// VARIANT that clears itself.
    class Variant
    {
    public:
        Variant() noexcept { VariantInit(&m_value); }
        ~Variant() { VariantClear(&m_value); }

        Variant(const Variant &) = delete;
        Variant &operator=(const Variant &) = delete;
        Variant(Variant &&) = delete;
        Variant &operator=(Variant &&) = delete;

        /// Clears the current value and returns the VARIANT for an out parameter.
        [[nodiscard]] VARIANT *put() noexcept
        {
            VariantClear(&m_value);
            return &m_value;
        }

        [[nodiscard]] const VARIANT &get() const noexcept { return m_value; }

    private:
        VARIANT m_value;
    };

    /// Joins the calling thread to a COM apartment for the object's lifetime.
    class ComApartment
    {
    public:
        explicit ComApartment(DWORD model)
        {
            const HRESULT hr = CoInitializeEx(nullptr, model);
            m_initialized = SUCCEEDED(hr);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
            {
                throw ComError("CoInitializeEx", hr);
            }
        }

        ~ComApartment()
        {
            if (m_initialized)
            {
                CoUninitialize();
            }
        }

        ComApartment(const ComApartment &) = delete;
        ComApartment &operator=(const ComApartment &) = delete;
        ComApartment(ComApartment &&) = delete;
        ComApartment &operator=(ComApartment &&) = delete;

    private:
        bool m_initialized{false};
    };

    /// UTF-16 to UTF-8.
    [[nodiscard]] inline std::string to_utf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }
        const int size =
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
        return out;
    }

    /// UTF-8 to UTF-16.
    [[nodiscard]] inline std::wstring to_wide(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
        return out;
    }
} // namespace AcerFan

#endif // ACERFAN_COM_UTIL_HPP
