#pragma once

// Capture the current window client area to a PNG (--shot=path.png). Win32
// only; returns false elsewhere so the caller can log and still quit.
bool captureWindowClientPng(const char* path);
