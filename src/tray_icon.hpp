/**
 * @file tray_icon.hpp
 * @brief Notification-area icon and the temperature badge drawn into it.
 */
#ifndef ACERFAN_TRAY_ICON_HPP
#define ACERFAN_TRAY_ICON_HPP

#include <windows.h>
#include <shellapi.h>

#include <string_view>
#include <utility>

namespace AcerFan
{
    /// Owning HICON.
    class UniqueIcon
    {
    public:
        UniqueIcon() = default;
        explicit UniqueIcon(HICON icon) noexcept : m_icon(icon) {}
        ~UniqueIcon() { reset(); }

        UniqueIcon(const UniqueIcon &) = delete;
        UniqueIcon &operator=(const UniqueIcon &) = delete;
        UniqueIcon(UniqueIcon &&other) noexcept : m_icon(std::exchange(other.m_icon, nullptr)) {}
        UniqueIcon &operator=(UniqueIcon &&other) noexcept
        {
            if (this != &other)
            {
                reset();
                m_icon = std::exchange(other.m_icon, nullptr);
            }
            return *this;
        }

        void reset() noexcept
        {
            if (m_icon != nullptr)
            {
                DestroyIcon(m_icon);
                m_icon = nullptr;
            }
        }

        [[nodiscard]] HICON get() const noexcept { return m_icon; }

    private:
        HICON m_icon{nullptr};
    };

    /// Draws `text` (one or two characters) in `foreground` on a rounded `background` square, tray-icon sized.
    [[nodiscard]] UniqueIcon render_badge_icon(std::wstring_view text, COLORREF background, COLORREF foreground);

    /// One notification-area icon owned by a window. Removed again on destruction.
    class TrayIcon
    {
    public:
        /// Mouse and keyboard events arrive at `owner` as `callback_message` (NOTIFYICON_VERSION_4 layout).
        TrayIcon(HWND owner, UINT callback_message);
        ~TrayIcon();

        TrayIcon(const TrayIcon &) = delete;
        TrayIcon &operator=(const TrayIcon &) = delete;
        TrayIcon(TrayIcon &&) = delete;
        TrayIcon &operator=(TrayIcon &&) = delete;

        /// Shows `icon` with `tooltip`, adding the icon first if needed.
        void update(HICON icon, std::wstring_view tooltip);

        /// Adds the icon again after Explorer restarted (TaskbarCreated).
        void recreate();

    private:
        void add();

        NOTIFYICONDATAW m_data{};
        bool m_added{false};
    };
} // namespace AcerFan

#endif // ACERFAN_TRAY_ICON_HPP
