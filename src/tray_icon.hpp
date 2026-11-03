/**
 * @file tray_icon.hpp
 * @brief The notification-area icon and the temperature badge that it shows.
 */
#ifndef ACERFAN_TRAY_ICON_HPP
#define ACERFAN_TRAY_ICON_HPP

#include "win_handle.hpp"

#include <windows.h>
#include <shellapi.h>

#include <string_view>

namespace AcerFan
{
    /**
     * @brief The colors of a tray badge.
     */
    struct BadgeColors
    {
        /// Fill of the rounded square.
        COLORREF background{RGB(0, 0, 0)};
        /// Color of the text.
        COLORREF foreground{RGB(255, 255, 255)};
    };

    /**
     * @brief Draws `text`, one or two characters, on a rounded square at the size of a small icon.
     * @return The icon, or an empty UniqueIcon when GDI fails.
     */
    [[nodiscard]] UniqueIcon render_badge_icon(std::wstring_view text, BadgeColors colors);

    /**
     * @brief One notification-area icon of a window. The destructor removes it.
     */
    class TrayIcon
    {
    public:
        /**
         * @brief Prepares the icon. The first update() adds it.
         * @details Mouse and keyboard events arrive at `owner` as `callback_message`, in the layout of
         *          NOTIFYICON_VERSION_4.
         */
        TrayIcon(HWND owner, UINT callback_message) noexcept;

        /** @brief Removes the icon. */
        ~TrayIcon() noexcept;

        TrayIcon(const TrayIcon &) = delete;
        TrayIcon &operator=(const TrayIcon &) = delete;
        TrayIcon(TrayIcon &&) = delete;
        TrayIcon &operator=(TrayIcon &&) = delete;

        /**
         * @brief Shows `icon` with `tooltip`, and adds the icon first when needed.
         * @details The tray keeps no reference to `icon` after the call, so the caller can destroy it later.
         */
        void update(HICON icon, std::wstring_view tooltip) noexcept;

        /** @brief Adds the icon again after an Explorer restart (TaskbarCreated). */
        void recreate() noexcept;

    private:
        void add() noexcept;

        NOTIFYICONDATAW m_data{};
        bool m_added{false};
    };
} // namespace AcerFan

#endif // ACERFAN_TRAY_ICON_HPP
