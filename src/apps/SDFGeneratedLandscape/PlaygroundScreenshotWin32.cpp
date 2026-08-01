#include "pch.h"

#include "PlaygroundScreenshot.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>

#include <cstdint>
#include <vector>

#include <spdlog/spdlog.h>
#include <sokol_app.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

bool captureWindowClientPng(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }

    HWND hwnd = reinterpret_cast<HWND>(const_cast<void*>(sapp_win32_get_hwnd()));
    if (!hwnd) {
        spdlog::error("captureWindowClientPng: missing HWND");
        return false;
    }

    RECT clientRect = {};
    if (!GetClientRect(hwnd, &clientRect)) {
        spdlog::error("captureWindowClientPng: GetClientRect failed");
        return false;
    }

    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) {
        spdlog::error("captureWindowClientPng: invalid client size {}x{}", width, height);
        return false;
    }

    HDC windowDc = GetDC(hwnd);
    if (!windowDc) {
        return false;
    }

    HDC memoryDc = CreateCompatibleDC(windowDc);
    if (!memoryDc) {
        ReleaseDC(hwnd, windowDc);
        return false;
    }

    HBITMAP bitmap = CreateCompatibleBitmap(windowDc, width, height);
    if (!bitmap) {
        DeleteDC(memoryDc);
        ReleaseDC(hwnd, windowDc);
        return false;
    }

    HGDIOBJ previousObject = SelectObject(memoryDc, bitmap);
    DwmFlush();
    const BOOL printed = PrintWindow(hwnd, memoryDc, PW_RENDERFULLCONTENT);
    if (!printed) {
        BitBlt(memoryDc, 0, 0, width, height, windowDc, 0, 0, SRCCOPY);
    }

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    std::vector<std::uint8_t> pixels((std::size_t)width * (std::size_t)height * 4);
    if (GetDIBits(memoryDc, bitmap, 0, (UINT)height, pixels.data(), &bitmapInfo, DIB_RGB_COLORS) == 0) {
        SelectObject(memoryDc, previousObject);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(hwnd, windowDc);
        spdlog::error("captureWindowClientPng: GetDIBits failed");
        return false;
    }

    // BGRA -> RGBA for stb_image_write.
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        const std::uint8_t b = pixels[i + 0];
        pixels[i + 0] = pixels[i + 2];
        pixels[i + 2] = b;
    }

    const int written = stbi_write_png(path, width, height, 4, pixels.data(), width * 4);
    SelectObject(memoryDc, previousObject);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(hwnd, windowDc);

    if (!written) {
        spdlog::error("captureWindowClientPng: stbi_write_png failed for {}", path);
        return false;
    }

    spdlog::info("captureWindowClientPng: saved {}x{} to {}", width, height, path);
    return true;
}

#else

bool captureWindowClientPng(const char* /*path*/) {
    return false;
}

#endif
