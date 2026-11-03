#include "tray_icon.hpp"

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <vector>

namespace AcerFan
{
    namespace
    {
        constexpr UINT icon_id = 1;

        std::uint8_t blend(std::uint8_t from, std::uint8_t to, std::uint32_t coverage) noexcept
        {
            return static_cast<std::uint8_t>(from + (static_cast<int>(to) - from) * static_cast<int>(coverage) / 255);
        }

        /// True when (x, y) lies outside a square with corners rounded by `radius`.
        bool outside_rounded(int x, int y, int size, int radius) noexcept
        {
            const int cx = x < radius ? radius : (x >= size - radius ? size - radius - 1 : x);
            const int cy = y < radius ? radius : (y >= size - radius ? size - radius - 1 : y);
            const int dx = x - cx;
            const int dy = y - cy;
            return dx * dx + dy * dy > radius * radius;
        }
    } // namespace

    UniqueIcon render_badge_icon(std::wstring_view text, COLORREF background, COLORREF foreground)
    {
        const int size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem());

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = size;
        info.bmiHeader.biHeight = -size; // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        void *bits = nullptr;
        HBITMAP color = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dc == nullptr || color == nullptr || bits == nullptr)
        {
            if (color != nullptr)
            {
                DeleteObject(color);
            }
            if (dc != nullptr)
            {
                DeleteDC(dc);
            }
            return UniqueIcon{};
        }
        HGDIOBJ old_bitmap = SelectObject(dc, color);

        // GDI text ignores alpha, so draw white-on-black to get the glyph coverage, then compose by hand.
        RECT rect{0, 0, size, size};
        FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        LOGFONTW font_desc{};
        font_desc.lfHeight = -(size * 13 / 16);
        font_desc.lfWeight = FW_BOLD;
        font_desc.lfQuality = ANTIALIASED_QUALITY;
        wcscpy_s(font_desc.lfFaceName, L"Segoe UI");
        HFONT font = CreateFontIndirectW(&font_desc);
        HGDIOBJ old_font = SelectObject(dc, font);
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(
            dc,
            text.data(),
            static_cast<int>(text.size()),
            &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
        );
        GdiFlush();

        const int radius = std::max(1, size / 8);
        auto *pixels = static_cast<std::uint32_t *>(bits);
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                std::uint32_t &pixel = pixels[y * size + x];
                if (outside_rounded(x, y, size, radius))
                {
                    pixel = 0; // fully transparent corner
                    continue;
                }
                const std::uint32_t coverage = pixel & 0xFF;
                const std::uint8_t r = blend(GetRValue(background), GetRValue(foreground), coverage);
                const std::uint8_t g = blend(GetGValue(background), GetGValue(foreground), coverage);
                const std::uint8_t b = blend(GetBValue(background), GetBValue(foreground), coverage);
                pixel = 0xFF000000u | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | b;
            }
        }

        SelectObject(dc, old_font);
        DeleteObject(font);
        SelectObject(dc, old_bitmap);

        // The colour bitmap carries alpha, so the mask is unused; it only has to exist and be all zero.
        const std::vector<std::uint8_t> zeros(static_cast<std::size_t>(((size + 15) / 16) * 2 * size), 0);
        HBITMAP mask = CreateBitmap(size, size, 1, 1, zeros.data());
        ICONINFO icon_info{};
        icon_info.fIcon = TRUE;
        icon_info.hbmMask = mask;
        icon_info.hbmColor = color;
        UniqueIcon icon{CreateIconIndirect(&icon_info)};

        DeleteObject(mask);
        DeleteObject(color);
        DeleteDC(dc);
        return icon;
    }

    TrayIcon::TrayIcon(HWND owner, UINT callback_message)
    {
        m_data.cbSize = sizeof(m_data);
        m_data.hWnd = owner;
        m_data.uID = icon_id;
        m_data.uCallbackMessage = callback_message;
    }

    TrayIcon::~TrayIcon()
    {
        if (m_added)
        {
            Shell_NotifyIconW(NIM_DELETE, &m_data);
        }
    }

    void TrayIcon::add()
    {
        m_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        m_added = Shell_NotifyIconW(NIM_ADD, &m_data) != FALSE;
        if (m_added)
        {
            m_data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &m_data);
        }
    }

    void TrayIcon::update(HICON icon, std::wstring_view tooltip)
    {
        m_data.hIcon = icon;
        const std::size_t length = std::min(tooltip.size(), std::size(m_data.szTip) - 1);
        std::wmemcpy(m_data.szTip, tooltip.data(), length);
        m_data.szTip[length] = L'\0';
        if (!m_added)
        {
            add();
            return;
        }
        m_data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        if (Shell_NotifyIconW(NIM_MODIFY, &m_data) == FALSE)
        {
            m_added = false; // Explorer lost the icon; add it on the next update.
        }
    }

    void TrayIcon::recreate()
    {
        m_added = false;
        add();
    }
} // namespace AcerFan
