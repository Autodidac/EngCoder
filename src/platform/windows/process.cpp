module;
#include "module_prelude.hpp"
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

module threadspire.platform;

namespace threadspire::ai::platform {
namespace {

class UniqueHandle final {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE value) noexcept : value_(value) {}
    ~UniqueHandle() {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(value_);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : value_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] HANDLE release() noexcept {
        const auto value = value_;
        value_ = nullptr;
        return value;
    }
    void reset(HANDLE value = nullptr) noexcept {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(value_);
        }
        value_ = value;
    }

private:
    HANDLE value_{nullptr};
};

[[nodiscard]] std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const auto size = ::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (size <= 0) {
        std::wstring fallback;
        fallback.reserve(value.size());
        for (const auto character : value) {
            fallback.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
        }
        return fallback;
    }

    std::wstring result(static_cast<std::size_t>(size), L'\0');
    static_cast<void>(::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size));
    return result;
}

[[nodiscard]] std::string wide_to_utf8(const std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const auto size = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        return "unable to convert Windows error text to UTF-8";
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    static_cast<void>(::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr));
    return result;
}

[[nodiscard]] std::wstring quote_argument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }

    std::wstring quoted;
    quoted.push_back(L'\"');
    std::size_t backslashes = 0;

    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }

        if (character == L'\"') {
            quoted.append((backslashes * 2) + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
            continue;
        }

        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }

    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

[[nodiscard]] std::wstring make_command_line(const ProcessSpec& spec) {
    std::wstring command = quote_argument(spec.executable.wstring());
    for (const auto& argument : spec.arguments) {
        command.push_back(L' ');
        command += quote_argument(utf8_to_wide(argument));
    }
    return command;
}

[[nodiscard]] std::string windows_error(const DWORD code) {
    wchar_t* buffer = nullptr;
    const auto size = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);

    if (size == 0 || buffer == nullptr) {
        return "Windows error " + std::to_string(code);
    }

    std::wstring message(buffer, size);
    ::LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }
    return wide_to_utf8(message);
}

[[nodiscard]] UniqueHandle open_log(
    const std::filesystem::path& path,
    const bool append,
    std::string& error) {
    std::error_code filesystem_error;
    if (!path.empty() && !path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), filesystem_error);
        if (filesystem_error) {
            error = "unable to create log directory: " + filesystem_error.message();
            return {};
        }
    }

    const auto target = path.empty() ? std::filesystem::path(L"NUL") : path;
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    UniqueHandle handle(::CreateFileW(
        target.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &attributes,
        append ? OPEN_ALWAYS : CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));

    if (handle.get() == INVALID_HANDLE_VALUE) {
        error = "unable to open log file: " + windows_error(::GetLastError());
        static_cast<void>(handle.release());
        return {};
    }
    if (append) {
        static_cast<void>(::SetFilePointer(handle.get(), 0, nullptr, FILE_END));
    }
    return handle;
}

[[nodiscard]] bool create_process(
    const ProcessSpec& spec,
    const DWORD creation_flags,
    PROCESS_INFORMATION& process_information,
    UniqueHandle& log_handle,
    std::string& error) {
    log_handle = open_log(spec.log_file, spec.append_log, error);
    if (log_handle.get() == nullptr) {
        return false;
    }

    SECURITY_ATTRIBUTES input_attributes{};
    input_attributes.nLength = sizeof(input_attributes);
    input_attributes.bInheritHandle = TRUE;
    UniqueHandle input_handle(::CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &input_attributes,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (input_handle.get() == INVALID_HANDLE_VALUE) {
        error = "unable to open NUL input: " + windows_error(::GetLastError());
        static_cast<void>(input_handle.release());
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_handle.get();
    startup.hStdOutput = log_handle.get();
    startup.hStdError = log_handle.get();

    auto command_line = make_command_line(spec);
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    const auto working_directory = spec.working_directory.empty()
        ? std::wstring{}
        : spec.working_directory.wstring();

    const auto success = ::CreateProcessW(
        nullptr,
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        creation_flags,
        nullptr,
        working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup,
        &process_information);

    if (success == FALSE) {
        error = "CreateProcessW failed: " + windows_error(::GetLastError());
        return false;
    }
    return true;
}

} // namespace

ProcessResult run_process(const ProcessSpec& spec, StopPredicate should_stop) {
    ProcessResult result;
    const auto start = std::chrono::steady_clock::now();

    PROCESS_INFORMATION process_information{};
    UniqueHandle log_handle;
    if (!create_process(
            spec,
            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED,
            process_information,
            log_handle,
            result.error)) {
        return result;
    }

    UniqueHandle process(process_information.hProcess);
    UniqueHandle thread(process_information.hThread);
    result.launched = true;

    UniqueHandle job(::CreateJobObjectW(nullptr, nullptr));
    bool assigned_to_job = false;
    if (job.get() != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (::SetInformationJobObject(
                job.get(),
                JobObjectExtendedLimitInformation,
                &limits,
                sizeof(limits)) != FALSE) {
            assigned_to_job = ::AssignProcessToJobObject(job.get(), process.get()) != FALSE;
        }
    }

    if (::ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        result.error = "ResumeThread failed: " + windows_error(::GetLastError());
        ::TerminateProcess(process.get(), ERROR_PROCESS_ABORTED);
        ::WaitForSingleObject(process.get(), 5000);
        return result;
    }

    while (true) {
        const auto wait_result = ::WaitForSingleObject(process.get(), 100);
        if (wait_result == WAIT_OBJECT_0) {
            break;
        }
        if (wait_result == WAIT_FAILED) {
            result.error = "WaitForSingleObject failed: " + windows_error(::GetLastError());
            break;
        }

        if (should_stop && should_stop()) {
            result.cancelled = true;
            if (assigned_to_job) {
                ::TerminateJobObject(job.get(), ERROR_CANCELLED);
            } else {
                ::TerminateProcess(process.get(), ERROR_CANCELLED);
            }
            ::WaitForSingleObject(process.get(), 5000);
            break;
        }
    }

    DWORD exit_code = 0;
    if (::GetExitCodeProcess(process.get(), &exit_code) != FALSE) {
        result.exit_code = static_cast<int>(exit_code);
    } else if (result.error.empty()) {
        result.error = "GetExitCodeProcess failed: " + windows_error(::GetLastError());
    }

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
}

bool launch_detached(const ProcessSpec& spec, std::string& error) {
    PROCESS_INFORMATION process_information{};
    UniqueHandle log_handle;
    if (!create_process(
            spec,
            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
            process_information,
            log_handle,
            error)) {
        return false;
    }

    [[maybe_unused]] UniqueHandle process(process_information.hProcess);
    [[maybe_unused]] UniqueHandle thread(process_information.hThread);
    return true;
}

} // namespace threadspire::ai::platform

