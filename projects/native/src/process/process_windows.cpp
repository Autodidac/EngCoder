#include "engcoder/process/process.hpp"
#ifdef _WIN32

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <cstring>
#include <map>
#include <thread>

namespace engcoder::process::detail {
namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count) <= 0) {
        return {};
    }
    return result;
}

std::wstring quote_windows_argument(std::wstring_view value) {
    if (value.empty()) {
        return L"\"\"";
    }
    if (value.find_first_of(L" \t\"") == std::wstring_view::npos) {
        return std::wstring(value);
    }

    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}


struct WideCaseInsensitiveLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const noexcept {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](wchar_t x, wchar_t y) {
            return std::towlower(x) < std::towlower(y);
        });
    }
};

std::vector<wchar_t> build_environment_block(
    const std::vector<std::pair<std::string, std::string>>& overrides) {
    if (overrides.empty()) return {};

    std::map<std::wstring, std::wstring, WideCaseInsensitiveLess> variables;
    LPWCH current = GetEnvironmentStringsW();
    if (current != nullptr) {
        for (const wchar_t* entry = current; *entry != L'\0'; entry += std::wcslen(entry) + 1) {
            const std::wstring_view line{entry};
            const auto equals = line.find(L'=', line.starts_with(L'=') ? 1 : 0);
            if (equals == std::wstring_view::npos) continue;
            variables[std::wstring(line.substr(0, equals))] = std::wstring(line.substr(equals + 1));
        }
        FreeEnvironmentStringsW(current);
    }

    for (const auto& [key_utf8, value_utf8] : overrides) {
        const auto key = widen(key_utf8);
        const auto value = widen(value_utf8);
        if (!key.empty()) variables[key] = value;
    }

    std::vector<wchar_t> block;
    for (const auto& [key, value] : variables) {
        block.insert(block.end(), key.begin(), key.end());
        block.push_back(L'=');
        block.insert(block.end(), value.begin(), value.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

void append_tail(std::string& output, const char* data, std::size_t size, std::size_t limit) {
    if (limit == 0 || size == 0) return;
    if (size >= limit) {
        output.assign(data + (size - limit), limit);
        return;
    }
    if (output.size() + size > limit) {
        output.erase(0, output.size() + size - limit);
    }
    output.append(data, size);
}

void read_pipe(HANDLE pipe, std::string& output, std::size_t limit) {
    char buffer[4096];
    DWORD bytes_read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0) {
        append_tail(output, buffer, static_cast<std::size_t>(bytes_read), limit);
    }
}

void close_if(HANDLE& handle) noexcept {
    if (handle && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

core::Error win32_error(std::string_view operation) {
    return {
        core::make_error_code(core::Errc::process_failed),
        std::string(operation) + " failed with Win32 error " + std::to_string(GetLastError())
    };
}

} // namespace

std::expected<Result, core::Error> run_windows(const Request& request, std::stop_token stop) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;

    if (!CreatePipe(&stdout_read, &stdout_write, &security, 0)) {
        return std::unexpected(win32_error("CreatePipe(stdout)"));
    }
    if (!CreatePipe(&stderr_read, &stderr_write, &security, 0)) {
        close_if(stdout_read);
        close_if(stdout_write);
        return std::unexpected(win32_error("CreatePipe(stderr)"));
    }
    if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0)) {
        close_if(stdout_read); close_if(stdout_write);
        close_if(stderr_read); close_if(stderr_write);
        return std::unexpected(win32_error("SetHandleInformation"));
    }

    const auto executable = widen(request.executable);
    if (executable.empty()) {
        close_if(stdout_read); close_if(stdout_write);
        close_if(stderr_read); close_if(stderr_write);
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::invalid_argument),
            "process executable is empty or invalid UTF-8"
        });
    }

    std::wstring command_line = quote_windows_argument(executable);
    for (const auto& argument : request.arguments) {
        const auto wide = widen(argument);
        if (wide.empty() && !argument.empty()) {
            close_if(stdout_read); close_if(stdout_write);
            close_if(stderr_read); close_if(stderr_write);
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::invalid_argument),
                "process argument contains invalid UTF-8"
            });
        }
        command_line.push_back(L' ');
        command_line += quote_windows_argument(wide);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stderr_write;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process{};
    const auto cwd = request.working_directory.empty()
        ? std::wstring{}
        : request.working_directory.wstring();
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    auto environment_block = build_environment_block(request.environment);
    DWORD creation_flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    if (!environment_block.empty()) creation_flags |= CREATE_UNICODE_ENVIRONMENT;
    const BOOL created = CreateProcessW(
        nullptr,
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        creation_flags,
        environment_block.empty() ? nullptr : environment_block.data(),
        cwd.empty() ? nullptr : cwd.c_str(),
        &startup,
        &process);

    close_if(stdout_write);
    close_if(stderr_write);
    if (!created) {
        const auto error = win32_error("CreateProcessW");
        close_if(stdout_read);
        close_if(stderr_read);
        return std::unexpected(error);
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        const auto error = win32_error("CreateJobObjectW");
        TerminateProcess(process.hProcess, 1);
        close_if(process.hThread); close_if(process.hProcess);
        close_if(stdout_read); close_if(stderr_read);
        return std::unexpected(error);
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job, process.hProcess)) {
        const auto error = win32_error("AssignProcessToJobObject");
        TerminateProcess(process.hProcess, 1);
        close_if(process.hThread); close_if(process.hProcess); close_if(job);
        close_if(stdout_read); close_if(stderr_read);
        return std::unexpected(error);
    }

    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        const auto error = win32_error("ResumeThread");
        TerminateJobObject(job, 1);
        close_if(process.hThread); close_if(process.hProcess); close_if(job);
        close_if(stdout_read); close_if(stderr_read);
        return std::unexpected(error);
    }

    Result result;
    std::jthread stdout_reader([&] { read_pipe(stdout_read, result.stdout_text, request.max_output_bytes); });
    std::jthread stderr_reader([&] { read_pipe(stderr_read, result.stderr_text, request.max_output_bytes); });
    const auto deadline = std::chrono::steady_clock::now() + request.timeout;

    for (;;) {
        const auto wait = WaitForSingleObject(process.hProcess, 50);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait == WAIT_FAILED) {
            TerminateJobObject(job, 1);
            break;
        }
        if (stop.stop_requested()) {
            result.cancelled = true;
            TerminateJobObject(job, 1);
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            TerminateJobObject(job, 1);
            break;
        }
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process.hProcess, &exit_code)) {
        result.exit_code = static_cast<int>(exit_code);
    }

    if (stdout_reader.joinable()) { stdout_reader.join(); }
    if (stderr_reader.joinable()) { stderr_reader.join(); }

    close_if(process.hThread);
    close_if(process.hProcess);
    close_if(stdout_read);
    close_if(stderr_read);
    close_if(job);
    return result;
}

} // namespace engcoder::process::detail
#endif
