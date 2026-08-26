
#include <Windows.h>
#include <CommCtrl.h>
#include <Ole2.h>

#include <exception>
#include <string>

import threadspire.ui;

namespace {

[[nodiscard]] std::wstring widen_error(const char* value) {
    if (value == nullptr || *value == '\0') {
        return L"Unknown error";
    }
    const auto size = ::MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (size <= 0) {
        return L"Unable to convert error text";
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    const auto converted = ::MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), size);
    if (converted <= 0) {
        return L"Unable to convert error text";
    }
    result.resize(static_cast<std::size_t>(converted - 1));
    return result;
}

} // namespace

int WINAPI wWinMain(
    const HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    const int show_command) {
    static_cast<void>(::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    const auto ole_result = ::OleInitialize(nullptr);
    try {
        threadspire::ai::app::MainWindow window(instance);
        if (!window.create(show_command)) {
            ::MessageBoxW(nullptr, L"Unable to create the EngCoder window.",
                          L"EngCoder", MB_OK | MB_ICONERROR);
            if (SUCCEEDED(ole_result)) {
                ::OleUninitialize();
            }
            return 1;
        }

        MSG message{};
        while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (::IsDialogMessageW(window.handle(), &message) == FALSE) {
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
            }
        }
        if (SUCCEEDED(ole_result)) {
            ::OleUninitialize();
        }
        return static_cast<int>(message.wParam);
    } catch (const std::exception& exception) {
        const auto text = widen_error(exception.what());
        ::MessageBoxW(nullptr, text.c_str(), L"EngCoder", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(ole_result)) {
            ::OleUninitialize();
        }
        return 1;
    }
}
