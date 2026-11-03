/**
 * @file win_handle.hpp
 * @brief std::unique_ptr aliases that own Win32 handles.
 */
#ifndef ACERFAN_WIN_HANDLE_HPP
#define ACERFAN_WIN_HANDLE_HPP

#include <windows.h>

#include <memory>
#include <type_traits>

namespace AcerFan
{
    /**
     * @brief Closes a kernel object handle.
     */
    struct KernelHandleDeleter
    {
        /// Calls CloseHandle.
        void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
    };

    /**
     * @brief Deletes a GDI object such as a bitmap or a font.
     * @warning Select the object out of every device context first. GDI refuses to delete a selected object.
     */
    struct GdiObjectDeleter
    {
        /// Calls DeleteObject.
        void operator()(HGDIOBJ object) const noexcept { DeleteObject(object); }
    };

    /**
     * @brief Deletes a memory device context.
     */
    struct DcDeleter
    {
        /// Calls DeleteDC.
        void operator()(HDC dc) const noexcept { DeleteDC(dc); }
    };

    /**
     * @brief Destroys an icon.
     */
    struct IconDeleter
    {
        /// Calls DestroyIcon.
        void operator()(HICON icon) const noexcept { DestroyIcon(icon); }
    };

    /**
     * @brief Destroys a menu.
     */
    struct MenuDeleter
    {
        /// Calls DestroyMenu.
        void operator()(HMENU menu) const noexcept { DestroyMenu(menu); }
    };

    /// Owns a Win32 handle of type `Handle`, a pointer type, and releases it through `Deleter`.
    template <class Handle, class Deleter> using UniqueHandle = std::unique_ptr<std::remove_pointer_t<Handle>, Deleter>;

    /// Owns a kernel object handle such as a mutex.
    using UniqueKernelHandle = UniqueHandle<HANDLE, KernelHandleDeleter>;
    /// Owns a memory device context.
    using UniqueDc = UniqueHandle<HDC, DcDeleter>;
    /// Owns a bitmap.
    using UniqueBitmap = UniqueHandle<HBITMAP, GdiObjectDeleter>;
    /// Owns a font.
    using UniqueFont = UniqueHandle<HFONT, GdiObjectDeleter>;
    /// Owns an icon.
    using UniqueIcon = UniqueHandle<HICON, IconDeleter>;
    /// Owns a menu.
    using UniqueMenu = UniqueHandle<HMENU, MenuDeleter>;
} // namespace AcerFan

#endif // ACERFAN_WIN_HANDLE_HPP
