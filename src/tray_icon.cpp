#include "tray_icon.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <vector>

namespace AcerFan
{
    namespace
    {
        constexpr UINT ICON_ID = 1;
        constexpr WORD BITS_PER_PIXEL = 32;
        constexpr int FONT_HEIGHT_NUMERATOR = 13;
        constexpr int FONT_HEIGHT_DENOMINATOR = 16;
        constexpr int CORNER_RADIUS_DIVISOR = 8;
        constexpr std::uint32_t CHANNEL_MASK = 0xFF;
        constexpr int FULL_COVERAGE = 255;
        constexpr std::uint32_t OPAQUE_ALPHA = 0xFF000000u;
        constexpr int RED_SHIFT = 16;
        constexpr int GREEN_SHIFT = 8;
        // A monochrome bitmap row is padded to a whole number of 16-bit words.
        constexpr int MASK_ROW_ALIGN_BITS = 16;
        constexpr int BYTES_PER_WORD = 2;

        std::uint8_t blend(std::uint8_t from, std::uint8_t to, std::uint32_t coverage) noexcept
        {
            const int delta = static_cast<int>(to) - static_cast<int>(from);
            return static_cast<std::uint8_t>(from + delta * static_cast<int>(coverage) / FULL_COVERAGE);
        }

        // True when (x, y) lies outside a square of `size` whose corners have the radius `radius`.
        bool outside_rounded(int x, int y, int size, int radius) noexcept
        {
            const int cx = x < radius ? radius : (x >= size - radius ? size - radius - 1 : x);
            const int cy = y < radius ? radius : (y >= size - radius ? size - radius - 1 : y);
            const int dx = x - cx;
            const int dy = y - cy;
            return dx * dx + dy * dy > radius * radius;
        }
    } // namespace

    UniqueIcon render_badge_icon(std::wstring_view text, BadgeColors colors)
    {
        const int size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem());

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = size;
        // A negative height makes a top-down bitmap.
        info.bmiHeader.biHeight = -size;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = BITS_PER_PIXEL;
        info.bmiHeader.biCompression = BI_RGB;

        HDC screen = GetDC(nullptr);
        const UniqueDc dc{CreateCompatibleDC(screen)};
        ReleaseDC(nullptr, screen);
        if (!dc)
        {
            return {};
        }
        void *bits = nullptr;
        const UniqueBitmap color{CreateDIBSection(dc.get(), &info, DIB_RGB_COLORS, &bits, nullptr, 0)};
        if (!color || bits == nullptr)
        {
            return {};
        }

        // GDI text ignores alpha. Draw white on black to get the glyph coverage, then compose the pixels by hand.
        LOGFONTW font_desc{};
        font_desc.lfHeight = -(size * FONT_HEIGHT_NUMERATOR / FONT_HEIGHT_DENOMINATOR);
        font_desc.lfWeight = FW_BOLD;
        font_desc.lfQuality = ANTIALIASED_QUALITY;
        wcscpy_s(font_desc.lfFaceName, L"Segoe UI");
        const UniqueFont font{CreateFontIndirectW(&font_desc)};

        const HGDIOBJ old_bitmap = SelectObject(dc.get(), color.get());
        const HGDIOBJ old_font = SelectObject(dc.get(), font.get());
        RECT rect{0, 0, size, size};
        FillRect(dc.get(), &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetTextColor(dc.get(), RGB(255, 255, 255));
        SetBkMode(dc.get(), TRANSPARENT);
        const UINT draw_flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
        DrawTextW(dc.get(), text.data(), static_cast<int>(text.size()), &rect, draw_flags);
        GdiFlush();
        // GDI deletes no object that a device context still selects.
        SelectObject(dc.get(), old_font);
        SelectObject(dc.get(), old_bitmap);

        const int radius = std::max(1, size / CORNER_RADIUS_DIVISOR);
        auto *pixels = static_cast<std::uint32_t *>(bits);
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                std::uint32_t &pixel = pixels[y * size + x];
                if (outside_rounded(x, y, size, radius))
                {
                    // A fully transparent corner.
                    pixel = 0;
                    continue;
                }
                const std::uint32_t coverage = pixel & CHANNEL_MASK;
                const std::uint8_t r = blend(GetRValue(colors.background), GetRValue(colors.foreground), coverage);
                const std::uint8_t g = blend(GetGValue(colors.background), GetGValue(colors.foreground), coverage);
                const std::uint8_t b = blend(GetBValue(colors.background), GetBValue(colors.foreground), coverage);
                pixel = OPAQUE_ALPHA | (static_cast<std::uint32_t>(r) << RED_SHIFT) |
                        (static_cast<std::uint32_t>(g) << GREEN_SHIFT) | b;
            }
        }

        // The color bitmap carries alpha, so Windows ignores the mask. The mask only has to exist and be all zero.
        const int row_bytes = (size + MASK_ROW_ALIGN_BITS - 1) / MASK_ROW_ALIGN_BITS * BYTES_PER_WORD;
        const std::vector<std::uint8_t> zeros(static_cast<std::size_t>(row_bytes) * static_cast<std::size_t>(size), 0);
        const UniqueBitmap mask{CreateBitmap(size, size, 1, 1, zeros.data())};
        ICONINFO icon_info{};
        icon_info.fIcon = TRUE;
        icon_info.hbmMask = mask.get();
        icon_info.hbmColor = color.get();
        return UniqueIcon{CreateIconIndirect(&icon_info)};
    }

    TrayIcon::TrayIcon(HWND owner, UINT callback_message) noexcept
    {
        m_data.cbSize = sizeof(m_data);
        m_data.hWnd = owner;
        m_data.uID = ICON_ID;
        m_data.uCallbackMessage = callback_message;
    }

    TrayIcon::~TrayIcon() noexcept
    {
        if (m_added)
        {
            Shell_NotifyIconW(NIM_DELETE, &m_data);
        }
    }

    void TrayIcon::add() noexcept
    {
        m_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        m_added = Shell_NotifyIconW(NIM_ADD, &m_data) != FALSE;
        if (m_added)
        {
            m_data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &m_data);
        }
    }

    void TrayIcon::update(HICON icon, std::wstring_view tooltip) noexcept
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
            // Explorer lost the icon. The next update adds it again.
            m_added = false;
        }
    }

    void TrayIcon::recreate() noexcept
    {
        m_added = false;
        add();
    }
} // namespace AcerFan
