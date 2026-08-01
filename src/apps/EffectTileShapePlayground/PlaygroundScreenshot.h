#pragma once

// Win32 GDI capture of the window client area, used by the --shot path on
// Windows; returns false on every other platform. The portable entry point is
// capturePlaygroundPng() in main.cpp, which reads the GL framebuffer back
// directly where the backend is GL and falls back to this one otherwise.
bool captureWindowClientPng(const char* path);