namespace threadspire::ai::platform {

struct ManagedProcess::Impl final {
    UniqueHandle process;
    UniqueHandle job;
    int last_exit_code{-1};
};

ManagedProcess::ManagedProcess() : impl_(std::make_unique<Impl>()) {}
ManagedProcess::~ManagedProcess() { stop(); }
ManagedProcess::ManagedProcess(ManagedProcess&&) noexcept = default;
ManagedProcess& ManagedProcess::operator=(ManagedProcess&&) noexcept = default;

bool ManagedProcess::start(const ProcessSpec& spec, std::string& error) {
    stop();
    PROCESS_INFORMATION process_information{};
    UniqueHandle log_handle;
    if (!create_process(
            spec,
            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED,
            process_information,
            log_handle,
            error)) {
        return false;
    }

    impl_->process.reset(process_information.hProcess);
    UniqueHandle thread(process_information.hThread);
    impl_->job.reset(::CreateJobObjectW(nullptr, nullptr));
    if (impl_->job.get() != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (::SetInformationJobObject(
                impl_->job.get(),
                JobObjectExtendedLimitInformation,
                &limits,
                sizeof(limits)) == FALSE ||
            ::AssignProcessToJobObject(impl_->job.get(), impl_->process.get()) == FALSE) {
            impl_->job.reset();
        }
    }

    if (::ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        error = "ResumeThread failed: " + windows_error(::GetLastError());
        ::TerminateProcess(impl_->process.get(), ERROR_PROCESS_ABORTED);
        ::WaitForSingleObject(impl_->process.get(), 5000);
        impl_->process.reset();
        impl_->job.reset();
        return false;
    }
    impl_->last_exit_code = -1;
    return true;
}

void ManagedProcess::stop() noexcept {
    if (!impl_ || impl_->process.get() == nullptr) {
        return;
    }
    DWORD code = 0;
    if (::GetExitCodeProcess(impl_->process.get(), &code) != FALSE && code == STILL_ACTIVE) {
        if (impl_->job.get() != nullptr) {
            static_cast<void>(::TerminateJobObject(impl_->job.get(), ERROR_CANCELLED));
        } else {
            static_cast<void>(::TerminateProcess(impl_->process.get(), ERROR_CANCELLED));
        }
        static_cast<void>(::WaitForSingleObject(impl_->process.get(), 5000));
    }
    if (::GetExitCodeProcess(impl_->process.get(), &code) != FALSE) {
        impl_->last_exit_code = static_cast<int>(code);
    }
    impl_->process.reset();
    impl_->job.reset();
}

bool ManagedProcess::running() const noexcept {
    if (!impl_ || impl_->process.get() == nullptr) {
        return false;
    }
    DWORD code = 0;
    return ::GetExitCodeProcess(impl_->process.get(), &code) != FALSE && code == STILL_ACTIVE;
}

int ManagedProcess::exit_code() const noexcept {
    if (!impl_ || impl_->process.get() == nullptr) {
        return impl_ ? impl_->last_exit_code : -1;
    }
    DWORD code = 0;
    if (::GetExitCodeProcess(impl_->process.get(), &code) == FALSE || code == STILL_ACTIVE) {
        return -1;
    }
    return static_cast<int>(code);
}

} // namespace threadspire::ai::platform
