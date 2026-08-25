module;
#include "module_prelude.hpp"
#include <Windows.h>
#include <CommCtrl.h>
#include <Richedit.h>
#include <Dwmapi.h>
#include <ShObjIdl.h>
#include <Shellapi.h>
#include <Uxtheme.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

module threadspire.ui;
import threadspire.core;
import threadspire.scheduler;

namespace threadspire::ai::app {
namespace {

constexpr wchar_t window_class_name[] = L"ThreadSpire.MainWindow";
constexpr UINT_PTR update_timer = 1;
constexpr UINT_PTR stream_timer = 2;
constexpr COLORREF window_color = RGB(22, 24, 29);
constexpr COLORREF edit_color = RGB(31, 34, 41);
constexpr COLORREF button_color = RGB(40, 44, 53);
constexpr COLORREF button_pressed_color = RGB(52, 58, 70);
constexpr COLORREF selection_color = RGB(48, 75, 116);
constexpr COLORREF border_color = RGB(70, 76, 89);
constexpr COLORREF accent_color = RGB(92, 155, 255);
constexpr COLORREF text_color = RGB(236, 239, 244);
constexpr COLORREF muted_text_color = RGB(171, 177, 190);
constexpr COLORREF disabled_text_color = RGB(112, 118, 130);
constexpr COLORREF transcript_user_color = RGB(124, 181, 255);
constexpr COLORREF transcript_internal_color = RGB(115, 214, 157);
constexpr COLORREF transcript_connected_color = RGB(120, 201, 255);
constexpr COLORREF transcript_error_color = RGB(255, 121, 135);
constexpr COLORREF transcript_output_color = RGB(125, 220, 160);
constexpr DWORD immersive_dark_mode_attribute = 20U;
constexpr std::array<std::size_t, 3> automatic_bootstrap_targets{24000U, 36000U, 50000U};

namespace id {
constexpr int tabs = 100;
constexpr int status = 101;

constexpr int page_chat = 200;
constexpr int page_coding = 201;
constexpr int page_models = 202;
constexpr int page_learning = 203;
constexpr int page_skills = 204;
constexpr int page_long_horizon = 205;
constexpr int page_logs = 206;

constexpr int chat_prompt_label = 300;
constexpr int chat_prompt = 301;
constexpr int chat_route = 302;
constexpr int chat_send = 303;
constexpr int chat_accept = 304;
constexpr int chat_reject = 305;
constexpr int chat_plan_label = 306;
constexpr int chat_plan = 307;
constexpr int chat_response_label = 308;
constexpr int chat_response = 309;
constexpr int chat_backend = 310;
constexpr int chat_mode_label = 311;
constexpr int chat_mode = 312;
constexpr int chat_model_label = 313;
constexpr int chat_model = 314;
constexpr int chat_discover = 315;
constexpr int chat_open_models = 316;
constexpr int chat_repository_label = 317;
constexpr int chat_repository = 318;
constexpr int chat_repository_browse = 319;
constexpr int chat_stop = 320;
constexpr int chat_clear = 321;
constexpr int chat_auto_learning = 322;
constexpr int chat_learning_status = 323;
constexpr int chat_mcp_status = 324;
constexpr int chat_hint = 325;
constexpr int chat_source_label = 326;
constexpr int chat_source = 327;
constexpr int chat_open_output = 328;
constexpr int chat_training_data_size = 329;
constexpr int chat_active_model_size = 330;
constexpr int chat_installed_packs_size = 331;
constexpr int chat_outputs_cache_size = 332;
constexpr int chat_total_usage_size = 333;
constexpr int chat_free_disk_size = 334;

constexpr int coding_repository_label = 350;
constexpr int coding_repository = 351;
constexpr int coding_browse = 352;
constexpr int coding_task_label = 353;
constexpr int coding_task = 354;
constexpr int coding_ask = 355;
constexpr int coding_run = 356;
constexpr int coding_accept = 357;
constexpr int coding_reject = 358;
constexpr int coding_plan_label = 359;
constexpr int coding_plan = 360;
constexpr int coding_response_label = 361;
constexpr int coding_response = 362;
constexpr int coding_backend = 363;
constexpr int coding_model_label = 364;
constexpr int coding_model = 365;
constexpr int coding_discover = 366;
constexpr int coding_open_models = 367;
constexpr int coding_source_label = 368;
constexpr int coding_source = 369;

constexpr int models_discover = 400;
constexpr int models_discovery_status = 401;
constexpr int models_manual_endpoint_label = 402;
constexpr int models_manual_endpoint = 403;
constexpr int models_manual_model_label = 404;
constexpr int models_manual_model = 405;
constexpr int models_manual_protocol = 406;
constexpr int models_manual_coding = 407;
constexpr int models_manual_teacher = 408;
constexpr int models_manual_add = 409;
constexpr int models_manual_capabilities_label = 438;
constexpr int models_manual_capabilities = 439;
constexpr int models_mcp_label = 410;
constexpr int models_mcp_endpoint = 411;
constexpr int models_mcp_connect = 412;
constexpr int models_mcp_status = 413;
constexpr int models_python_label = 414;
constexpr int models_python = 415;
constexpr int models_browse = 416;
constexpr int models_install = 417;
constexpr int models_verify = 418;
constexpr int models_start = 419;
constexpr int models_stop = 420;
constexpr int models_status = 421;
constexpr int models_editor = 422;
constexpr int models_reload = 423;
constexpr int models_save = 424;
constexpr int models_local_header = 425;
constexpr int models_mcp_header = 426;
constexpr int models_backend_header = 427;
constexpr int models_registry_header = 428;
constexpr int models_internal_profile_label = 429;
constexpr int models_internal_profile = 430;
constexpr int models_internal_create = 431;
constexpr int models_internal_bootstrap = 432;
constexpr int models_internal_status = 433;
constexpr int models_automation_toggle = 434;
constexpr int models_progress = 435;
constexpr int models_progress_detail = 436;
constexpr int models_advanced_header = 437;

constexpr int skills_refresh = 500;
constexpr int skills_skill = 501;
constexpr int skills_queue = 502;
constexpr int skills_report = 503;
constexpr int skills_rollback = 504;
constexpr int skills_bootstrap = 505;
constexpr int skills_skill_label = 506;

constexpr int learning_pause = 600;
constexpr int learning_policy = 601;
constexpr int learning_jobs = 602;
constexpr int learning_piggyback = 603;
constexpr int learning_piggyback_count = 604;
constexpr int learning_piggyback_skill = 605;
constexpr int learning_piggyback_status = 606;
constexpr int learning_auto = 607;
constexpr int learning_build_custom = 608;
constexpr int learning_count_label = 609;
constexpr int learning_skill_label = 610;
constexpr int learning_header = 611;
constexpr int learning_custom_skill_label = 612;
constexpr int learning_custom_skill = 613;
constexpr int learning_dataset_label = 614;
constexpr int learning_dataset_path = 615;
constexpr int learning_dataset_browse = 616;
constexpr int learning_dataset_import = 617;
constexpr int learning_dataset_template = 618;
constexpr int learning_stop = 619;
constexpr int learning_activity_header = 620;
constexpr int learning_library_header = 621;
constexpr int learning_library_filter = 622;
constexpr int learning_library_source = 623;
constexpr int learning_library_refresh = 624;
constexpr int learning_library = 625;

constexpr int horizon_repository_label = 700;
constexpr int horizon_repository = 701;
constexpr int horizon_browse = 702;
constexpr int horizon_task_label = 703;
constexpr int horizon_task = 704;
constexpr int horizon_build_label = 705;
constexpr int horizon_build = 706;
constexpr int horizon_test_label = 707;
constexpr int horizon_test = 708;
constexpr int horizon_iterations_label = 709;
constexpr int horizon_iterations = 710;
constexpr int horizon_clean = 711;
constexpr int horizon_start = 712;
constexpr int horizon_cancel = 713;
constexpr int horizon_report = 714;

constexpr int logs_open = 800;
constexpr int logs_open_root = 801;
constexpr int logs_view = 802;
constexpr int logs_refresh_storage = 803;
constexpr int logs_storage = 804;
constexpr int logs_storage_header = 805;
} // namespace id


struct TrainingTargetOption final {
    const wchar_t* label;
    std::string_view skill;
};

constexpr std::array training_targets{
    TrainingTargetOption{L"English foundation — recommended first", "english-foundation"},
    TrainingTargetOption{L"English conversation", "english-conversation"},
    TrainingTargetOption{L"English reasoning + instruction following", "english-reasoning"},
    TrainingTargetOption{L"C++23 core", "cpp23-core"},
    TrainingTargetOption{L"C++23 debugging", "cpp23-debugging"},
    TrainingTargetOption{L"C++23 generation", "cpp23-generation"},
    TrainingTargetOption{L"Custom topic name", "custom"},
};

constexpr int custom_training_target_index =
    static_cast<int>(training_targets.size() - 1U);

[[nodiscard]] std::string normalize_training_skill(const std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    auto separator_pending = false;
    for (const auto raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        if (std::isalnum(character) != 0) {
            if (separator_pending && !normalized.empty()) {
                normalized.push_back('-');
            }
            normalized.push_back(static_cast<char>(std::tolower(character)));
            separator_pending = false;
        } else {
            separator_pending = !normalized.empty();
        }
    }
    while (!normalized.empty() && normalized.back() == '-') {
        normalized.pop_back();
    }
    return normalized.empty() ? std::string{"english-foundation"} : normalized;
}

[[nodiscard]] int training_target_index(const std::string_view skill) noexcept {
    for (std::size_t index = 0; index + 1U < training_targets.size(); ++index) {
        if (training_targets[index].skill == skill) {
            return static_cast<int>(index);
        }
    }
    return custom_training_target_index;
}


[[nodiscard]] int scaled(const int value, const unsigned int dpi) noexcept {
    return ::MulDiv(value, static_cast<int>(dpi), 96);
}

[[nodiscard]] std::wstring widen(const std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const auto count = ::MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (count <= 0) {
        return L"<UTF-8 conversion failed>";
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    static_cast<void>(::MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        count));
    return result;
}

[[nodiscard]] std::string narrow(const std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const auto count = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (count <= 0) {
        throw std::runtime_error("unable to convert text to UTF-8");
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    static_cast<void>(::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        count,
        nullptr,
        nullptr));
    return result;
}

[[nodiscard]] std::wstring normalize_edit_newlines(const std::wstring_view value) {
    std::wstring result;
    result.reserve(value.size() + value.size() / 16U);
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto character = value[index];
        if (character == L'\r') {
            result.push_back(L'\r');
            if (index + 1U < value.size() && value[index + 1U] == L'\n') {
                result.push_back(L'\n');
                ++index;
            } else {
                result.push_back(L'\n');
            }
        } else if (character == L'\n') {
            result.push_back(L'\r');
            result.push_back(L'\n');
        } else {
            result.push_back(character);
        }
    }
    return result;
}

[[nodiscard]] std::string utf8_prefix(
    const std::string_view value,
    const std::size_t maximum_bytes) {
    if (value.size() <= maximum_bytes) {
        return std::string(value);
    }

    auto end = maximum_bytes;
    while (end > 0U &&
           (static_cast<unsigned char>(value[end]) & 0xC0U) == 0x80U) {
        --end;
    }
    return std::string(value.substr(0U, end));
}

[[nodiscard]] std::string recent_conversation_context(
    const std::vector<std::pair<std::string, std::string>>& turns) {
    constexpr std::size_t maximum_turns = 2U;
    constexpr std::size_t maximum_context_bytes = 640U;
    constexpr std::size_t maximum_user_bytes = 160U;
    constexpr std::size_t maximum_assistant_bytes = 320U;

    std::vector<std::string> selected;
    selected.reserve(maximum_turns);
    std::size_t used = 0U;
    for (auto iterator = turns.rbegin();
         iterator != turns.rend() && selected.size() < maximum_turns;
         ++iterator) {
        auto section =
            std::string{"Earlier user:\n"} +
            utf8_prefix(iterator->first, maximum_user_bytes) +
            "\nEarlier assistant:\n" +
            utf8_prefix(iterator->second, maximum_assistant_bytes) +
            "\n";
        if (used + section.size() > maximum_context_bytes) {
            continue;
        }
        used += section.size();
        selected.push_back(std::move(section));
    }
    if (selected.empty()) {
        return {};
    }

    std::string result =
        "Recent conversation context, oldest to newest. Continue consistently; "
        "do not quote this block unless asked.\n\n";
    for (auto iterator = selected.rbegin(); iterator != selected.rend(); ++iterator) {
        result += *iterator;
        result += '\n';
    }
    return result;
}

[[nodiscard]] bool is_rich_edit_control(const HWND handle) noexcept {
    if (handle == nullptr) {
        return false;
    }
    std::array<wchar_t, 32> class_name{};
    const auto length = ::GetClassNameW(
        handle,
        class_name.data(),
        static_cast<int>(class_name.size()));
    return length > 0 && std::wstring_view(
        class_name.data(), static_cast<std::size_t>(length)).starts_with(L"RICHEDIT");
}

[[nodiscard]] bool is_edit_control(const HWND handle) noexcept {
    if (handle == nullptr) {
        return false;
    }
    std::array<wchar_t, 32> class_name{};
    const auto length = ::GetClassNameW(
        handle,
        class_name.data(),
        static_cast<int>(class_name.size()));
    return length > 0 && (
        ::lstrcmpiW(class_name.data(), L"EDIT") == 0 ||
        std::wstring_view(
            class_name.data(), static_cast<std::size_t>(length)).starts_with(L"RICHEDIT"));
}

[[nodiscard]] const wchar_t* transcript_control_class() noexcept {
    static const HMODULE rich_edit_module = ::LoadLibraryW(L"Msftedit.dll");
    return rich_edit_module != nullptr ? MSFTEDIT_CLASS : L"EDIT";
}

void format_rich_range(
    const HWND handle,
    const LONG start,
    const LONG end,
    const COLORREF color,
    const bool bold) noexcept {
    CHARRANGE range{.cpMin = start, .cpMax = end};
    static_cast<void>(::SendMessageW(
        handle, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range)));
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR | CFM_BOLD;
    format.dwEffects = bold ? CFE_BOLD : 0U;
    format.crTextColor = color;
    static_cast<void>(::SendMessageW(
        handle, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format)));
}

void style_chat_transcript(
    const HWND handle,
    const std::wstring_view text) noexcept {
    if (!is_rich_edit_control(handle)) {
        return;
    }

    static_cast<void>(::SendMessageW(handle, EM_SETBKGNDCOLOR, 0, edit_color));
    format_rich_range(handle, 0, -1, text_color, false);

    std::size_t line_start = 0;
    LONG rich_position = 0;
    while (line_start <= text.size()) {
        const auto newline = text.find(L'\n', line_start);
        auto line_end = newline == std::wstring_view::npos ? text.size() : newline;
        if (line_end > line_start && text[line_end - 1U] == L'\r') {
            --line_end;
        }
        const auto line = text.substr(line_start, line_end - line_start);
        COLORREF color = text_color;
        bool styled = true;
        if (line == L"YOU" || line.starts_with(L"YOU —")) {
            color = transcript_user_color;
        } else if (line.starts_with(L"ASSISTANT — THREADSPIRE")) {
            color = transcript_internal_color;
        } else if (line.starts_with(L"ASSISTANT — CONNECTED") ||
                   line.starts_with(L"ASSISTANT — AUTOMATIC")) {
            color = transcript_connected_color;
        } else if (line.starts_with(L"ERROR")) {
            color = transcript_error_color;
        } else if (line.starts_with(L"OUTPUT")) {
            color = transcript_output_color;
        } else if (!line.empty() && std::ranges::all_of(
                       line, [](const wchar_t character) { return character == L'-'; })) {
            color = muted_text_color;
        } else {
            styled = false;
        }
        if (styled && line_end > line_start) {
            format_rich_range(
                handle,
                rich_position,
                rich_position + static_cast<LONG>(line.size()),
                color,
                color != muted_text_color);
        }
        if (newline == std::wstring_view::npos) {
            break;
        }
        rich_position += static_cast<LONG>(line.size()) + 1;
        line_start = newline + 1U;
    }
}

struct EditViewport final {
    DWORD selection_start{0};
    DWORD selection_end{0};
    int first_visible_line{0};
    bool follow_tail{false};
};

[[nodiscard]] std::wstring window_text(const HWND handle) {
    if (handle == nullptr) {
        return {};
    }
    const auto length = ::GetWindowTextLengthW(handle);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length) + 1U, L'\0');
    const auto copied = ::GetWindowTextW(handle, result.data(), length + 1);
    result.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0U);
    return result;
}

[[nodiscard]] int edit_visible_line_capacity(const HWND handle) noexcept {
    RECT formatting{};
    static_cast<void>(::SendMessageW(
        handle,
        EM_GETRECT,
        0,
        reinterpret_cast<LPARAM>(&formatting)));

    auto line_height = 16;
    if (const auto device_context = ::GetDC(handle); device_context != nullptr) {
        const auto font = reinterpret_cast<HFONT>(
            ::SendMessageW(handle, WM_GETFONT, 0, 0));
        const auto previous = font != nullptr
            ? ::SelectObject(device_context, font)
            : nullptr;
        TEXTMETRICW metrics{};
        if (::GetTextMetricsW(device_context, &metrics) != FALSE) {
            const auto measured_line_height = static_cast<int>(
                metrics.tmHeight + metrics.tmExternalLeading);
            line_height = std::max(1, measured_line_height);
        }
        if (previous != nullptr) {
            static_cast<void>(::SelectObject(device_context, previous));
        }
        static_cast<void>(::ReleaseDC(handle, device_context));
    }

    const auto formatting_height = static_cast<int>(
        formatting.bottom - formatting.top);
    return std::max(1, formatting_height / line_height);
}

[[nodiscard]] EditViewport capture_edit_viewport(const HWND handle) noexcept {
    EditViewport viewport;
    static_cast<void>(::SendMessageW(
        handle,
        EM_GETSEL,
        reinterpret_cast<WPARAM>(&viewport.selection_start),
        reinterpret_cast<LPARAM>(&viewport.selection_end)));
    viewport.first_visible_line = static_cast<int>(
        ::SendMessageW(handle, EM_GETFIRSTVISIBLELINE, 0, 0));
    const auto line_count = std::max(
        1,
        static_cast<int>(::SendMessageW(handle, EM_GETLINECOUNT, 0, 0)));
    const auto visible_lines = edit_visible_line_capacity(handle);
    viewport.follow_tail =
        viewport.selection_start == viewport.selection_end &&
        viewport.first_visible_line + visible_lines >= line_count - 1;
    return viewport;
}

void restore_edit_viewport(
    const HWND handle,
    const EditViewport& viewport) noexcept {
    const auto text_length = std::max(0, ::GetWindowTextLengthW(handle));
    if (viewport.follow_tail) {
        static_cast<void>(::SendMessageW(
            handle,
            EM_SETSEL,
            static_cast<WPARAM>(text_length),
            static_cast<LPARAM>(text_length)));
        static_cast<void>(::SendMessageW(handle, EM_SCROLLCARET, 0, 0));
        return;
    }

    const auto selection_start = std::min<DWORD>(
        viewport.selection_start,
        static_cast<DWORD>(text_length));
    const auto selection_end = std::min<DWORD>(
        viewport.selection_end,
        static_cast<DWORD>(text_length));
    static_cast<void>(::SendMessageW(
        handle,
        EM_SETSEL,
        static_cast<WPARAM>(selection_start),
        static_cast<LPARAM>(selection_end)));
    const auto current_first = static_cast<int>(
        ::SendMessageW(handle, EM_GETFIRSTVISIBLELINE, 0, 0));
    static_cast<void>(::SendMessageW(
        handle,
        EM_LINESCROLL,
        0,
        static_cast<LPARAM>(viewport.first_visible_line - current_first)));
}

void redraw_control(const HWND handle) noexcept {
    static_cast<void>(::RedrawWindow(
        handle,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW));
}

void fill_solid_rect(
    const HDC device_context,
    const RECT& rectangle,
    const COLORREF color) noexcept {
    const auto brush = ::CreateSolidBrush(color);
    if (brush != nullptr) {
        static_cast<void>(::FillRect(device_context, &rectangle, brush));
        static_cast<void>(::DeleteObject(brush));
    }
}

void frame_solid_rect(
    const HDC device_context,
    const RECT& rectangle,
    const COLORREF color) noexcept {
    const auto brush = ::CreateSolidBrush(color);
    if (brush != nullptr) {
        static_cast<void>(::FrameRect(device_context, &rectangle, brush));
        static_cast<void>(::DeleteObject(brush));
    }
}

void draw_dark_text(
    const HDC device_context,
    RECT rectangle,
    const std::wstring_view text,
    const COLORREF color,
    const UINT format) noexcept {
    static_cast<void>(::SetBkMode(device_context, TRANSPARENT));
    static_cast<void>(::SetTextColor(device_context, color));
    std::wstring owned(text);
    static_cast<void>(::DrawTextW(
        device_context,
        owned.data(),
        static_cast<int>(owned.size()),
        &rectangle,
        format));
}

[[nodiscard]] std::wstring combo_item_text(
    const HWND combo,
    const UINT item_id) {
    if (item_id == static_cast<UINT>(-1)) {
        return window_text(combo);
    }
    const auto length = static_cast<int>(::SendMessageW(
        combo,
        CB_GETLBTEXTLEN,
        static_cast<WPARAM>(item_id),
        0));
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(length) + 1U, L'\0');
    const auto copied = static_cast<int>(::SendMessageW(
        combo,
        CB_GETLBTEXT,
        static_cast<WPARAM>(item_id),
        reinterpret_cast<LPARAM>(text.data())));
    text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0U);
    return text;
}

[[nodiscard]] bool draw_dark_control_item(
    const DRAWITEMSTRUCT& item) {
    const auto disabled = (item.itemState & ODS_DISABLED) != 0U;
    const auto selected = (item.itemState & ODS_SELECTED) != 0U;
    const auto focused = (item.itemState & ODS_FOCUS) != 0U;
    const auto foreground = disabled ? disabled_text_color : text_color;

    if (item.CtlType == ODT_STATIC &&
        (item.CtlID == id::chat_training_data_size ||
         item.CtlID == id::chat_active_model_size ||
         item.CtlID == id::chat_installed_packs_size ||
         item.CtlID == id::chat_outputs_cache_size ||
         item.CtlID == id::chat_total_usage_size ||
         item.CtlID == id::chat_free_disk_size)) {
        auto rectangle = item.rcItem;
        fill_solid_rect(item.hDC, rectangle, edit_color);
        frame_solid_rect(item.hDC, rectangle, border_color);
        ::InflateRect(&rectangle, -10, -5);

        const auto text = window_text(item.hwndItem);
        const auto separator = text.find_first_of(L"\r\n");
        const auto title = text.substr(0, separator);
        auto value = separator == std::wstring::npos
            ? std::wstring{}
            : text.substr(separator + 1U);
        while (!value.empty() && (value.front() == L'\r' || value.front() == L'\n')) {
            value.erase(value.begin());
        }

        const auto middle = rectangle.top + (rectangle.bottom - rectangle.top) / 2;
        auto title_rectangle = rectangle;
        title_rectangle.bottom = middle;
        auto value_rectangle = rectangle;
        value_rectangle.top = middle;
        draw_dark_text(
            item.hDC,
            title_rectangle,
            title,
            muted_text_color,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        draw_dark_text(
            item.hDC,
            value_rectangle,
            value,
            foreground,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        return true;
    }

    if (item.CtlType == ODT_BUTTON) {
        auto rectangle = item.rcItem;
        fill_solid_rect(
            item.hDC,
            rectangle,
            selected ? button_pressed_color : button_color);
        frame_solid_rect(item.hDC, rectangle, focused ? accent_color : border_color);
        ::InflateRect(&rectangle, -8, -2);
        draw_dark_text(
            item.hDC,
            rectangle,
            window_text(item.hwndItem),
            foreground,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        return true;
    }

    if (item.CtlType == ODT_COMBOBOX) {
        auto rectangle = item.rcItem;
        fill_solid_rect(
            item.hDC,
            rectangle,
            selected ? selection_color : edit_color);
        if (item.itemAction != ODA_FOCUS) {
            frame_solid_rect(item.hDC, rectangle, border_color);
        }
        rectangle.left += 8;
        rectangle.right -= 6;
        draw_dark_text(
            item.hDC,
            rectangle,
            combo_item_text(item.hwndItem, item.itemID),
            foreground,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        return true;
    }

    if (item.CtlType == ODT_TAB) {
        auto rectangle = item.rcItem;
        const auto current = TabCtrl_GetCurSel(item.hwndItem);
        const auto active = current >= 0 &&
            static_cast<UINT>(current) == item.itemID;
        fill_solid_rect(
            item.hDC,
            rectangle,
            active ? edit_color : window_color);
        frame_solid_rect(item.hDC, rectangle, active ? accent_color : border_color);

        std::array<wchar_t, 128> label{};
        TCITEMW tab{};
        tab.mask = TCIF_TEXT;
        tab.pszText = label.data();
        tab.cchTextMax = static_cast<int>(label.size());
        static_cast<void>(TabCtrl_GetItem(item.hwndItem, static_cast<int>(item.itemID), &tab));
        draw_dark_text(
            item.hDC,
            rectangle,
            label.data(),
            active ? text_color : muted_text_color,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        return true;
    }

    return false;
}

[[nodiscard]] std::string trim_line_endings(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }
    return value;
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}


[[nodiscard]] bool internal_model_ready(const AppPaths& paths) {
    return runtime::inspect_internal_model_state(paths).bootstrap_complete;
}

[[nodiscard]] bool internal_host_startable(const AppPaths& paths) {
    return internal_model_ready(paths) && runtime::backend_files_present(paths);
}

[[nodiscard]] bool training_pack_payload_ready(
    const std::filesystem::path& source) noexcept {
    try {
        if (source.empty() || !std::filesystem::is_directory(source)) {
            return false;
        }
        const auto catalog = runtime::CapabilityPackManager::catalog();
        const auto descriptor = std::ranges::find(
            catalog,
            std::string_view{"internal-pytorch-training"},
            &runtime::CapabilityPackDescriptor::id);
        if (descriptor == catalog.end()) {
            return false;
        }
        std::error_code error;
        for (const auto& relative : descriptor->required_files) {
            const auto file = (source / relative).lexically_normal();
            const auto status = std::filesystem::symlink_status(file, error);
            if (error || std::filesystem::is_symlink(status) ||
                !std::filesystem::is_regular_file(status)) {
                return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::optional<std::filesystem::path> automatic_training_pack_source(
    const AppPaths& paths,
    const std::filesystem::path& preferred) {
    const auto executable_directory = paths.executable.parent_path();
    const std::array candidates{
        preferred,
        paths.root / "pack-inbox" / "internal-pytorch-training",
        paths.root / "downloads" / "internal-pytorch-training",
        executable_directory / "optional-packs" / "internal-pytorch-training",
        executable_directory / "packs" / "internal-pytorch-training",
    };
    const auto installed = (paths.packs / "internal-pytorch-training").lexically_normal();
    for (const auto& candidate : candidates) {
        const auto normalized = candidate.lexically_normal();
        if (!normalized.empty() && normalized != installed &&
            training_pack_payload_ready(normalized)) {
            return normalized;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool terminal_learning_phase(const std::string_view phase) noexcept {
    return phase == "repair-required" || phase == "rejected" || phase == "failed";
}

[[nodiscard]] std::size_t saved_learning_steps(
    const runtime::InternalModelProgress& progress) noexcept {
    return progress.total_steps > 0U
        ? progress.total_steps
        : progress.prior_total_steps + progress.step;
}

[[nodiscard]] std::size_t adaptive_bootstrap_target(
    const AppPaths& paths,
    const runtime::InternalModelState& state) noexcept {
    const auto configured = runtime::default_bootstrap_target(state);
    const auto progress = runtime::read_internal_model_progress(paths);
    if (!state.bootstrap_policy_current &&
        (!progress.available || !progress.bootstrap_policy_current)) {
        return configured;
    }
    if (!progress.available || !terminal_learning_phase(progress.phase)) {
        return std::max(configured, progress.bootstrap_target_steps);
    }
    const auto completed = saved_learning_steps(progress);
    for (const auto target : automatic_bootstrap_targets) {
        if (completed < target) {
            return std::max(configured, target);
        }
    }
    return automatic_bootstrap_targets.back();
}

[[nodiscard]] bool automatic_learning_limit_reached(
    const AppPaths& paths,
    const runtime::InternalModelState& state) noexcept {
    if (state.bootstrap_complete) {
        return false;
    }
    const auto progress = runtime::read_internal_model_progress(paths);
    if (!state.bootstrap_policy_current &&
        (!progress.available || !progress.bootstrap_policy_current)) {
        return false;
    }
    return progress.available && terminal_learning_phase(progress.phase) &&
        saved_learning_steps(progress) >= automatic_bootstrap_targets.back();
}
[[nodiscard]] std::wstring internal_model_status_text(const AppPaths& paths) {
    const auto state = runtime::inspect_internal_model_state(paths);
    if (!state.exists) {
        return L"Built-in AI is not installed.";
    }
    if (!state.manifest_valid) {
        return L"Built-in AI setup was interrupted. ThreadSpire will continue from the last safe point.";
    }
    if (state.bootstrap_complete) {
        return L"Built-in AI is ready and uses only local model files.";
    }

    const auto progress = runtime::read_internal_model_progress(paths);
    const auto progress_active = progress.available &&
        progress.phase != "complete" && progress.phase != "candidate-ready" &&
        progress.phase != "repair-required" && progress.phase != "rejected" &&
        progress.phase != "failed";
    const auto progress_has_saved_work =
        progress.available && saved_learning_steps(progress) > 0U;
    const auto target = progress_has_saved_work && progress.bootstrap_target_steps > 0U
        ? progress.bootstrap_target_steps
        : runtime::default_bootstrap_target(state);
    const auto completed = progress_has_saved_work
        ? saved_learning_steps(progress)
        : state.steps;
    std::wostringstream stream;
    if (progress.available && terminal_learning_phase(progress.phase)) {
        stream << L"Built-in AI failed quality checks at "
               << completed << L"/" << target;
    } else {
        stream << L"Built-in AI learning setup: ";
        if (progress_active && completed == 0U) {
            stream << L"starting";
        } else {
            stream << completed << L"/" << target;
        }
    }
    stream << L" • saved automatically • not used for answers until every check passes";
    return stream.str();
}
[[nodiscard]] std::wstring format_duration(const double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return L"unknown";
    }
    const auto whole = static_cast<std::uint64_t>(seconds);
    const auto hours = whole / 3600U;
    const auto minutes = (whole % 3600U) / 60U;
    const auto remaining = whole % 60U;
    std::wostringstream stream;
    if (hours > 0U) {
        stream << hours << L":" << std::setfill(L'0') << std::setw(2) << minutes
               << L":" << std::setw(2) << remaining;
    } else {
        stream << minutes << L":" << std::setfill(L'0') << std::setw(2) << remaining;
    }
    return stream.str();
}

[[nodiscard]] std::wstring format_bytes_compact(const std::uintmax_t bytes) {
    constexpr std::array units{L"B", L"KiB", L"MiB", L"GiB", L"TiB"};
    auto value = static_cast<double>(bytes);
    std::size_t unit = 0U;
    while (value >= 1024.0 && unit + 1U < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(unit == 0U ? 0 : 2)
           << value << L' ' << units[unit];
    return stream.str();
}

[[nodiscard]] std::wstring format_bytes(const std::uintmax_t bytes) {
    constexpr std::array units{L"B", L"KiB", L"MiB", L"GiB", L"TiB"};
    auto value = static_cast<double>(bytes);
    std::size_t unit = 0U;
    while (value >= 1024.0 && unit + 1U < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(unit == 0U ? 0 : 2)
           << value << L' ' << units[unit]
           << L" (" << bytes << L" bytes)";
    return stream.str();
}

struct LearningProgress final {
    bool available{false};
    std::string phase;
    std::string message;
    std::size_t step{0};
    std::size_t maximum_steps{0};
    std::size_t tokens_seen{0};
    double percent{0.0};
    std::optional<double> loss;
    double elapsed_seconds{0.0};
    std::optional<double> eta_seconds;
};

[[nodiscard]] LearningProgress read_learning_progress(
    const std::filesystem::path& path) {
    LearningProgress progress;
    if (!std::filesystem::is_regular_file(path)) {
        return progress;
    }

    const auto text = read_text(path);
    std::string parse_error;
    const auto document = core::json::try_parse(text, parse_error);
    if (!document.has_value() || !document->is_object()) {
        return progress;
    }

    const auto unsigned_number = [&document](
                                     const std::string_view key) -> std::size_t {
        return static_cast<std::size_t>(std::max(
            0.0,
            core::json::number_or(document->find(key))));
    };
    progress.available = true;
    progress.phase = core::json::string_or(document->find("phase"));
    progress.message = core::json::string_or(document->find("message"));
    progress.step = unsigned_number("step");
    progress.maximum_steps = unsigned_number("maximum_steps");
    progress.tokens_seen = unsigned_number("tokens_seen");
    progress.percent = std::clamp(
        core::json::number_or(document->find("percent")),
        0.0,
        100.0);
    if (const auto* loss = document->find("loss");
        loss != nullptr && loss->is_number()) {
        progress.loss = loss->as_number();
    }
    progress.elapsed_seconds = std::max(
        0.0,
        core::json::number_or(document->find("elapsed_seconds")));
    if (const auto* eta = document->find("eta_seconds");
        eta != nullptr && eta->is_number()) {
        const auto value = eta->as_number();
        if (std::isfinite(value) && value >= 0.0) {
            progress.eta_seconds = value;
        }
    }
    return progress;
}


void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("unable to write " + path.string());
        }
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    std::error_code error;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        throw std::runtime_error("unable to publish " + path.string() + ": " + error.message());
    }
}

[[nodiscard]] std::string read_tail(
    const std::filesystem::path& path,
    const std::size_t maximum = 256U * 1024U) {
    if (!std::filesystem::is_regular_file(path)) {
        return {};
    }
    std::ifstream stream(path, std::ios::binary);
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end <= 0) {
        return {};
    }
    const auto size = static_cast<std::size_t>(end);
    const auto offset = size > maximum ? size - maximum : 0U;
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    std::string text(size - offset, '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(stream.gcount()));
    return text;
}

void move_control(
    const HWND control,
    const int x,
    const int y,
    const int width,
    const int height) {
    if (control != nullptr) {
        static_cast<void>(::SetWindowPos(
            control,
            nullptr,
            x,
            y,
            std::max(1, width),
            std::max(1, height),
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOCOPYBITS));
    }
}

LRESULT CALLBACK prompt_edit_subclass(
    const HWND edit,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam,
    const UINT_PTR subclass_id,
    const DWORD_PTR reference_data) {
    if (message == WM_KEYDOWN &&
        wparam == static_cast<WPARAM>(VK_RETURN) &&
        (::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        const auto root = ::GetAncestor(edit, GA_ROOT);
        if (root != nullptr) {
            static_cast<void>(::PostMessageW(
                root,
                WM_COMMAND,
                MAKEWPARAM(static_cast<WORD>(reference_data), BN_CLICKED),
                reinterpret_cast<LPARAM>(edit)));
        }
        return 0;
    }
    if (message == WM_NCDESTROY) {
        static_cast<void>(::RemoveWindowSubclass(
            edit,
            prompt_edit_subclass,
            subclass_id));
    }
    return ::DefSubclassProc(edit, message, wparam, lparam);
}

LRESULT CALLBACK page_host_subclass(
    const HWND page,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam,
    const UINT_PTR subclass_id,
    const DWORD_PTR reference_data) {
    const auto root = reinterpret_cast<HWND>(reference_data);
    switch (message) {
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_DELETEITEM:
    case WM_COMPAREITEM:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
        if (root != nullptr) {
            return ::SendMessageW(root, message, wparam, lparam);
        }
        break;
    case WM_ERASEBKGND: {
        RECT client{};
        static_cast<void>(::GetClientRect(page, &client));
        fill_solid_rect(reinterpret_cast<HDC>(wparam), client, window_color);
        return 1;
    }
    case WM_NCDESTROY:
        static_cast<void>(::RemoveWindowSubclass(
            page,
            page_host_subclass,
            subclass_id));
        break;
    default:
        break;
    }
    return ::DefSubclassProc(page, message, wparam, lparam);
}

[[nodiscard]] bool has_available_teacher(const models::ModelRegistry& registry) {
    return std::ranges::any_of(
        registry.models(),
        [](const models::ModelProfile& model) {
            return model.enabled &&
                   !models::is_internal_model(model) &&
                   model.teacher &&
                   models::has_capability(model, "chat");
        });
}

[[nodiscard]] std::string skill_for(const routing::TaskAnalysis& analysis) {
    switch (analysis.kind) {
    case routing::TaskKind::code_debugging:
        return "cpp23-debugging";
    case routing::TaskKind::code_generation:
        return "cpp23-generation";
    case routing::TaskKind::repository_task:
        return "cpp23-repository";
    case routing::TaskKind::planning:
        return "planning";
    case routing::TaskKind::translation:
        return "translation";
    case routing::TaskKind::transformation:
        return "english-transformation";
    case routing::TaskKind::tool_action:
        return "tool-use";
    case routing::TaskKind::explanation:
        return "explanation";
    case routing::TaskKind::conversation:
        return "english-conversation";
    }
    return "general";
}


[[nodiscard]] std::wstring plain_stage_name(const std::string_view role) {
    if (role == "answer" || role == "draft" || role == "execute") {
        return L"Answer";
    }
    if (role == "automatic-fallback" || role == "fallback") {
        return L"Backup";
    }
    if (role == "verify" || role == "review") {
        return L"Check";
    }
    if (role == "fast-candidate") {
        return L"Fast choice";
    }
    if (role == "specialist-candidate") {
        return L"Specialist choice";
    }
    if (role == "select") {
        return L"Choose the better answer";
    }
    if (role == "plan") {
        return L"Plan";
    }
    return widen(role);
}

[[nodiscard]] std::wstring format_plan(
    const routing::RoutingPlan& plan,
    const models::ModelRegistry* registry = nullptr) {
    std::wostringstream stream;
    if (plan.stages.empty()) {
        stream << L"No ready AI can handle this message. Start another local AI app or repair the built-in AI.";
        return stream.str();
    }
    stream << L"AI choices\r\n";
    for (const auto& stage : plan.stages) {
        std::wstring model_name = widen(stage.model_id);
        std::wstring runtime_kind;
        if (stage.model_id == "verified-memory") {
            model_name = L"ThreadSpire verified knowledge";
            runtime_kind = L"built-in local memory";
        }
        if (registry != nullptr) {
            if (const auto* model = registry->find(stage.model_id); model != nullptr) {
                model_name = widen(model->display_name);
                runtime_kind = models::is_internal_model(*model)
                    ? L"built-in AI"
                    : L"another AI app";
            }
        }
        stream << L"• " << plain_stage_name(stage.role) << L": " << model_name;
        if (!runtime_kind.empty()) {
            stream << L" — " << runtime_kind;
        }
        stream << L"\r\n";
    }
    const auto used_backup = std::ranges::any_of(
        plan.stages,
        [](const routing::RouteStage& stage) { return stage.role == "automatic-fallback"; });
    if (used_backup) {
        stream << L"\r\nAutomatic used another AI after the first one could not answer.";
    } else if (plan.stages.size() > 1U) {
        stream << L"\r\nAutomatic may try these AIs in order.";
    } else if (plan.stages.front().model_id == "verified-memory") {
        stream << L"\r\nUsed an exact independently verified answer.";
    }
    return stream.str();
}

[[nodiscard]] std::string join_tool_events(const std::vector<std::string>& events) {
    std::string result;
    for (const auto& event : events) {
        if (!result.empty()) {
            result.push_back('\n');
        }
        result += event;
    }
    return result;
}

struct TrainingLibraryRecord final {
    std::string category;
    std::string identifier;
    std::string topic;
    std::string tags;
    std::string provenance;
    std::string verification;
    std::string prompt;
    std::string answer;
    std::string fingerprint;
};

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] std::string json_tags(const core::json::Value& record) {
    const auto* value = record.find("tags");
    const auto* tags = value == nullptr ? nullptr : value->as_array();
    if (tags == nullptr) {
        return {};
    }
    std::string result;
    for (const auto& tag : *tags) {
        const auto text = core::json::string_or(&tag);
        if (text.empty()) {
            continue;
        }
        if (!result.empty()) {
            result += ", ";
        }
        result += text;
    }
    return result;
}

void append_training_library_file(
    const std::filesystem::path& path,
    const std::string_view category,
    std::vector<TrainingLibraryRecord>& records,
    std::size_t& unreadable_records) {
    if (!std::filesystem::is_regular_file(path)) {
        return;
    }
    std::ifstream stream(path, std::ios::binary);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        std::string error;
        auto document = core::json::try_parse(line, error);
        if (!document.has_value() || !document->is_object()) {
            ++unreadable_records;
            continue;
        }

        const auto held_out = category == "Held-out check";
        const auto approved_result = category == "Approved result";
        if (!held_out && !approved_result &&
            document->find("accepted") != nullptr &&
            !core::json::bool_or(document->find("accepted"), false)) {
            continue;
        }
        if (approved_result) {
            const auto independently_checked =
                core::json::bool_or(document->find("human_accepted"), false) ||
                core::json::bool_or(document->find("deterministic_verified"), false) ||
                core::json::bool_or(document->find("reference_verified"), false);
            if (!core::json::bool_or(document->find("eligible_for_training"), false) ||
                !independently_checked ||
                core::json::bool_or(document->find("human_rejected"), false)) {
                continue;
            }
        }

        TrainingLibraryRecord record;
        record.category = std::string(category);
        record.identifier = core::json::string_or(document->find("id"), "unnamed-record");
        record.topic = core::json::string_or(
            document->find("skill"),
            core::json::string_or(document->find("domain"), "general"));
        record.tags = json_tags(*document);
        record.provenance = core::json::string_or(
            document->find("provenance"),
            held_out ? "bundled independent validation" : "bundled curated curriculum");

        if (approved_result) {
            record.prompt = core::json::string_or(document->find("prompt"));
            record.answer = core::json::string_or(document->find("response"));
            record.verification = core::json::string_or(document->find("verification_notes"));
            if (record.verification.empty()) {
                if (core::json::bool_or(document->find("human_accepted"), false)) {
                    record.verification = "Approved by the user";
                } else if (core::json::bool_or(document->find("reference_verified"), false)) {
                    record.verification = "Matched a trusted reference";
                } else {
                    record.verification = "Passed an independent deterministic check";
                }
            }
            record.fingerprint = core::json::string_or(document->find("content_hash"));
        } else {
            const auto* messages_value = document->find("messages");
            const auto* messages = messages_value == nullptr ? nullptr : messages_value->as_array();
            if (messages != nullptr) {
                for (const auto& message : *messages) {
                    const auto role = lower_ascii(core::json::string_or(message.find("role"), "user"));
                    const auto content = core::json::string_or(message.find("content"));
                    if (content.empty()) {
                        continue;
                    }
                    if (role == "assistant") {
                        if (!record.answer.empty()) {
                            record.answer += "\n";
                        }
                        record.answer += content;
                    } else {
                        if (!record.prompt.empty()) {
                            record.prompt += "\n";
                        }
                        record.prompt += role == "system" ? "Instruction: " : "User: ";
                        record.prompt += content;
                    }
                }
            }
            record.verification = held_out
                ? "Held out from training; used only to check the candidate"
                : "Accepted curated example";
        }
        if (record.prompt.empty() || record.answer.empty()) {
            ++unreadable_records;
            continue;
        }
        if (record.fingerprint.empty()) {
            record.fingerprint = record.prompt + '\n' + record.answer;
        }
        records.push_back(std::move(record));
    }
}

[[nodiscard]] std::wstring training_library_text(
    const AppPaths& paths,
    const std::string_view search_text,
    const int source_filter,
    std::wstring& summary) {
    std::vector<TrainingLibraryRecord> records;
    std::size_t unreadable_records = 0;
    append_training_library_file(
        paths.bootstrap_dataset,
        "Training example",
        records,
        unreadable_records);
    append_training_library_file(
        paths.memory / "episodes.jsonl",
        "Approved result",
        records,
        unreadable_records);
    append_training_library_file(
        paths.bootstrap_anchors,
        "Held-out check",
        records,
        unreadable_records);

    std::map<std::string, std::size_t, std::less<>> duplicate_counts;
    std::size_t training_count = 0;
    std::size_t approved_count = 0;
    std::size_t held_out_count = 0;
    for (const auto& record : records) {
        ++duplicate_counts[record.fingerprint];
        training_count += record.category == "Training example" ? 1U : 0U;
        approved_count += record.category == "Approved result" ? 1U : 0U;
        held_out_count += record.category == "Held-out check" ? 1U : 0U;
    }

    const auto query = lower_ascii(std::string(search_text));
    std::vector<const TrainingLibraryRecord*> visible;
    visible.reserve(records.size());
    for (const auto& record : records) {
        const auto source_matches = source_filter <= 0 ||
            (source_filter == 1 && record.category == "Training example") ||
            (source_filter == 2 && record.category == "Approved result") ||
            (source_filter == 3 && record.category == "Held-out check");
        if (!source_matches) {
            continue;
        }
        if (!query.empty()) {
            const auto searchable = lower_ascii(
                record.category + '\n' + record.identifier + '\n' + record.topic + '\n' +
                record.tags + '\n' + record.provenance + '\n' + record.verification + '\n' +
                record.prompt + '\n' + record.answer);
            if (searchable.find(query) == std::string::npos) {
                continue;
            }
        }
        visible.push_back(&record);
    }

    std::wostringstream summary_stream;
    summary_stream << L"Training Library — " << visible.size() << L" shown of " << records.size()
                   << L" records • " << training_count << L" training • " << approved_count
                   << L" approved results • " << held_out_count << L" held-out checks";
    if (unreadable_records > 0U) {
        summary_stream << L" • " << unreadable_records << L" unreadable";
    }
    summary = summary_stream.str();

    std::wostringstream output;
    output << L"WHAT THREADSPIRE LEARNS FROM\r\n"
           << L"Training examples and approved results can update weights. Held-out checks never train the model; they only decide whether a candidate is safe to use. No prompt or answer below is shortened.\r\n\r\n";
    if (visible.empty()) {
        output << L"No records match this view.";
        return output.str();
    }
    std::size_t index = 0;
    for (const auto* record : visible) {
        ++index;
        const auto duplicates = duplicate_counts[record->fingerprint];
        output << L"[" << index << L"] " << widen(record->category) << L"\r\n"
               << L"Name: " << widen(record->identifier) << L"\r\n"
               << L"Topic: " << widen(record->topic) << L"\r\n";
        if (!record->tags.empty()) {
            output << L"Tags: " << widen(record->tags) << L"\r\n";
        }
        output << L"Source: " << widen(record->provenance) << L"\r\n"
               << L"Checks: " << widen(record->verification) << L"\r\n";
        if (duplicates > 1U) {
            output << L"Duplicate copies: " << duplicates
                   << L" (training uses one copy)\r\n";
        }
        output << L"\r\nPROMPT\r\n" << widen(record->prompt)
               << L"\r\n\r\nEXPECTED ANSWER\r\n" << widen(record->answer)
               << L"\r\n\r\n------------------------------------------------------------\r\n\r\n";
    }
    return output.str();
}
[[nodiscard]] double coding_score(const models::ModelProfile& model) noexcept {
    return model.quality * 0.55 + model.reliability * 0.25 +
           std::min(1.0, model.tokens_per_second / 100.0) * 0.10 +
           static_cast<double>(model.priority) / 2000.0 +
           (model.coding_agent ? 0.08 : 0.0);
}


} // namespace

MainWindow::MainWindow(const HINSTANCE instance)
    : instance_(instance),
      paths_(make_app_paths()),
      model_registry_(models::ModelRegistry::defaults()),
      mcp_client_(std::make_shared<mcp::Client>()),
      backend_server_(paths_) {
    initialize_base_resources(paths_);
    model_registry_ = models::ModelRegistry::load(paths_.model_registry);
    auto migrated_runtime_labels = false;
    for (auto& model : model_registry_.models()) {
        if (models::is_internal_model(model) &&
            model.display_name.starts_with("Custom AI")) {
            model.display_name.replace(0, std::string_view{"Custom AI"}.size(), "ThreadSpire");
            migrated_runtime_labels = true;
        }
    }
    if (migrated_runtime_labels) {
        try {
            model_registry_.save(paths_.model_registry);
        } catch (...) {
        }
    }
    // Heavy runtime files stay in the optional pack; the native app coordinates them.
    selected_python_ = paths_.python;
    const auto saved_pack_source = trim_line_endings(read_text(
        paths_.root / "config" / "internal-ai-setup-files.txt"));
    if (!saved_pack_source.empty()) {
        selected_pack_source_ = std::filesystem::path(widen(saved_pack_source)).lexically_normal();
    }

    preferred_model_id_ = read_text(paths_.root / "config" / "preferred-model.txt");
    preferred_model_id_ = trim_line_endings(std::move(preferred_model_id_));

    const auto preferred_source = trim_line_endings(read_text(
        paths_.root / "config" / "preferred-source.txt"));
    if (preferred_source == "automatic") {
        inference_source_ = InferenceSource::automatic;
    } else if (preferred_source == "custom" || preferred_source == "internal") {
        inference_source_ = InferenceSource::custom_model;
    } else if (preferred_source == "local") {
        inference_source_ = InferenceSource::local_bridge;
    } else {
        inference_source_ = InferenceSource::automatic;
    }

    const auto saved_training_target = trim_line_endings(read_text(
        paths_.root / "config" / "training-target.txt"));
    training_target_ = saved_training_target.empty()
        ? std::string{"english-foundation"}
        : normalize_training_skill(saved_training_target);
    const auto saved_custom_skill = trim_line_endings(read_text(
        paths_.root / "config" / "custom-training-skill.txt"));
    custom_training_skill_ = saved_custom_skill.empty()
        ? std::string{"custom-skill"}
        : normalize_training_skill(saved_custom_skill);
    const auto saved_custom_dataset = trim_line_endings(read_text(
        paths_.root / "config" / "custom-training-dataset.txt"));
    custom_training_dataset_ = saved_custom_dataset.empty()
        ? paths_.custom_training_template
        : std::filesystem::path(widen(saved_custom_dataset));
    if (!std::filesystem::is_regular_file(custom_training_dataset_)) {
        custom_training_dataset_ = paths_.custom_training_template;
    }
    const auto curriculum_marker =
        paths_.root / "state" / "english-first-curriculum-v1.txt";
    if (!std::filesystem::is_regular_file(curriculum_marker)) {
        // Version 0.5.2 exposed a C++ target by default even when the private
        // model had not established basic language behavior. New and upgraded
        // installations start with English, but the user can switch targets.
        training_target_ = "english-foundation";
        try {
            write_text(paths_.root / "config" / "training-target.txt", training_target_ + "\n");
            write_text(curriculum_marker, "english-first\n");
        } catch (...) {
        }
    }

    try {
        const auto legacy_cursor = trim_line_endings(read_text(
            paths_.root / "state" / "piggyback-seed-cursor.txt"));
        if (!legacy_cursor.empty()) {
            // The legacy cursor belonged to the old C++-first target. Do not
            // make English inherit that position in the mixed seed corpus.
            piggyback_seed_offsets_[training_cursor_key(
                "cpp23-core", paths_.data / "seed.jsonl")] =
                static_cast<std::size_t>(std::stoull(legacy_cursor));
        }
    } catch (...) {
        piggyback_seed_offsets_.erase(training_cursor_key(
            "cpp23-core", paths_.data / "seed.jsonl"));
    }

    std::string base_model = "threadspire://coder";
    const models::ModelProfile* best = nullptr;
    for (const auto& model : model_registry_.models()) {
        if (!model.custom_model || !model.coding_agent) {
            continue;
        }
        if (best == nullptr || coding_score(model) > coding_score(*best)) {
            best = &model;
        }
    }
    if (best != nullptr) {
        base_model = best->model_name;
    }
    const auto internal_base =
        base_model.starts_with("threadspire://") ||
        base_model.starts_with("internal://");

    learning::LearningSettings settings{
        .python_executable = selected_python_,
        .worker_script = paths_.worker,
        .state_root = paths_.root,
        .seed_dataset = paths_.bootstrap_dataset,
        .anchor_dataset = paths_.bootstrap_anchors,
        .base_model = std::move(base_model),
    };
    const auto initial_internal_state = runtime::inspect_internal_model_state(paths_);
    bootstrap_priority_lease_ = !initial_internal_state.bootstrap_complete;
    settings.initial_interactive_priority = bootstrap_priority_lease_;
    // Coalesce independently verified evidence into meaningful candidates.
    // This prevents one candidate launch per accepted episode.
    settings.policy.lora_microbatch = internal_base ? 8U : 4U;
    learning_service_ = std::make_unique<learning::ContinualLearningService>(std::move(settings));
    active_log_ = paths_.logs / "model-server.log";
    interactive_model_verified_ = std::filesystem::is_regular_file(
        paths_.root / "inference-verified.flag");
    const auto lifecycle_setting = trim_line_endings(read_text(
        paths_.root / "config" / "automatic-lifecycle.txt"));
    automatic_lifecycle_enabled_ = lifecycle_setting != "off";
    const auto training_setting = trim_line_endings(read_text(
        paths_.root / "config" / "automatic-training.txt"));
    automatic_training_enabled_ = training_setting != "off";
    lifecycle_retry_at_ = std::chrono::steady_clock::now();
    next_piggyback_at_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
}

MainWindow::~MainWindow() {
    backend_operation_.cancel();
    discovery_operation_.cancel();
    mcp_operation_.cancel();
    piggyback_operation_.cancel();
    chat_operation_.cancel();
    long_horizon_operation_.cancel();
    media_operation_.cancel();
    storage_operation_.cancel();

    backend_operation_.wait();
    discovery_operation_.wait();
    mcp_operation_.wait();
    piggyback_operation_.wait();
    chat_operation_.wait();
    long_horizon_operation_.wait();
    media_operation_.wait();
    storage_operation_.wait();

    if (learning_service_ != nullptr) {
        learning_service_->request_stop();
        learning_service_.reset();
    }
    backend_server_.stop();
    if (font_ != nullptr) {
        static_cast<void>(::DeleteObject(font_));
    }
    if (window_brush_ != nullptr) {
        static_cast<void>(::DeleteObject(window_brush_));
    }
    if (edit_brush_ != nullptr) {
        static_cast<void>(::DeleteObject(edit_brush_));
    }
}

bool MainWindow::register_window_class() {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &MainWindow::window_procedure;
    window_class.hInstance = instance_;
    window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hIconSm = ::LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = window_brush_;
    window_class.lpszClassName = window_class_name;
    return ::RegisterClassExW(&window_class) != 0 ||
           ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool MainWindow::create(const int show_command) {
    window_brush_ = ::CreateSolidBrush(window_color);
    edit_brush_ = ::CreateSolidBrush(edit_color);
    if (window_brush_ == nullptr || edit_brush_ == nullptr || !register_window_class()) {
        return false;
    }

    window_ = ::CreateWindowExW(
        0,
        window_class_name,
        L"ThreadSpire — local evolving AI",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1450,
        930,
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        return false;
    }
    const BOOL dark_mode = TRUE;
    static_cast<void>(::DwmSetWindowAttribute(
        window_,
        immersive_dark_mode_attribute,
        &dark_mode,
        sizeof(dark_mode)));
    static_cast<void>(::SetWindowTheme(window_, L"DarkMode_Explorer", nullptr));
    ::ShowWindow(window_, show_command);
    static_cast<void>(::UpdateWindow(window_));
    static_cast<void>(::SetTimer(window_, update_timer, 250, nullptr));
    static_cast<void>(::SetTimer(window_, stream_timer, 40, nullptr));
    return true;
}

HWND MainWindow::handle() const noexcept {
    return window_;
}

LRESULT CALLBACK MainWindow::window_procedure(
    const HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) {
    MainWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->window_ = window;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return self != nullptr
        ? self->handle_message(message, wparam, lparam)
        : ::DefWindowProcW(window, message, wparam, lparam);
}

LRESULT MainWindow::handle_message(
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        dpi_ = ::GetDpiForWindow(window_);
        recreate_ui_font();
        create_controls();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* information = reinterpret_cast<MINMAXINFO*>(lparam);
        information->ptMinTrackSize.x = scaled(980, dpi_);
        information->ptMinTrackSize.y = scaled(680, dpi_);
        return 0;
    }
    case WM_SIZE:
        layout_controls();
        static_cast<void>(::RedrawWindow(
            window_,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN));
        return 0;
    case WM_EXITSIZEMOVE:
        static_cast<void>(::RedrawWindow(
            window_,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME |
                RDW_ALLCHILDREN | RDW_UPDATENOW));
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        recreate_ui_font();
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        static_cast<void>(::SetWindowPos(
            window_, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE));
        layout_controls();
        return 0;
    }
    case WM_COMMAND: {
        const auto identifier = static_cast<int>(LOWORD(wparam));
        const auto notification = static_cast<int>(HIWORD(wparam));
        if (identifier == id::chat_mode && notification == CBN_SELCHANGE) {
            update_workspace_mode();
        } else if ((identifier == id::chat_source || identifier == id::coding_source) &&
                   notification == CBN_SELCHANGE) {
            update_inference_source(identifier);
        } else if ((identifier == id::chat_model || identifier == id::coding_model) &&
                   notification == CBN_SELCHANGE) {
            update_model_selection(identifier);
        } else if (identifier == id::learning_piggyback_skill &&
                   notification == CBN_SELCHANGE) {
            handle_training_target_change();
        } else if (identifier == id::learning_custom_skill &&
                   notification == EN_KILLFOCUS) {
            handle_training_target_change();
        } else {
            handle_command(identifier);
        }
        return 0;
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lparam);
        if (header->idFrom == static_cast<UINT_PTR>(id::tabs) && header->code == TCN_SELCHANGE) {
            const auto selected = TabCtrl_GetCurSel(control(id::tabs));
            TCITEMW item{};
            item.mask = TCIF_PARAM;
            if (selected >= 0 && TabCtrl_GetItem(control(id::tabs), selected, &item) != FALSE) {
                show_page(static_cast<Page>(std::clamp<LONG_PTR>(
                    item.lParam,
                    static_cast<LONG_PTR>(Page::chat),
                    static_cast<LONG_PTR>(Page::logs))));
            }
        }
        return 0;
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        return item != nullptr && draw_dark_control_item(*item) ? TRUE : FALSE;
    }
    case WM_MEASUREITEM: {
        auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (item != nullptr && item->CtlType == ODT_COMBOBOX) {
            item->itemHeight = static_cast<UINT>(scaled(25, dpi_));
            return TRUE;
        }
        return FALSE;
    }
    case WM_TIMER:
        if (wparam == update_timer) {
            update_periodic_state();
        } else if (wparam == stream_timer) {
            flush_stream_text();
        }
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSCROLLBAR: {
        const auto device_context = reinterpret_cast<HDC>(wparam);
        const auto child = reinterpret_cast<HWND>(lparam);
        const auto editable = message == WM_CTLCOLOREDIT ||
                              message == WM_CTLCOLORLISTBOX ||
                              is_edit_control(child);
        const auto enabled = child == nullptr || ::IsWindowEnabled(child) != FALSE;
        ::SetTextColor(device_context, enabled ? text_color : disabled_text_color);
        ::SetBkColor(device_context, editable ? edit_color : window_color);
        if (!editable) {
            ::SetBkMode(device_context, TRANSPARENT);
        }
        return reinterpret_cast<LRESULT>(editable ? edit_brush_ : window_brush_);
    }
    case WM_ERASEBKGND: {
        RECT client{};
        static_cast<void>(::GetClientRect(window_, &client));
        static_cast<void>(::FillRect(
            reinterpret_cast<HDC>(wparam),
            &client,
            window_brush_));
        return 1;
    }
    case WM_CLOSE:
        chat_operation_.cancel();
        long_horizon_operation_.cancel();
        piggyback_operation_.cancel();
        discovery_operation_.cancel();
        mcp_operation_.cancel();
        ::DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        static_cast<void>(::KillTimer(window_, update_timer));
        static_cast<void>(::KillTimer(window_, stream_timer));
        ::PostQuitMessage(0);
        return 0;
    default:
        return ::DefWindowProcW(window_, message, wparam, lparam);
    }
}

HWND MainWindow::create_control(
    const wchar_t* class_name,
    const wchar_t* text,
    const DWORD style,
    const DWORD extended_style,
    const int identifier,
    const HWND parent) {
    auto effective_style = style;
    auto effective_extended_style = extended_style & ~WS_EX_CLIENTEDGE;
    const auto is_static =
        class_name != nullptr && ::lstrcmpiW(class_name, L"STATIC") == 0;
    const auto is_button =
        class_name != nullptr && ::lstrcmpiW(class_name, L"BUTTON") == 0;
    const auto is_combo =
        class_name != nullptr && ::lstrcmpiW(class_name, L"COMBOBOX") == 0;
    if (is_static && text != nullptr && text[0] != L'\0' &&
        (effective_style & SS_TYPEMASK) == SS_LEFT &&
        (effective_style & SS_EDITCONTROL) == 0U &&
        std::wstring_view{text}.find_first_of(L"\r\n") == std::wstring_view::npos) {
        effective_style |= SS_LEFTNOWORDWRAP | SS_CENTERIMAGE | SS_ENDELLIPSIS;
    }
    if (is_button) {
        const auto type = effective_style & BS_TYPEMASK;
        if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) {
            effective_style = (effective_style & ~BS_TYPEMASK) | BS_OWNERDRAW;
        }
    }
    if (is_combo && (effective_style & 0x0003U) == CBS_DROPDOWNLIST) {
        effective_style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
    }

    const auto handle = ::CreateWindowExW(
        effective_extended_style,
        class_name,
        text,
        effective_style,
        0,
        0,
        1,
        1,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
        instance_,
        nullptr);
    if (handle == nullptr) {
        throw std::runtime_error("CreateWindowExW failed");
    }
    controls_[identifier] = handle;
    apply_font(handle);
    if (!is_button || (effective_style & BS_TYPEMASK) != BS_OWNERDRAW) {
        static_cast<void>(::SetWindowTheme(handle, L"DarkMode_Explorer", nullptr));
    }
    if (is_rich_edit_control(handle)) {
        static_cast<void>(::SendMessageW(handle, EM_SETBKGNDCOLOR, 0, edit_color));
    }
    if (is_edit_control(handle)) {
        const auto margin = static_cast<LPARAM>(MAKELONG(
            scaled(6, dpi_),
            scaled(6, dpi_)));
        static_cast<void>(::SendMessageW(
            handle,
            EM_SETMARGINS,
            EC_LEFTMARGIN | EC_RIGHTMARGIN,
            margin));
    }
    return handle;
}

void MainWindow::create_controls() {
    INITCOMMONCONTROLSEX common{};
    common.dwSize = sizeof(common);
    common.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS;
    static_cast<void>(::InitCommonControlsEx(&common));

    const auto tabs = create_control(
        WC_TABCONTROLW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
            TCS_FIXEDWIDTH | TCS_OWNERDRAWFIXED,
        0,
        id::tabs,
        window_);
    static_cast<void>(::SendMessageW(
        tabs,
        TCM_SETITEMSIZE,
        0,
        MAKELPARAM(scaled(150, dpi_), scaled(30, dpi_))));
    const std::array tabs_and_pages{
        std::pair{L"Chat", Page::chat},
        std::pair{L"AI Setup", Page::models},
        std::pair{L"Learning", Page::learning},
        std::pair{L"Skills", Page::skills},
        std::pair{L"Storage & Logs", Page::logs},
    };
    auto selected_tab = 0;
    auto tab_index = 0;
    for (const auto& [label, page] : tabs_and_pages) {
        TCITEMW item{};
        item.mask = TCIF_TEXT | TCIF_PARAM;
        item.pszText = const_cast<wchar_t*>(label);
        item.lParam = static_cast<LPARAM>(page);
        static_cast<void>(TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs), &item));
        selected_tab = page == page_ ? tab_index : selected_tab;
        ++tab_index;
    }

    static_cast<void>(TabCtrl_SetCurSel(tabs, selected_tab));
    create_control(
        L"STATIC", L"Discovering local AI…",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
        0, id::status, window_);

    for (const auto [page_id, page] : std::array{
             std::pair{id::page_chat, Page::chat},
             std::pair{id::page_coding, Page::coding},
             std::pair{id::page_models, Page::models},
             std::pair{id::page_learning, Page::learning},
             std::pair{id::page_skills, Page::skills},
             std::pair{id::page_long_horizon, Page::long_horizon},
             std::pair{id::page_logs, Page::logs}}) {
        const auto visibility = page == page_ ? WS_VISIBLE : 0U;
        const auto handle = create_control(
            L"STATIC",
            L"",
            WS_CHILD | WS_CLIPCHILDREN | visibility,
            WS_EX_CONTROLPARENT,
            page_id,
            window_);
        if (::SetWindowSubclass(
                handle,
                page_host_subclass,
                static_cast<UINT_PTR>(page_id),
                reinterpret_cast<DWORD_PTR>(window_)) == FALSE) {
            throw std::runtime_error("SetWindowSubclass failed for a page host");
        }
        page_windows_.push_back(handle);
    }

    create_chat_page();
    create_coding_page();
    create_models_page();
    create_learning_page();
    create_skills_page();
    create_long_horizon_page();
    create_logs_page();

    const auto saved_mcp = read_text(paths_.root / "config" / "mcp-endpoint.txt");
    if (!saved_mcp.empty()) {
        set_control_text(id::models_mcp_endpoint, widen(saved_mcp));
    }

    refresh_model_selectors();
    update_workspace_mode();
    layout_controls();
    load_models_editor();
    update_skill_report();
    update_learning_report();
    // Automatic setup is coordinated independently of model selection. It yields to
    // active work and never prevents Chat from using another connected AI.
    drive_automatic_lifecycle();
    discover_local_ai();
    if (!saved_mcp.empty()) {
        connect_mcp();
    }
}

void MainWindow::create_chat_page() {
    const auto parent = control(id::page_chat);

    create_control(L"STATIC", L"Task", WS_CHILD | WS_VISIBLE, 0, id::chat_mode_label, parent);
    const auto mode = create_control(
        L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        WS_EX_CLIENTEDGE, id::chat_mode, parent);
    static_cast<void>(::SendMessageW(
        mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ask / write")));
    static_cast<void>(::SendMessageW(
        mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Code help")));
    static_cast<void>(::SendMessageW(
        mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Repository task")));
    static_cast<void>(::SendMessageW(
        mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Generate image")));
    static_cast<void>(::SendMessageW(
        mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Generate speech")));
    static_cast<void>(::SendMessageW(
        mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Use tools")));
    static_cast<void>(::SendMessageW(mode, CB_SETCURSEL, 0, 0));

    create_control(L"STATIC", L"Use", WS_CHILD | WS_VISIBLE, 0, id::chat_source_label, parent);
    const auto source = create_control(
        L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        WS_EX_CLIENTEDGE, id::chat_source, parent);
    static_cast<void>(::SendMessageW(
        source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automatic — fastest ready AI")));
    static_cast<void>(::SendMessageW(
        source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Another AI app only")));
    static_cast<void>(::SendMessageW(
        source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Built-in AI only")));
    static_cast<void>(::SendMessageW(
        source,
        CB_SETCURSEL,
        static_cast<WPARAM>(inference_source_),
        0));

    create_control(L"STATIC", L"AI", WS_CHILD | WS_VISIBLE, 0, id::chat_model_label, parent);
    create_control(
        L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        CBS_DROPDOWNLIST | WS_VSCROLL,
        WS_EX_CLIENTEDGE, id::chat_model, parent);
    create_control(
        L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::chat_discover, parent);
    create_control(
        L"BUTTON", L"AI setup…", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::chat_open_models, parent);
    create_control(
        L"STATIC", L"Using: checking available AI • Built-in AI: checking automatic setup",
        WS_CHILD | WS_VISIBLE, 0, id::chat_backend, parent);

    create_control(
        L"STATIC", L"Repository",
        WS_CHILD | WS_VISIBLE, 0, id::chat_repository_label, parent);
    create_control(
        L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, id::chat_repository, parent);
    create_control(
        L"BUTTON", L"Browse…", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::chat_repository_browse, parent);

    create_control(
        L"STATIC", L"Conversation", WS_CHILD | WS_VISIBLE,
        0, id::chat_response_label, parent);
    create_control(
        transcript_control_class(),
        L"Type a message below. ThreadSpire automatically uses the best AI that is ready.\r\n\r\n"
        L"OTHER AI APPS\r\n"
        L"ThreadSpire finds and uses compatible AI apps already running on this computer.\r\n\r\n"
        L"BUILT-IN AI\r\n"
        L"Setup, learning, recovery, and startup happen automatically in the background.",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        WS_EX_CLIENTEDGE, id::chat_response, parent);

    create_control(
        L"STATIC", L"Message", WS_CHILD | WS_VISIBLE,
        0, id::chat_prompt_label, parent);
    const auto prompt = create_control(
        L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        WS_EX_CLIENTEDGE, id::chat_prompt, parent);
    static_cast<void>(::SetWindowSubclass(
        prompt,
        prompt_edit_subclass,
        1,
        static_cast<DWORD_PTR>(id::chat_send)));

    create_control(
        L"BUTTON", L"Run task", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, id::chat_send, parent);
    create_control(
        L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::chat_stop, parent);
    create_control(
        L"BUTTON", L"Accept + learn", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::chat_accept, parent);
    create_control(
        L"BUTTON", L"Reject", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::chat_reject, parent);
    create_control(
        L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::chat_clear, parent);
    create_control(
        L"STATIC", L"Ctrl+Enter runs. Shift+Enter or Enter inserts a new line.",
        WS_CHILD | WS_VISIBLE, 0, id::chat_hint, parent);
    create_control(
        L"BUTTON", L"Open outputs", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::chat_open_output, parent);

    create_control(
        L"STATIC", L"How it will run", WS_CHILD | WS_VISIBLE,
        0, id::chat_plan_label, parent);
    create_control(
        L"BUTTON", L"Check choices", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::chat_route, parent);
    create_control(
        L"EDIT", L"Waiting for a message.", WS_CHILD | WS_VISIBLE | WS_BORDER |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        WS_EX_CLIENTEDGE, id::chat_plan, parent);

    create_control(
        L"BUTTON", L"Learn automatically from approved results",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, id::chat_auto_learning, parent);
    set_checked(id::chat_auto_learning, automatic_training_enabled_);
    create_control(
        L"STATIC",
        L"Automatic learning is on. It waits while you are using the app.",
        WS_CHILD | WS_VISIBLE | SS_EDITCONTROL, 0, id::chat_learning_status, parent);
    create_control(
        L"STATIC", L"Optional tools: not connected",
        WS_CHILD | WS_VISIBLE | SS_EDITCONTROL, 0, id::chat_mcp_status, parent);
}

void MainWindow::create_coding_page() {
    const auto parent = control(id::page_coding);

    create_control(
        L"STATIC", L"Use", WS_CHILD | WS_VISIBLE,
        0, id::coding_source_label, parent);
    const auto source = create_control(
        L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        WS_EX_CLIENTEDGE, id::coding_source, parent);
    static_cast<void>(::SendMessageW(
        source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automatic — fastest ready AI")));
    static_cast<void>(::SendMessageW(
        source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Another AI app only")));
    static_cast<void>(::SendMessageW(
        source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Built-in AI only")));
    static_cast<void>(::SendMessageW(
        source,
        CB_SETCURSEL,
        static_cast<WPARAM>(inference_source_),
        0));

    create_control(
        L"STATIC", L"Choose AI", WS_CHILD | WS_VISIBLE,
        0, id::coding_model_label, parent);
    create_control(
        L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        CBS_DROPDOWNLIST | WS_VSCROLL,
        WS_EX_CLIENTEDGE, id::coding_model, parent);
    create_control(
        L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::coding_discover, parent);
    create_control(
        L"BUTTON", L"AI setup…", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::coding_open_models, parent);
    create_control(
        L"STATIC", L"AI source: connected local LLM • no coding model discovered yet", WS_CHILD | WS_VISIBLE,
        0, id::coding_backend, parent);

    create_control(
        L"STATIC", L"Repository",
        WS_CHILD | WS_VISIBLE, 0, id::coding_repository_label, parent);
    create_control(
        L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, id::coding_repository, parent);
    create_control(
        L"BUTTON", L"Browse…", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::coding_browse, parent);

    create_control(
        L"STATIC", L"Coding task", WS_CHILD | WS_VISIBLE,
        0, id::coding_task_label, parent);
    const auto task = create_control(
        L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        WS_EX_CLIENTEDGE, id::coding_task, parent);
    static_cast<void>(::SetWindowSubclass(
        task,
        prompt_edit_subclass,
        2,
        static_cast<DWORD_PTR>(id::coding_ask)));

    create_control(
        L"BUTTON", L"Ask coding model", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, id::coding_ask, parent);
    create_control(
        L"BUTTON", L"Run build/test agent", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::coding_run, parent);
    create_control(
        L"BUTTON", L"Accept + learn", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::coding_accept, parent);
    create_control(
        L"BUTTON", L"Reject", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::coding_reject, parent);

    create_control(
        L"STATIC", L"Agent route", WS_CHILD | WS_VISIBLE,
        0, id::coding_plan_label, parent);
    create_control(
        L"EDIT", L"Waiting for a coding task.", WS_CHILD | WS_VISIBLE | WS_BORDER |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        WS_EX_CLIENTEDGE, id::coding_plan, parent);

    create_control(
        L"STATIC", L"Coding response", WS_CHILD | WS_VISIBLE,
        0, id::coding_response_label, parent);
    create_control(
        transcript_control_class(), L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        WS_EX_CLIENTEDGE, id::coding_response, parent);
}

void MainWindow::create_models_page() {
    const auto parent = control(id::page_models);
    create_control(
        L"STATIC", L"Built-in AI",
        WS_CHILD | WS_VISIBLE, 0, id::models_backend_header, parent);
    create_control(
        L"BUTTON", L"Keep built-in AI ready automatically",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, id::models_automation_toggle, parent);
    set_checked(id::models_automation_toggle, automatic_lifecycle_enabled_);
    create_control(
        L"STATIC", L"Built-in AI is not installed.",
        WS_CHILD | WS_VISIBLE, 0, id::models_internal_status, parent);
    const auto progress = create_control(
        PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        0, id::models_progress, parent);
    static_cast<void>(::SendMessageW(progress, PBM_SETRANGE32, 0, 1000));
    static_cast<void>(::SendMessageW(progress, PBM_SETPOS, 0, 0));
    static_cast<void>(::SendMessageW(
        progress,
        PBM_SETBKCOLOR,
        0,
        static_cast<LPARAM>(edit_color)));
    static_cast<void>(::SendMessageW(
        progress,
        PBM_SETBARCOLOR,
        0,
        static_cast<LPARAM>(accent_color)));
    create_control(
        L"STATIC", L"ThreadSpire is checking what is ready.",
        WS_CHILD | WS_VISIBLE, 0, id::models_progress_detail, parent);

    create_control(
        L"STATIC", L"Other AI apps on this computer",
        WS_CHILD | WS_VISIBLE, 0, id::models_local_header, parent);
    create_control(
        L"BUTTON", L"Find AI apps now", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, id::models_discover, parent);
    create_control(
        L"STATIC", L"Looking for compatible AI apps…",
        WS_CHILD | WS_VISIBLE, 0, id::models_discovery_status, parent);

    create_control(
        L"BUTTON", L"Backup and repair options",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, id::models_advanced_header, parent);
    set_checked(id::models_advanced_header, false);

    create_control(L"STATIC", L"Address", WS_CHILD | WS_VISIBLE, 0, id::models_manual_endpoint_label, parent);
    create_control(
        L"EDIT", L"http://127.0.0.1:1234", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, id::models_manual_endpoint, parent);
    create_control(L"STATIC", L"AI name", WS_CHILD | WS_VISIBLE, 0, id::models_manual_model_label, parent);
    create_control(
        L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, id::models_manual_model, parent);
    const auto protocol = create_control(
        L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        WS_EX_CLIENTEDGE, id::models_manual_protocol, parent);
    static_cast<void>(::SendMessageW(
        protocol, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Most local AI apps")));
    static_cast<void>(::SendMessageW(
        protocol, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ollama")));
    static_cast<void>(::SendMessageW(protocol, CB_SETCURSEL, 0, 0));
    create_control(
        L"BUTTON", L"Can write code", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, id::models_manual_coding, parent);
    set_checked(id::models_manual_coding, true);
    create_control(
        L"BUTTON", L"Can help learning", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, id::models_manual_teacher, parent);
    set_checked(id::models_manual_teacher, true);
    create_control(
        L"BUTTON", L"Add AI", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::models_manual_add, parent);
    create_control(
        L"STATIC", L"What it can do", WS_CHILD | WS_VISIBLE,
        0, id::models_manual_capabilities_label, parent);
    create_control(
        L"EDIT", L"chat,code,repository,tools", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, id::models_manual_capabilities, parent);

    create_control(
        L"STATIC", L"Optional tool connection",
        WS_CHILD | WS_VISIBLE, 0, id::models_mcp_header, parent);
    create_control(L"STATIC", L"Tool address", WS_CHILD | WS_VISIBLE, 0, id::models_mcp_label, parent);
    create_control(
        L"EDIT", L"http://127.0.0.1:3000/mcp", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, id::models_mcp_endpoint, parent);
    create_control(
        L"BUTTON", L"Connect tools", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::models_mcp_connect, parent);
    create_control(
        L"STATIC", L"No optional tools connected",
        WS_CHILD | WS_VISIBLE, 0, id::models_mcp_status, parent);

    create_control(
        L"STATIC", L"Built-in AI size",
        WS_CHILD | WS_VISIBLE, 0, id::models_internal_profile_label, parent);
    const auto internal_profile = create_control(
        L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        WS_EX_CLIENTEDGE, id::models_internal_profile, parent);
    static_cast<void>(::SendMessageW(
        internal_profile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Small — fastest setup")));
    static_cast<void>(::SendMessageW(
        internal_profile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Medium — balanced")));
    static_cast<void>(::SendMessageW(
        internal_profile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Large — best quality")));
    static_cast<void>(::SendMessageW(internal_profile, CB_SETCURSEL, 0, 0));
    create_control(
        L"BUTTON", L"Rebuild from scratch", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::models_internal_create, parent);
    create_control(
        L"BUTTON", L"Continue setup", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::models_internal_bootstrap, parent);
    create_control(
        L"STATIC", L"Setup files",
        WS_CHILD | WS_VISIBLE, 0, id::models_python_label, parent);
    create_control(
        L"EDIT", selected_pack_source_.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, id::models_python, parent);
    create_control(
        L"BUTTON", L"Choose folder…", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::models_browse, parent);
    create_control(
        L"BUTTON", L"Replace setup files", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::models_install, parent);
    create_control(
        L"BUTTON", L"Check setup", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::models_verify, parent);
    create_control(
        L"BUTTON", L"Start built-in AI", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::models_start, parent);
    create_control(
        L"BUTTON", L"Stop built-in AI", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::models_stop, parent);
    create_control(
        L"STATIC", L"No backup action has run",
        WS_CHILD | WS_VISIBLE, 0, id::models_status, parent);

    create_control(
        L"STATIC", L"Saved AI list",
        WS_CHILD | WS_VISIBLE, 0, id::models_registry_header, parent);
    create_control(
        L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL,
        WS_EX_CLIENTEDGE, id::models_editor, parent);
    create_control(
        L"BUTTON", L"Undo list edits", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::models_reload, parent);
    create_control(
        L"BUTTON", L"Save AI list", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::models_save, parent);
}
void MainWindow::create_learning_page() {
    const auto parent = control(id::page_learning);
    create_control(
        L"STATIC", L"Learning — automatic by default; every accepted example is visible below",
        WS_CHILD | WS_VISIBLE, 0, id::learning_header, parent);
    create_control(
        L"BUTTON", L"Pause background learning",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, id::learning_pause, parent);
    create_control(
        L"BUTTON", L"Learn automatically while the app is idle",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, id::learning_auto, parent);
    set_checked(id::learning_auto, automatic_training_enabled_);
    create_control(
        L"BUTTON", L"Stop current learning pass",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::learning_stop, parent);

    create_control(
        L"STATIC", L"Training target",
        WS_CHILD | WS_VISIBLE, 0, id::learning_skill_label, parent);
    const auto target = create_control(
        L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, id::learning_piggyback_skill, parent);
    for (const auto& option : training_targets) {
        static_cast<void>(::SendMessageW(
            target,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(option.label)));
    }

    create_control(
        L"STATIC", L"Custom topic name",
        WS_CHILD | WS_VISIBLE, 0, id::learning_custom_skill_label, parent);
    create_control(
        L"EDIT", widen(custom_training_skill_).c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        0, id::learning_custom_skill, parent);

    create_control(
        L"STATIC", L"New examples to check",
        WS_CHILD | WS_VISIBLE, 0, id::learning_count_label, parent);
    create_control(
        L"EDIT", L"8",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        0, id::learning_piggyback_count, parent);
    create_control(
        L"BUTTON", L"Find more examples now",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::learning_piggyback, parent);
    create_control(
        L"BUTTON", L"Improve selected topic now",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::learning_build_custom, parent);

    create_control(
        L"STATIC", L"My examples file",
        WS_CHILD | WS_VISIBLE, 0, id::learning_dataset_label, parent);
    create_control(
        L"EDIT", custom_training_dataset_.wstring().c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        0, id::learning_dataset_path, parent);
    create_control(
        L"BUTTON", L"Browse…",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::learning_dataset_browse, parent);
    create_control(
        L"BUTTON", L"Add verified file",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::learning_dataset_import, parent);
    create_control(
        L"BUTTON", L"Open example template",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::learning_dataset_template, parent);

    create_control(
        L"STATIC", L"Selected target: English foundation",
        WS_CHILD | WS_VISIBLE, 0, id::learning_piggyback_status, parent);
    create_control(
        L"STATIC",
        L"CUSTOM TRAINING WORKFLOW\r\n"
        L"1. Select exactly what this pass should teach. English foundation is the upgrade default.\r\n"
        L"2. Generate examples with an external local teacher plus an independent verifier, or browse to your own JSONL and import it as trusted data.\r\n"
        L"3. Verified evidence automatically queues a shadow update. Queue/retry can manually coalesce another update for the selected target.\r\n"
        L"4. ThreadSpire evaluates the shadow candidate before promotion. Changing the target cancels only the active example pass and applies immediately to the next pass. Existing weights and accepted records are preserved.",
        WS_CHILD | WS_VISIBLE | SS_EDITCONTROL,
        0,
        id::learning_policy,
        parent);
    create_control(
        L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
        0, id::learning_jobs, parent);
    create_control(
        L"STATIC", L"Current learning activity",
        WS_CHILD | WS_VISIBLE, 0, id::learning_activity_header, parent);
    create_control(
        L"STATIC", L"Training Library — loading accepted examples…",
        WS_CHILD | WS_VISIBLE, 0, id::learning_library_header, parent);
    const auto library_filter = create_control(
        L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        0, id::learning_library_filter, parent);
    static_cast<void>(::SendMessageW(
        library_filter,
        EM_SETCUEBANNER,
        TRUE,
        reinterpret_cast<LPARAM>(L"Search prompts, answers, topics, tags, or sources")));
    const auto library_source = create_control(
        L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, id::learning_library_source, parent);
    for (const auto* option : {
             L"Everything",
             L"Training examples",
             L"Approved results",
             L"Held-out checks"}) {
        static_cast<void>(::SendMessageW(
            library_source,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(option)));
    }
    static_cast<void>(::SendMessageW(library_source, CB_SETCURSEL, 0, 0));
    create_control(
        L"BUTTON", L"Apply / refresh view",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::learning_library_refresh, parent);
    const auto library = create_control(
        L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
        ES_READONLY | WS_VSCROLL | WS_HSCROLL,
        0, id::learning_library, parent);
    static_cast<void>(::SendMessageW(
        library,
        EM_SETLIMITTEXT,
        static_cast<WPARAM>(16U * 1024U * 1024U),
        0));

    select_training_skill(training_target_);
    set_control_text(
        id::learning_piggyback_status,
        widen("Selected target: " + selected_training_skill() +
              " • automatic mode will remain on this target"));
    update_training_library();

}

void MainWindow::create_skills_page() {
    const auto parent = control(id::page_skills);
    create_control(
        L"BUTTON", L"Bootstrap local core", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::skills_bootstrap, parent);
    create_control(
        L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::skills_refresh, parent);
    create_control(
        L"STATIC", L"Skill ID",
        WS_CHILD | WS_VISIBLE, 0, id::skills_skill_label, parent);
    create_control(
        L"EDIT", L"cpp23-debugging", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, id::skills_skill, parent);
    create_control(
        L"BUTTON", L"Queue LoRA update", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::skills_queue, parent);
    create_control(
        L"BUTTON", L"Rollback active skill", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::skills_rollback, parent);
    create_control(
        L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
        WS_EX_CLIENTEDGE, id::skills_report, parent);
}

void MainWindow::create_long_horizon_page() {
    const auto parent = control(id::page_long_horizon);
    create_control(L"STATIC", L"Repository", WS_CHILD | WS_VISIBLE, 0, id::horizon_repository_label, parent);
    create_control(
        L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, id::horizon_repository, parent);
    create_control(
        L"BUTTON", L"Browse…", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::horizon_browse, parent);
    create_control(L"STATIC", L"Coding task", WS_CHILD | WS_VISIBLE, 0, id::horizon_task_label, parent);
    create_control(
        L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        WS_EX_CLIENTEDGE, id::horizon_task, parent);
    create_control(L"STATIC", L"Build command", WS_CHILD | WS_VISIBLE, 0, id::horizon_build_label, parent);
    create_control(
        L"EDIT", LR"(cmake --build out\build\windows-vs2022 --config Release)",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, id::horizon_build, parent);
    create_control(L"STATIC", L"Test command", WS_CHILD | WS_VISIBLE, 0, id::horizon_test_label, parent);
    create_control(
        L"EDIT", LR"(ctest --test-dir out\build\windows-vs2022 -C Release --output-on-failure)",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, id::horizon_test, parent);
    create_control(L"STATIC", L"Maximum loops", WS_CHILD | WS_VISIBLE, 0, id::horizon_iterations_label, parent);
    create_control(
        L"EDIT", L"8", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        WS_EX_CLIENTEDGE, id::horizon_iterations, parent);
    create_control(
        L"BUTTON", L"Require clean Git worktree", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        BS_AUTOCHECKBOX, 0, id::horizon_clean, parent);
    set_checked(id::horizon_clean, true);
    create_control(
        L"BUTTON", L"Start / resume run", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, id::horizon_start, parent);
    create_control(
        L"BUTTON", L"Cancel + rollback", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::horizon_cancel, parent);
    create_control(
        L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
        WS_EX_CLIENTEDGE, id::horizon_report, parent);
}

void MainWindow::create_logs_page() {
    const auto parent = control(id::page_logs);
    create_control(
        L"STATIC", L"Live application-data footprint (refreshes every 5 seconds)",
        WS_CHILD | WS_VISIBLE, 0, id::logs_storage_header, parent);
    create_control(
        L"STATIC", L"TRAINING DATA\r\nMeasuring…",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, id::chat_training_data_size, parent);
    create_control(
        L"STATIC", L"ACTIVE MODEL\r\nMeasuring…",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, id::chat_active_model_size, parent);
    create_control(
        L"STATIC", L"INSTALLED PACKS\r\nMeasuring…",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, id::chat_installed_packs_size, parent);
    create_control(
        L"STATIC", L"OUTPUTS / CACHE\r\nMeasuring…",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, id::chat_outputs_cache_size, parent);
    create_control(
        L"STATIC", L"TOTAL USAGE\r\nMeasuring…",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, id::chat_total_usage_size, parent);
    create_control(
        L"STATIC", L"FREE DISK\r\nMeasuring…",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, id::chat_free_disk_size, parent);

    create_control(
        L"BUTTON", L"Open active log", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::logs_open, parent);
    create_control(
        L"BUTTON", L"Open application data", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::logs_open_root, parent);
    create_control(
        L"BUTTON", L"Refresh sizes", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, id::logs_refresh_storage, parent);
    create_control(
        L"EDIT", L"Measuring storage in the background…", WS_CHILD | WS_VISIBLE | WS_BORDER |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
        WS_EX_CLIENTEDGE, id::logs_storage, parent);
    create_control(
        L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
        WS_EX_CLIENTEDGE, id::logs_view, parent);
}

void MainWindow::layout_controls() {
    if (window_ == nullptr) {
        return;
    }
    RECT client{};
    ::GetClientRect(window_, &client);
    const auto client_width = static_cast<int>(client.right - client.left);
    const auto client_height = static_cast<int>(client.bottom - client.top);
    const auto margin = scaled(10, dpi_);
    const auto status_height = scaled(24, dpi_);
    move_control(control(id::status), 0, client_height - status_height, client_width, status_height);
    const auto tabs = control(id::tabs);
    move_control(
        tabs,
        margin,
        margin,
        client_width - margin * 2,
        client_height - status_height - margin * 2);
    const auto tab_count = TabCtrl_GetItemCount(tabs);
    if (tab_count > 0) {
        RECT tab_client{};
        static_cast<void>(::GetClientRect(tabs, &tab_client));
        const auto tab_width = std::max(
            1,
            static_cast<int>(tab_client.right - tab_client.left) / tab_count);
        static_cast<void>(::SendMessageW(
            tabs,
            TCM_SETITEMSIZE,
            0,
            MAKELPARAM(tab_width, scaled(30, dpi_))));
    }

    RECT page_rect{};
    ::GetClientRect(tabs, &page_rect);
    static_cast<void>(TabCtrl_AdjustRect(tabs, FALSE, &page_rect));
    std::array<POINT, 2> page_points{{
        POINT{page_rect.left, page_rect.top},
        POINT{page_rect.right, page_rect.bottom},
    }};
    static_cast<void>(::MapWindowPoints(
        tabs,
        window_,
        page_points.data(),
        static_cast<UINT>(page_points.size())));

    const auto page_left = static_cast<int>(page_points[0].x);
    const auto page_top = static_cast<int>(page_points[0].y);
    const auto page_right = static_cast<int>(page_points[1].x);
    const auto page_bottom = static_cast<int>(page_points[1].y);
    const auto width = std::max(1, page_right - page_left);
    const auto height = std::max(1, page_bottom - page_top);
    for (const auto page : page_windows_) {
        static_cast<void>(::SetWindowPos(
            page,
            HWND_TOP,
            page_left,
            page_top,
            width,
            height,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS));
    }

    const auto gap = scaled(8, dpi_);
    const auto row = scaled(32, dpi_);
    const auto button = scaled(132, dpi_);

    // Primary workspace: connection, model, conversation, coding mode, and learning state
    // are intentionally visible without requiring another tab.
    auto workspace_x = 0;
    move_control(control(id::chat_mode_label), workspace_x, 0, scaled(40, dpi_), row);
    workspace_x += scaled(44, dpi_);
    move_control(control(id::chat_mode), workspace_x, 0, scaled(190, dpi_), row + scaled(160, dpi_));
    workspace_x += scaled(190, dpi_) + gap;
    move_control(control(id::chat_source_label), workspace_x, 0, scaled(62, dpi_), row);
    workspace_x += scaled(66, dpi_);
    move_control(control(id::chat_source), workspace_x, 0, scaled(218, dpi_), row + scaled(120, dpi_));
    workspace_x += scaled(218, dpi_) + gap;
    move_control(control(id::chat_model_label), workspace_x, 0, scaled(48, dpi_), row);
    workspace_x += scaled(52, dpi_);

    const auto workspace_button_width = scaled(108, dpi_);
    const auto workspace_settings_width = scaled(142, dpi_);
    const auto model_width = std::max(
        scaled(220, dpi_),
        width - workspace_x);
    move_control(control(id::chat_model), workspace_x, 0, model_width, row + scaled(220, dpi_));

    const auto workspace_status_y = row + gap;
    const auto workspace_buttons_width =
        workspace_button_width + workspace_settings_width + gap;
    move_control(
        control(id::chat_backend),
        0,
        workspace_status_y,
        std::max(1, width - workspace_buttons_width - gap),
        row);
    move_control(
        control(id::chat_discover),
        width - workspace_buttons_width,
        workspace_status_y,
        workspace_button_width,
        row);
    move_control(
        control(id::chat_open_models),
        width - workspace_settings_width,
        workspace_status_y,
        workspace_settings_width,
        row);

    const auto workspace_repository_y = workspace_status_y + row + gap;
    const auto show_workspace_repository = workspace_coding_mode();
    for (const auto identifier : {
             id::chat_repository_label,
             id::chat_repository,
             id::chat_repository_browse}) {
        ::ShowWindow(control(identifier), show_workspace_repository ? SW_SHOW : SW_HIDE);
    }
    move_control(
        control(id::chat_repository_label),
        0,
        workspace_repository_y,
        scaled(82, dpi_),
        row);
    move_control(
        control(id::chat_repository),
        scaled(86, dpi_),
        workspace_repository_y,
        width - scaled(206, dpi_) - gap,
        row);
    move_control(
        control(id::chat_repository_browse),
        width - scaled(112, dpi_),
        workspace_repository_y,
        scaled(112, dpi_),
        row);

    const auto workspace_content_top = show_workspace_repository
        ? workspace_repository_y + row + gap
        : workspace_repository_y;
    const auto sidebar_width = std::clamp(
        width * 30 / 100,
        scaled(300, dpi_),
        scaled(430, dpi_));
    const auto conversation_width = width - sidebar_width - gap;
    const auto sidebar_x = conversation_width + gap;
    const auto action_y = height - row;
    const auto prompt_height = std::clamp(
        height * 19 / 100,
        scaled(105, dpi_),
        scaled(175, dpi_));
    const auto prompt_y = action_y - gap - prompt_height;
    const auto prompt_label_y = prompt_y - row;
    const auto conversation_y = workspace_content_top + row;
    const auto conversation_height = std::max(
        scaled(120, dpi_),
        prompt_label_y - gap - conversation_y);

    move_control(
        control(id::chat_response_label),
        0,
        workspace_content_top,
        scaled(160, dpi_),
        row);
    move_control(
        control(id::chat_response),
        0,
        conversation_y,
        conversation_width,
        conversation_height);
    move_control(
        control(id::chat_prompt_label),
        0,
        prompt_label_y,
        scaled(160, dpi_),
        row);
    move_control(
        control(id::chat_prompt),
        0,
        prompt_y,
        conversation_width,
        prompt_height);

    auto action_x = 0;
    const auto move_workspace_action = [&](const int identifier, const int control_width) {
        move_control(control(identifier), action_x, action_y, control_width, row);
        action_x += control_width + gap;
    };
    move_workspace_action(id::chat_send, scaled(88, dpi_));
    move_workspace_action(id::chat_stop, scaled(72, dpi_));
    move_workspace_action(id::chat_accept, scaled(126, dpi_));
    move_workspace_action(id::chat_reject, scaled(82, dpi_));
    move_workspace_action(id::chat_clear, scaled(72, dpi_));
    move_workspace_action(id::chat_open_output, scaled(112, dpi_));
    move_control(
        control(id::chat_hint),
        action_x,
        action_y,
        std::max(1, conversation_width - action_x),
        row);

    move_control(
        control(id::chat_plan_label),
        sidebar_x,
        workspace_content_top,
        sidebar_width - scaled(126, dpi_) - gap,
        row);
    move_control(
        control(id::chat_route),
        width - scaled(126, dpi_),
        workspace_content_top,
        scaled(126, dpi_),
        row);
    const auto sidebar_available = height - workspace_content_top - row - gap;
    const auto sidebar_status_reserve =
        row + gap + scaled(64, dpi_) + gap + scaled(54, dpi_);
    const auto maximum_plan_height = std::max(
        scaled(120, dpi_),
        sidebar_available - sidebar_status_reserve);
    const auto plan_height = std::clamp(
        sidebar_available * 55 / 100,
        scaled(120, dpi_),
        maximum_plan_height);
    move_control(
        control(id::chat_plan),
        sidebar_x,
        workspace_content_top + row,
        sidebar_width,
        plan_height);
    const auto workspace_learning_y =
        workspace_content_top + row + plan_height + gap;
    move_control(
        control(id::chat_auto_learning),
        sidebar_x,
        workspace_learning_y,
        sidebar_width,
        row);
    move_control(
        control(id::chat_learning_status),
        sidebar_x,
        workspace_learning_y + row + gap,
        sidebar_width,
        scaled(64, dpi_));
    move_control(
        control(id::chat_mcp_status),
        sidebar_x,
        workspace_learning_y + row + gap + scaled(68, dpi_),
        sidebar_width,
        scaled(54, dpi_));

    // Dedicated coding workspace remains available as the second primary tab.
    auto coding_x = 0;
    move_control(control(id::coding_source_label), coding_x, 0, scaled(62, dpi_), row);
    coding_x += scaled(66, dpi_);
    move_control(control(id::coding_source), coding_x, 0, scaled(218, dpi_), row + scaled(120, dpi_));
    coding_x += scaled(218, dpi_) + gap;
    move_control(control(id::coding_model_label), coding_x, 0, scaled(48, dpi_), row);
    coding_x += scaled(52, dpi_);
    const auto coding_discover_width = scaled(108, dpi_);
    const auto coding_settings_width = scaled(142, dpi_);
    const auto coding_model_width = std::max(
        scaled(220, dpi_),
        width - coding_x);
    move_control(
        control(id::coding_model),
        coding_x,
        0,
        coding_model_width,
        row + scaled(220, dpi_));
    const auto coding_status_y = row + gap;
    const auto coding_buttons_width =
        coding_discover_width + coding_settings_width + gap;
    move_control(
        control(id::coding_backend),
        0,
        coding_status_y,
        std::max(1, width - coding_buttons_width - gap),
        row);
    move_control(
        control(id::coding_discover),
        width - coding_buttons_width,
        coding_status_y,
        coding_discover_width,
        row);
    move_control(
        control(id::coding_open_models),
        width - coding_settings_width,
        coding_status_y,
        coding_settings_width,
        row);

    const auto coding_repository_y = coding_status_y + row + gap;
    move_control(
        control(id::coding_repository_label),
        0,
        coding_repository_y,
        scaled(82, dpi_),
        row);
    move_control(
        control(id::coding_repository),
        scaled(86, dpi_),
        coding_repository_y,
        width - scaled(206, dpi_) - gap,
        row);
    move_control(
        control(id::coding_browse),
        width - scaled(112, dpi_),
        coding_repository_y,
        scaled(112, dpi_),
        row);

    const auto coding_label_y = coding_repository_y + row + gap;
    const auto coding_task_y = coding_label_y + row;
    const auto coding_upper_height = std::max(
        coding_task_y + scaled(210, dpi_),
        height * 47 / 100);
    const auto coding_left_width = width * 62 / 100;
    move_control(
        control(id::coding_task_label),
        0,
        coding_label_y,
        scaled(160, dpi_),
        row);
    move_control(
        control(id::coding_task),
        0,
        coding_task_y,
        coding_left_width - gap,
        coding_upper_height - coding_task_y - row - gap);

    auto coding_action_x = 0;
    const auto coding_actions_y = coding_upper_height - row;
    for (const auto identifier : {
             id::coding_ask,
             id::coding_run,
             id::coding_accept,
             id::coding_reject}) {
        const auto current_width =
            identifier == id::coding_run ? scaled(174, dpi_) :
            identifier == id::coding_ask ? scaled(150, dpi_) :
            identifier == id::coding_accept ? scaled(126, dpi_) :
            scaled(82, dpi_);
        move_control(
            control(identifier),
            coding_action_x,
            coding_actions_y,
            current_width,
            row);
        coding_action_x += current_width + gap;
    }

    move_control(
        control(id::coding_plan_label),
        coding_left_width,
        coding_label_y,
        scaled(150, dpi_),
        row);
    move_control(
        control(id::coding_plan),
        coding_left_width,
        coding_task_y,
        width - coding_left_width,
        coding_upper_height - coding_task_y);
    move_control(
        control(id::coding_response_label),
        0,
        coding_upper_height + gap,
        scaled(180, dpi_),
        row);
    move_control(
        control(id::coding_response),
        0,
        coding_upper_height + gap + row,
        width,
        height - coding_upper_height - gap - row);

    auto y = 0;
    move_control(control(id::models_backend_header), 0, y, width, row);
    y += row;
    move_control(control(id::models_automation_toggle), 0, y, width, row);
    y += row + gap;
    move_control(control(id::models_internal_status), 0, y, width, row);
    y += row + gap;
    move_control(control(id::models_progress), 0, y, width, row);
    y += row + gap;
    move_control(control(id::models_progress_detail), 0, y, width, row);
    y += row + gap * 2;

    move_control(control(id::models_local_header), 0, y, width, row);
    y += row;
    move_control(control(id::models_discover), 0, y, scaled(168, dpi_), row);
    move_control(
        control(id::models_discovery_status),
        scaled(176, dpi_),
        y,
        width - scaled(176, dpi_),
        row);
    y += row + gap * 2;

    move_control(control(id::models_advanced_header), 0, y, width, row);
    y += row + gap;

    constexpr std::array backup_model_controls{
        id::models_manual_endpoint_label,
        id::models_manual_endpoint,
        id::models_manual_model_label,
        id::models_manual_model,
        id::models_manual_protocol,
        id::models_manual_coding,
        id::models_manual_teacher,
        id::models_manual_add,
        id::models_manual_capabilities_label,
        id::models_manual_capabilities,
        id::models_mcp_header,
        id::models_mcp_label,
        id::models_mcp_endpoint,
        id::models_mcp_connect,
        id::models_mcp_status,
        id::models_internal_profile_label,
        id::models_internal_profile,
        id::models_internal_create,
        id::models_internal_bootstrap,
        id::models_python_label,
        id::models_python,
        id::models_browse,
        id::models_install,
        id::models_verify,
        id::models_start,
        id::models_stop,
        id::models_status,
        id::models_registry_header,
        id::models_editor,
        id::models_reload,
        id::models_save,
    };
    const auto show_backup_controls = checked(id::models_advanced_header);
    for (const auto identifier : backup_model_controls) {
        ::ShowWindow(control(identifier), show_backup_controls ? SW_SHOW : SW_HIDE);
    }

    if (show_backup_controls) {
        const auto models_label_width = scaled(110, dpi_);
        const auto protocol_width = scaled(190, dpi_);
        move_control(control(id::models_manual_endpoint_label), 0, y, models_label_width, row);
        move_control(
            control(id::models_manual_endpoint),
            models_label_width + gap,
            y,
            width - models_label_width - protocol_width - gap * 2,
            row);
        move_control(
            control(id::models_manual_protocol),
            width - protocol_width,
            y,
            protocol_width,
            row + scaled(96, dpi_));
        y += row + gap;

        const auto add_ai_width = scaled(105, dpi_);
        const auto learning_width = scaled(150, dpi_);
        const auto coding_width = scaled(120, dpi_);
        move_control(control(id::models_manual_model_label), 0, y, models_label_width, row);
        move_control(
            control(id::models_manual_model),
            models_label_width + gap,
            y,
            width - models_label_width - add_ai_width - learning_width - coding_width - gap * 4,
            row);
        move_control(
            control(id::models_manual_coding),
            width - add_ai_width - learning_width - coding_width - gap * 2,
            y,
            coding_width,
            row);
        move_control(
            control(id::models_manual_teacher),
            width - add_ai_width - learning_width - gap,
            y,
            learning_width,
            row);
        move_control(control(id::models_manual_add), width - add_ai_width, y, add_ai_width, row);
        y += row + gap;
        move_control(control(id::models_manual_capabilities_label), 0, y, models_label_width, row);
        move_control(
            control(id::models_manual_capabilities),
            models_label_width + gap,
            y,
            width - models_label_width - gap,
            row);
        y += row + gap * 2;

        move_control(control(id::models_mcp_header), 0, y, width, row);
        y += row;
        const auto tool_label_width = scaled(110, dpi_);
        const auto tool_button_width = scaled(140, dpi_);
        move_control(control(id::models_mcp_label), 0, y, tool_label_width, row);
        move_control(
            control(id::models_mcp_endpoint),
            tool_label_width + gap,
            y,
            width - tool_label_width - tool_button_width - gap * 2,
            row);
        move_control(
            control(id::models_mcp_connect),
            width - tool_button_width,
            y,
            tool_button_width,
            row);
        y += row + gap;
        move_control(control(id::models_mcp_status), 0, y, width, row);
        y += row + gap * 2;

        const auto internal_label_width = scaled(120, dpi_);
        const auto rebuild_width = scaled(170, dpi_);
        const auto continue_width = scaled(150, dpi_);
        move_control(control(id::models_internal_profile_label), 0, y, internal_label_width, row);
        move_control(
            control(id::models_internal_profile),
            internal_label_width + gap,
            y,
            std::max(
                scaled(220, dpi_),
                width - internal_label_width - rebuild_width - continue_width - gap * 4),
            row);
        move_control(
            control(id::models_internal_create),
            width - rebuild_width - continue_width - gap,
            y,
            rebuild_width,
            row);
        move_control(
            control(id::models_internal_bootstrap),
            width - continue_width,
            y,
            continue_width,
            row);
        y += row + gap;

        const auto setup_label_width = scaled(120, dpi_);
        const auto choose_width = scaled(130, dpi_);
        move_control(control(id::models_python_label), 0, y, setup_label_width, row);
        move_control(
            control(id::models_python),
            setup_label_width + gap,
            y,
            width - setup_label_width - choose_width - gap * 2,
            row);
        move_control(
            control(id::models_browse),
            width - choose_width,
            y,
            choose_width,
            row);
        y += row + gap;

        auto model_action_x = 0;
        for (const auto [identifier, action_width] : std::array{
                 std::pair{id::models_install, scaled(170, dpi_)},
                 std::pair{id::models_verify, scaled(120, dpi_)},
                 std::pair{id::models_start, scaled(150, dpi_)},
                 std::pair{id::models_stop, scaled(150, dpi_)}}) {
            move_control(control(identifier), model_action_x, y, action_width, row);
            model_action_x += action_width + gap;
        }
        y += row + gap;
        move_control(control(id::models_status), 0, y, width, row);
        y += row + gap * 2;

        move_control(control(id::models_registry_header), 0, y, width, row);
        y += row;
        const auto registry_buttons_y = height - row;
        move_control(
            control(id::models_editor),
            0,
            y,
            width,
            std::max(1, registry_buttons_y - gap - y));
        move_control(
            control(id::models_reload),
            0,
            registry_buttons_y,
            scaled(145, dpi_),
            row);
        move_control(
            control(id::models_save),
            scaled(153, dpi_),
            registry_buttons_y,
            scaled(145, dpi_),
            row);
    }
    auto learning_y = 0;
    move_control(control(id::learning_header), 0, learning_y, width, row);
    learning_y += row;

    const auto stop_width = scaled(150, dpi_);
    const auto pause_width = scaled(255, dpi_);
    move_control(control(id::learning_pause), 0, learning_y, pause_width, row);
    move_control(
        control(id::learning_auto),
        pause_width + gap,
        learning_y,
        std::max(1, width - pause_width - stop_width - gap * 2),
        row);
    move_control(
        control(id::learning_stop),
        std::max(0, width - stop_width),
        learning_y,
        stop_width,
        row);
    learning_y += row + gap;

    const auto target_label_width = scaled(112, dpi_);
    const auto custom_label_width = scaled(106, dpi_);
    const auto target_combo_width = std::clamp(
        width * 42 / 100,
        scaled(300, dpi_),
        scaled(460, dpi_));
    move_control(
        control(id::learning_skill_label),
        0,
        learning_y,
        target_label_width,
        row);
    move_control(
        control(id::learning_piggyback_skill),
        target_label_width + gap,
        learning_y,
        target_combo_width,
        row + scaled(190, dpi_));
    const auto custom_label_x = target_label_width + target_combo_width + gap * 2;
    move_control(
        control(id::learning_custom_skill_label),
        custom_label_x,
        learning_y,
        custom_label_width,
        row);
    move_control(
        control(id::learning_custom_skill),
        custom_label_x + custom_label_width + gap,
        learning_y,
        std::max(1, width - custom_label_x - custom_label_width - gap),
        row);
    learning_y += row + gap;

    const auto count_label_width = scaled(122, dpi_);
    const auto count_edit_width = scaled(58, dpi_);
    const auto generate_width = scaled(220, dpi_);
    const auto queue_width = scaled(230, dpi_);
    move_control(
        control(id::learning_count_label),
        0,
        learning_y,
        count_label_width,
        row);
    move_control(
        control(id::learning_piggyback_count),
        count_label_width + gap,
        learning_y,
        count_edit_width,
        row);
    move_control(
        control(id::learning_piggyback),
        count_label_width + count_edit_width + gap * 2,
        learning_y,
        generate_width,
        row);
    move_control(
        control(id::learning_build_custom),
        count_label_width + count_edit_width + generate_width + gap * 3,
        learning_y,
        std::min(
            queue_width,
            std::max(1, width - count_label_width - count_edit_width - generate_width - gap * 3)),
        row);
    learning_y += row + gap;

    const auto dataset_label_width = scaled(148, dpi_);
    const auto browse_width = scaled(92, dpi_);
    const auto import_width = scaled(168, dpi_);
    const auto template_width = scaled(150, dpi_);
    const auto dataset_edit_x = dataset_label_width + gap;
    const auto dataset_buttons_width = browse_width + import_width + template_width + gap * 3;
    const auto dataset_edit_width = std::max(
        1,
        width - dataset_edit_x - dataset_buttons_width);
    move_control(
        control(id::learning_dataset_label),
        0,
        learning_y,
        dataset_label_width,
        row);
    move_control(
        control(id::learning_dataset_path),
        dataset_edit_x,
        learning_y,
        dataset_edit_width,
        row);
    auto dataset_button_x = dataset_edit_x + dataset_edit_width + gap;
    move_control(
        control(id::learning_dataset_browse),
        dataset_button_x,
        learning_y,
        browse_width,
        row);
    dataset_button_x += browse_width + gap;
    move_control(
        control(id::learning_dataset_import),
        dataset_button_x,
        learning_y,
        import_width,
        row);
    dataset_button_x += import_width + gap;
    move_control(
        control(id::learning_dataset_template),
        dataset_button_x,
        learning_y,
        template_width,
        row);
    learning_y += row + gap;

    move_control(
        control(id::learning_piggyback_status),
        0,
        learning_y,
        width,
        row);
    learning_y += row + gap;
    const auto policy_height = scaled(124, dpi_);
    move_control(
        control(id::learning_policy),
        0,
        learning_y,
        width,
        policy_height);
    learning_y += policy_height + gap;

    const auto library_source_width = scaled(170, dpi_);
    const auto library_refresh_width = scaled(175, dpi_);
    if (width >= scaled(980, dpi_)) {
        const auto activity_width = std::max(scaled(320, dpi_), (width - gap) * 34 / 100);
        const auto library_x = activity_width + gap;
        const auto library_width = std::max(1, width - library_x);
        move_control(
            control(id::learning_activity_header),
            0,
            learning_y,
            activity_width,
            row);
        move_control(
            control(id::learning_library_header),
            library_x,
            learning_y,
            library_width,
            row);
        const auto content_y = learning_y + row + gap;
        move_control(
            control(id::learning_jobs),
            0,
            content_y,
            activity_width,
            std::max(1, height - content_y));
        const auto filter_width = std::max(
            1,
            library_width - library_source_width - library_refresh_width - gap * 2);
        move_control(
            control(id::learning_library_filter),
            library_x,
            content_y,
            filter_width,
            row);
        move_control(
            control(id::learning_library_source),
            library_x + filter_width + gap,
            content_y,
            library_source_width,
            row + scaled(160, dpi_));
        move_control(
            control(id::learning_library_refresh),
            library_x + filter_width + library_source_width + gap * 2,
            content_y,
            library_refresh_width,
            row);
        const auto library_y = content_y + row + gap;
        move_control(
            control(id::learning_library),
            library_x,
            library_y,
            library_width,
            std::max(1, height - library_y));
    } else {
        move_control(control(id::learning_activity_header), 0, learning_y, width, row);
        const auto activity_y = learning_y + row + gap;
        const auto remaining = std::max(1, height - activity_y);
        const auto activity_height = std::max(row * 3, remaining / 3);
        move_control(
            control(id::learning_jobs),
            0,
            activity_y,
            width,
            activity_height);
        const auto library_header_y = activity_y + activity_height + gap;
        move_control(
            control(id::learning_library_header),
            0,
            library_header_y,
            width,
            row);
        const auto filter_y = library_header_y + row + gap;
        const auto filter_width = std::max(
            1,
            width - library_source_width - library_refresh_width - gap * 2);
        move_control(
            control(id::learning_library_filter),
            0,
            filter_y,
            filter_width,
            row);
        move_control(
            control(id::learning_library_source),
            filter_width + gap,
            filter_y,
            library_source_width,
            row + scaled(160, dpi_));
        move_control(
            control(id::learning_library_refresh),
            filter_width + library_source_width + gap * 2,
            filter_y,
            library_refresh_width,
            row);
        const auto library_y = filter_y + row + gap;
        move_control(
            control(id::learning_library),
            0,
            library_y,
            width,
            std::max(1, height - library_y));
    }
    move_control(control(id::skills_bootstrap), 0, 0, scaled(180, dpi_), row);
    move_control(
        control(id::skills_refresh),
        scaled(188, dpi_),
        0,
        scaled(120, dpi_),
        row);
    const auto skills_actions_y = row + gap;
    move_control(
        control(id::skills_skill_label),
        0,
        skills_actions_y,
        scaled(62, dpi_),
        row);
    move_control(
        control(id::skills_skill),
        scaled(66, dpi_),
        skills_actions_y,
        scaled(250, dpi_),
        row);
    move_control(
        control(id::skills_queue),
        scaled(324, dpi_),
        skills_actions_y,
        scaled(160, dpi_),
        row);
    move_control(
        control(id::skills_rollback),
        scaled(492, dpi_),
        skills_actions_y,
        scaled(175, dpi_),
        row);
    const auto skills_report_y = skills_actions_y + row + gap;
    move_control(
        control(id::skills_report),
        0,
        skills_report_y,
        width,
        height - skills_report_y);

    move_control(control(id::horizon_repository_label), 0, 0, scaled(130, dpi_), row);
    move_control(control(id::horizon_repository), scaled(135, dpi_), 0, width - scaled(255, dpi_), row);
    move_control(control(id::horizon_browse), width - scaled(112, dpi_), 0, scaled(112, dpi_), row);
    move_control(control(id::horizon_task_label), 0, row + gap, scaled(130, dpi_), row);
    const auto horizon_task_y = row * 2 + gap;
    const auto horizon_task_height = std::max(scaled(120, dpi_), height * 24 / 100);
    move_control(control(id::horizon_task), 0, horizon_task_y, width, horizon_task_height);
    const auto horizon_build_y = horizon_task_y + horizon_task_height + gap;
    move_control(control(id::horizon_build_label), 0, horizon_build_y, scaled(130, dpi_), row);
    move_control(control(id::horizon_build), scaled(135, dpi_), horizon_build_y, width - scaled(135, dpi_), row);
    const auto horizon_test_y = horizon_build_y + row + gap;
    move_control(control(id::horizon_test_label), 0, horizon_test_y, scaled(130, dpi_), row);
    move_control(control(id::horizon_test), scaled(135, dpi_), horizon_test_y, width - scaled(135, dpi_), row);
    const auto horizon_options_y = horizon_test_y + row + gap;
    move_control(control(id::horizon_iterations_label), 0, horizon_options_y, scaled(115, dpi_), row);
    move_control(control(id::horizon_iterations), scaled(120, dpi_), horizon_options_y, scaled(70, dpi_), row);
    move_control(control(id::horizon_clean), scaled(205, dpi_), horizon_options_y, scaled(220, dpi_), row);
    move_control(control(id::horizon_start), scaled(440, dpi_), horizon_options_y, scaled(180, dpi_), row);
    move_control(control(id::horizon_cancel), scaled(628, dpi_), horizon_options_y, scaled(160, dpi_), row);
    const auto horizon_report_y = horizon_options_y + row + gap;
    move_control(control(id::horizon_report), 0, horizon_report_y, width, height - horizon_report_y);

    move_control(control(id::logs_open), 0, 0, button, row);
    move_control(control(id::logs_open_root), button + gap, 0, scaled(180, dpi_), row);
    move_control(control(id::logs_refresh_storage), button + gap + scaled(188, dpi_), 0, scaled(130, dpi_), row);
    move_control(control(id::logs_storage_header), 0, row + gap, width, row);

    const auto storage_card_height = scaled(56, dpi_);
    const auto storage_card_width = (width - gap * 2) / 3;
    const auto storage_cards_first_y = row * 2 + gap * 2;
    const auto storage_cards_second_y = storage_cards_first_y + storage_card_height + gap;
    const auto place_storage_card = [&](const int identifier, const int column, const int y) {
        const auto x = column * (storage_card_width + gap);
        const auto card_width = column == 2 ? width - x : storage_card_width;
        move_control(control(identifier), x, y, card_width, storage_card_height);
    };
    place_storage_card(id::chat_active_model_size, 0, storage_cards_first_y);
    place_storage_card(id::chat_training_data_size, 1, storage_cards_first_y);
    place_storage_card(id::chat_installed_packs_size, 2, storage_cards_first_y);
    place_storage_card(id::chat_outputs_cache_size, 0, storage_cards_second_y);
    place_storage_card(id::chat_total_usage_size, 1, storage_cards_second_y);
    place_storage_card(id::chat_free_disk_size, 2, storage_cards_second_y);

    const auto storage_y = storage_cards_second_y + storage_card_height + gap;
    const auto storage_height = std::max(scaled(150, dpi_), (height - storage_y) * 55 / 100);
    move_control(control(id::logs_storage), 0, storage_y, width, storage_height);
    const auto log_y = storage_y + storage_height + gap;
    move_control(control(id::logs_view), 0, log_y, width, height - log_y);

    show_page(page_);
}

void MainWindow::show_page(const Page page) {
    page_ = page;
    const auto selected = static_cast<std::size_t>(page);
    HWND visible_page = nullptr;

    for (std::size_t index = 0; index < page_windows_.size(); ++index) {
        const auto host = page_windows_[index];
        if (index == selected) {
            visible_page = host;
            continue;
        }
        ::ShowWindow(host, SW_HIDE);
    }

    if (visible_page != nullptr) {
        static_cast<void>(::SetWindowPos(
            visible_page,
            HWND_TOP,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                SWP_NOOWNERZORDER | SWP_SHOWWINDOW));
        static_cast<void>(::RedrawWindow(
            visible_page,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW));
    }
}

void MainWindow::select_page_tab(const Page page) {
    const auto tabs = control(id::tabs);
    const auto count = TabCtrl_GetItemCount(tabs);
    for (auto index = 0; index < count; ++index) {
        TCITEMW item{};
        item.mask = TCIF_PARAM;
        if (TabCtrl_GetItem(tabs, index, &item) != FALSE &&
            item.lParam == static_cast<LPARAM>(page)) {
            static_cast<void>(TabCtrl_SetCurSel(tabs, index));
            return;
        }
    }
}

void MainWindow::handle_command(const int identifier) {
    switch (identifier) {
    case id::chat_route:
        preview_route();
        break;
    case id::chat_send:
        send_prompt();
        break;
    case id::chat_stop:
        stop_active_request();
        break;
    case id::chat_clear:
        clear_workspace();
        break;
    case id::chat_discover:
    case id::coding_discover:
        discover_local_ai();
        break;
    case id::chat_open_models:
    case id::coding_open_models:
        open_local_ai_page();
        break;
    case id::chat_repository_browse: {
        const auto selected = choose_directory(L"Select a repository for this task");
        if (selected.has_value()) {
            set_control_text(id::chat_repository, selected->wstring());
        }
        break;
    }
    case id::chat_auto_learning:
        automatic_training_enabled_ = checked(id::chat_auto_learning);
        set_checked(id::learning_auto, automatic_training_enabled_);
        try {
            write_text(
                paths_.root / "config" / "automatic-training.txt",
                automatic_training_enabled_ ? "on\n" : "off\n");
        } catch (...) {
        }
        if (!automatic_training_enabled_) {
            piggyback_operation_.cancel();
            set_status(L"Automatic verified-example generation disabled and saved");
        } else {
            next_piggyback_at_ =
                std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }
        update_learning_report();
        break;
    case id::chat_accept:
    case id::coding_accept:
        accept_last_response();
        break;
    case id::chat_reject:
    case id::coding_reject:
        reject_last_response();
        break;
    case id::coding_browse:
        browse_coding_repository();
        break;
    case id::coding_ask:
        send_coding_prompt();
        break;
    case id::coding_run:
        run_coding_agent();
        break;
    case id::models_discover:
        discover_local_ai();
        break;
    case id::models_manual_add:
        add_manual_local_ai();
        break;
    case id::models_mcp_connect:
        connect_mcp();
        break;
    case id::models_automation_toggle:
        automatic_lifecycle_enabled_ = checked(id::models_automation_toggle);
        lifecycle_failures_ = 0U;
        lifecycle_last_error_.clear();
        lifecycle_retry_at_ = std::chrono::steady_clock::now();
        try {
            write_text(
                paths_.root / "config" / "automatic-lifecycle.txt",
                automatic_lifecycle_enabled_ ? "on\n" : "off\n");
        } catch (...) {
        }
        set_status(automatic_lifecycle_enabled_
            ? L"ThreadSpire will set up and keep the built-in AI ready automatically"
            : L"Automatic built-in AI setup is paused; the current task is unchanged");
        break;
    case id::models_advanced_header:
        set_control_text(
            id::models_advanced_header,
            checked(id::models_advanced_header)
                ? L"Hide backup and repair options"
                : L"Backup and repair options");
        layout_controls();
        set_status(checked(id::models_advanced_header)
            ? L"Backup and repair options shown"
            : L"Backup options hidden; automatic setup remains active");
        break;
    case id::models_internal_create:
        create_internal_model(false);
        break;
    case id::models_internal_bootstrap:
        create_internal_model(true);
        break;
    case id::models_browse:
        browse_python();
        break;
    case id::models_install:
        install_backend();
        break;
    case id::models_verify:
        verify_backend();
        break;
    case id::models_start:
        start_backend();
        break;
    case id::models_stop:
        stop_backend();
        break;
    case id::models_reload:
        load_models_editor();
        break;
    case id::models_save:
        save_models_editor();
        break;
    case id::learning_pause:
        learning_service_->pause(checked(id::learning_pause));
        if (checked(id::learning_pause)) {
            piggyback_operation_.cancel();
        } else if (checked(id::learning_auto)) {
            next_piggyback_at_ =
                std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }
        update_learning_report();
        break;
    case id::learning_auto:
        automatic_training_enabled_ = checked(id::learning_auto);
        set_checked(id::chat_auto_learning, automatic_training_enabled_);
        try {
            write_text(
                paths_.root / "config" / "automatic-training.txt",
                automatic_training_enabled_ ? "on\n" : "off\n");
        } catch (...) {
        }
        if (!automatic_training_enabled_) {
            piggyback_operation_.cancel();
            set_status(L"Automatic verified-example generation disabled and saved");
        } else {
            set_status(L"Automatic verified-example generation enabled for the selected target");
            next_piggyback_at_ =
                std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }
        update_learning_report();
        break;
    case id::learning_piggyback:
        generate_piggyback(false);
        break;
    case id::learning_build_custom: {
        const auto skill = selected_training_skill();
        const auto evidence =
            learning_service_->memory_store().training_count_for_skill(skill);
        if (evidence == 0U) {
            set_status(widen(
                "No trainable evidence exists for " + skill +
                ". Generate verified examples or import trusted JSONL first."));
            break;
        }
        learning_service_->request_skill_update(skill);
        set_status(widen(
            "Queued shadow weight candidate for " + skill + " from " +
            std::to_string(evidence) + " verified example(s)"));
        update_learning_report();
        break;
    }
    case id::learning_dataset_browse:
        browse_custom_training_data();
        break;
    case id::learning_dataset_import:
        import_custom_training_data();
        break;
    case id::learning_dataset_template:
        open_path(paths_.custom_training_template);
        break;
    case id::learning_library_refresh:
        update_training_library();
        set_status(L"Training Library refreshed");
        break;
    case id::learning_stop:
        stop_training_pass();
        break;
    case id::skills_bootstrap:
        bootstrap_core_skills();
        break;
    case id::skills_refresh:
        update_skill_report();
        break;
    case id::skills_queue:
        queue_skill_update();
        break;
    case id::skills_rollback:
        rollback_skill();
        break;
    case id::horizon_browse:
        browse_repository();
        break;
    case id::horizon_start:
        long_horizon_from_coding_ = false;
        start_long_horizon();
        break;
    case id::horizon_cancel:
        cancel_long_horizon();
        break;
    case id::logs_open:
        open_path(active_log_);
        break;
    case id::logs_open_root:
        open_path(paths_.root);
        break;
    case id::logs_refresh_storage:
        update_storage_report(true);
        break;
    case id::chat_open_output: {
        std::error_code error;
        std::filesystem::create_directories(paths_.root / "outputs", error);
        open_path(paths_.root / "outputs");
        break;
    }
    default:
        break;
    }
}


void MainWindow::update_workspace_mode() {
    const auto task = workspace_task();
    const auto label = [&]() -> const wchar_t* {
        switch (task) {
        case WorkspaceTask::ask: return L"Prompt";
        case WorkspaceTask::code_help: return L"Code question or requested change";
        case WorkspaceTask::repository: return L"Repository task";
        case WorkspaceTask::image: return L"Image prompt";
        case WorkspaceTask::speech: return L"Text to speak";
        case WorkspaceTask::tool: return L"Tool task";
        }
        return L"Prompt";
    }();
    set_control_text(
        id::chat_prompt_label,
        label);
    set_control_text(
        id::chat_response_label,
        task == WorkspaceTask::repository ? L"Task and verified run result" : L"Task output");
    set_status(L"Task changed. ThreadSpire updated the available AI choices.");
    refresh_model_selectors();
    layout_controls();
}

void MainWindow::refresh_model_selectors() {
    models::ModelRegistry snapshot;
    {
        std::lock_guard lock(model_mutex_);
        snapshot = model_registry_;
    }

    const auto custom_host_running = backend_server_.running();
    const auto bootstrap_training =
        lifecycle_action_ == LifecycleAction::bootstrap_model &&
        backend_operation_.running();
    std::vector<models::ModelProfile> available;
    for (const auto& model : snapshot.models()) {
        if (!model.enabled || !source_accepts(model) ||
            !model_supports_workspace_task(model)) {
            continue;
        }
        if (inference_source_ == InferenceSource::automatic &&
            models::is_internal_model(model) && !custom_host_running) {
            continue;
        }
        available.push_back(model);
    }
    std::ranges::sort(available, [](const auto& left, const auto& right) {
        const auto left_score =
            left.priority * 1000.0 + left.quality * left.reliability * 100.0 +
            left.tokens_per_second;
        const auto right_score =
            right.priority * 1000.0 + right.quality * right.reliability * 100.0 +
            right.tokens_per_second;
        return left_score > right_score;
    });

    model_selector_ids_.clear();
    model_selector_ids_.emplace_back();

    const auto automatic_label = [&]() -> const wchar_t* {
        switch (inference_source_) {
        case InferenceSource::local_bridge:
            return available.empty()
                ? L"Choose a connected AI — waiting"
                : L"Choose a connected AI for me";
        case InferenceSource::custom_model:
            if (custom_host_running) {
                return L"Choose a built-in AI for me";
            }
            return bootstrap_training
                ? L"Choose a built-in AI — still learning"
                : L"Choose a built-in AI — unavailable";
        case InferenceSource::automatic:
            return available.empty()
                ? L"Choose any AI — waiting"
                : L"Choose any ready AI for me";
        }
        return L"Automatic";
    }();

    const auto reset_combo = [&](const int identifier) {
        const auto combo = control(identifier);
        if (combo == nullptr) {
            return;
        }
        static_cast<void>(::SendMessageW(combo, CB_RESETCONTENT, 0, 0));
        static_cast<void>(::SendMessageW(
            combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(automatic_label)));
    };
    reset_combo(id::chat_model);
    reset_combo(id::coding_model);

    for (const auto& model : available) {
        model_selector_ids_.push_back(model.id);
        std::wstring label;
        if (inference_source_ == InferenceSource::automatic) {
            label += models::is_internal_model(model)
                ? L"Built-in • "
                : L"Other app • ";
        }
        label += widen(model.display_name);
        if (models::is_internal_model(model) && !custom_host_running) {
            label += bootstrap_training
                ? L" • learning automatically"
                : L" • not available";
        }
        for (const auto identifier : {id::chat_model, id::coding_model}) {
            static_cast<void>(::SendMessageW(
                control(identifier),
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(label.c_str())));
        }
    }

    auto selection = 0;
    auto cleared_incompatible_preference = false;
    if (!preferred_model_id_.empty()) {
        const auto iterator = std::ranges::find(model_selector_ids_, preferred_model_id_);
        if (iterator != model_selector_ids_.end()) {
            selection = static_cast<int>(std::distance(model_selector_ids_.begin(), iterator));
        } else {
            preferred_model_id_.clear();
            cleared_incompatible_preference = true;
        }
    }
    if (cleared_incompatible_preference) {
        try {
            write_text(
                paths_.root / "config" / "preferred-model.txt",
                std::string_view{});
        } catch (...) {
        }
    }
    static_cast<void>(::SendMessageW(control(id::chat_model), CB_SETCURSEL, selection, 0));
    static_cast<void>(::SendMessageW(control(id::coding_model), CB_SETCURSEL, selection, 0));

    if (workspace_transcript_.empty() && !available.empty()) {
        std::wostringstream ready;
        if (inference_source_ == InferenceSource::custom_model) {
            ready << L"BUILT-IN AI\r\n";
            if (custom_host_running) {
                ready << L"Ready. Type a message below.";
            } else if (bootstrap_training) {
                ready << L"Learning is running automatically. ThreadSpire will start it when ready.";
            } else {
                ready << L"Required built-in AI files are not installed. Chat will use another ready AI.";
            }
        } else if (inference_source_ == InferenceSource::local_bridge) {
            ready << L"OTHER AI APP\r\n"
                  << available.size() << L" ready AI choice"
                  << (available.size() == 1U ? L"" : L"s")
                  << L" found. Type a message below.";
        } else {
            ready << L"AUTOMATIC\r\n"
                  << available.size() << L" ready AI choice"
                  << (available.size() == 1U ? L"" : L"s")
                  << L" found. ThreadSpire will use the fastest one that fits the task.";
        }
        set_control_text(id::chat_response, ready.str());
    }
}

void MainWindow::update_inference_source(const int source_identifier) {
    const auto selection = static_cast<int>(::SendMessageW(
        control(source_identifier),
        CB_GETCURSEL,
        0,
        0));
    inference_source_ = static_cast<InferenceSource>(std::clamp(selection, 0, 2));

    for (const auto identifier : {id::chat_source, id::coding_source}) {
        static_cast<void>(::SendMessageW(
            control(identifier),
            CB_SETCURSEL,
            static_cast<WPARAM>(inference_source_),
            0));
    }

    if (!preferred_model_id_.empty()) {
        const auto snapshot = runtime_model_snapshot();
        const auto* selected = snapshot.find(preferred_model_id_);
        if (selected == nullptr || !source_accepts(*selected)) {
            preferred_model_id_.clear();
        }
    }

    const auto source_name = [&]() -> std::string_view {
        switch (inference_source_) {
        case InferenceSource::automatic:
            return "automatic";
        case InferenceSource::local_bridge:
            return "bridge";
        case InferenceSource::custom_model:
            return "custom";
        }
        return "bridge";
    }();
    try {
        write_text(paths_.root / "config" / "preferred-source.txt", source_name);
        write_text(paths_.root / "config" / "preferred-model.txt", preferred_model_id_);
    } catch (...) {
    }

    refresh_model_selectors();
    const auto snapshot = runtime_model_snapshot();
    set_status(runtime_summary(snapshot, backend_server_.running()));
}

void MainWindow::update_model_selection(const int source_identifier) {
    const auto selection = static_cast<int>(::SendMessageW(
        control(source_identifier),
        CB_GETCURSEL,
        0,
        0));
    if (selection >= 0 &&
        static_cast<std::size_t>(selection) < model_selector_ids_.size()) {
        preferred_model_id_ = model_selector_ids_[static_cast<std::size_t>(selection)];
    } else {
        preferred_model_id_.clear();
    }

    const auto other = source_identifier == id::chat_model
        ? id::coding_model
        : id::chat_model;
    static_cast<void>(::SendMessageW(control(other), CB_SETCURSEL, selection, 0));

    try {
        write_text(
            paths_.root / "config" / "preferred-model.txt",
            preferred_model_id_);
    } catch (...) {
    }

    if (preferred_model_id_.empty()) {
        set_status(inference_source_ == InferenceSource::custom_model
            ? L"Built-in AI only; another AI will not be used"
            : inference_source_ == InferenceSource::local_bridge
                ? L"Choose a connected AI automatically"
                : L"Choose any ready AI automatically");
        return;
    }
    const auto snapshot = runtime_model_snapshot();
    const auto* selected = snapshot.find(preferred_model_id_);
    if (selected == nullptr) {
        set_status(L"The selected model is no longer available; select Discover");
        return;
    }
    const auto prefix = models::is_internal_model(*selected)
        ? std::string{"Locked to built-in AI: "}
        : std::string{"Locked to connected AI: "};
    set_status(widen(prefix + selected->display_name + ". No other AI will be used."));
}

void MainWindow::stop_active_request() {
    if (chat_operation_.running()) {
        chat_operation_.cancel();
        set_status(L"Stopping the current local model request…");
        return;
    }
    if (media_operation_.running()) {
        media_operation_.cancel();
        set_status(L"Stopping media generation…");
        return;
    }
    if (long_horizon_operation_.running()) {
        long_horizon_operation_.cancel();
        set_status(L"Stopping the repository task at its next safe boundary…");
        return;
    }
    {
        set_status(L"No foreground model request is running");
    }
}

void MainWindow::yield_automatic_bootstrap_to_foreground() {
    if (lifecycle_action_ != LifecycleAction::bootstrap_model ||
        !backend_operation_.running()) {
        return;
    }
    // A foreground request owns the machine now. Cancellation preserves the latest
    // atomic checkpoint; the automatic coordinator resumes only after the request.
    lifecycle_retry_at_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    backend_operation_.cancel();
    set_control_text(
        id::chat_learning_status,
        L"Saving built-in AI learning progress and yielding to your request…");
}

void MainWindow::clear_workspace() {
    if (chat_operation_.running()) {
        set_status(L"Stop the current response before clearing the workspace");
        return;
    }
    workspace_transcript_.clear();
    conversation_history_.clear();
    last_prompt_.clear();
    last_response_.clear();
    last_model_id_.clear();
    last_models_used_.clear();
    last_tool_events_.clear();
    pending_transcript_heading_offset_ = std::string::npos;
    pending_transcript_heading_length_ = 0;
    set_control_text(id::chat_prompt, L"");
    set_control_text(id::chat_plan, L"Waiting for a message.");
    set_control_text(
        id::chat_response,
        L"Task cleared. Choose a capability, describe the outcome, and select Run task.");
    set_status(L"Task cleared");
}

void MainWindow::open_local_ai_page() {
    show_page(Page::models);
    select_page_tab(Page::models);
    set_status(L"Local AI connections and MCP settings");
}

MainWindow::WorkspaceTask MainWindow::workspace_task() const noexcept {
    const auto selection = static_cast<int>(::SendMessageW(
        control(id::chat_mode), CB_GETCURSEL, 0, 0));
    return static_cast<WorkspaceTask>(std::clamp(selection, 0, 5));
}

bool MainWindow::workspace_coding_mode() const {
    const auto task = workspace_task();
    return task == WorkspaceTask::code_help || task == WorkspaceTask::repository;
}

bool MainWindow::model_supports_workspace_task(const models::ModelProfile& model) const noexcept {
    switch (workspace_task()) {
    case WorkspaceTask::image:
        return models::has_capability(model, "image") || models::has_capability(model, "image-generation");
    case WorkspaceTask::speech:
        return models::has_capability(model, "audio") || models::has_capability(model, "speech") ||
               models::has_capability(model, "audio-generation");
    case WorkspaceTask::repository:
    case WorkspaceTask::code_help:
        return model.coding_agent || models::has_capability(model, "code") ||
               models::has_capability(model, "cpp23") || models::has_capability(model, "repository");
    case WorkspaceTask::tool:
        return model.supports_tools || models::has_capability(model, "tools");
    case WorkspaceTask::ask:
        return models::has_capability(model, "chat") || models::has_capability(model, "english");
    }
    return false;
}

std::optional<std::string> MainWindow::selected_model_id() const {
    return preferred_model_id_.empty()
        ? std::nullopt
        : std::optional<std::string>(preferred_model_id_);
}

bool MainWindow::source_accepts(const models::ModelProfile& model) const noexcept {
    const auto custom = models::is_internal_model(model);
    switch (inference_source_) {
    case InferenceSource::automatic:
        return true;
    case InferenceSource::local_bridge:
        return !custom;
    case InferenceSource::custom_model:
        return custom;
    }
    return false;
}

void MainWindow::apply_source_filter(models::ModelRegistry& registry) const {
    for (auto& model : registry.models()) {
        model.enabled = model.enabled && source_accepts(model);
    }
}

std::wstring MainWindow::runtime_summary(
    const models::ModelRegistry& registry,
    const bool custom_host_running) const {
    std::size_t connected_ready = 0;
    for (const auto& model : registry.models()) {
        if (model.enabled && !models::is_internal_model(model)) {
            ++connected_ready;
        }
    }

    const auto setup_running =
        lifecycle_action_ == LifecycleAction::bootstrap_model &&
        backend_operation_.running();
    const auto host_starting =
        lifecycle_action_ == LifecycleAction::start_host &&
        backend_operation_.running();
    const auto internal_state = runtime::inspect_internal_model_state(paths_);
    const auto progress = runtime::read_internal_model_progress(paths_);
    std::wstring built_in_status;
    if (custom_host_running) {
        built_in_status = L"READY";
    } else if (setup_running) {
        const auto completed = progress.total_steps > 0U
            ? progress.total_steps
            : progress.prior_total_steps + progress.step;
        const auto target = progress.bootstrap_target_steps > 0U
            ? progress.bootstrap_target_steps
            : runtime::default_bootstrap_target(internal_state);
        std::wostringstream value;
        value << L"NOT READY — learning automatically; training copy is not used";
        if (target > 0U) {
            value << L" (" << completed << L"/" << target << L")";
        }
        built_in_status = value.str();
    } else if (host_starting) {
        built_in_status = L"NOT READY — starting automatically";
    } else if (!automatic_lifecycle_enabled_) {
        built_in_status = L"NOT READY — automatic setup paused";
    } else if (automatic_learning_limit_reached(paths_, internal_state)) {
        built_in_status = L"NOT READY — failed quality checks; rejected copy was never used";
    } else if (!runtime::backend_files_present(paths_)) {
        built_in_status = L"NOT INSTALLED — required files are missing";
    } else if (!internal_state.exists) {
        built_in_status = L"NOT READY — automatic setup starting";
    } else if (!internal_state.manifest_valid) {
        built_in_status = L"NOT READY — recovering automatically";
    } else if (!internal_state.bootstrap_complete) {
        built_in_status = L"NOT READY — learning will resume automatically";
    } else {
        built_in_status = L"READY — starting automatically";
    }

    std::wostringstream stream;
    switch (inference_source_) {
    case InferenceSource::local_bridge:
        stream << L"Using: another AI app";
        break;
    case InferenceSource::custom_model:
        stream << (custom_host_running
            ? L"Using: built-in AI only"
            : L"Using: built-in AI only • not ready");
        break;
    case InferenceSource::automatic:
        stream << L"Using: fastest ready AI";
        break;
    }
    stream << L" • Connected AI: ";
    if (connected_ready == 0U) {
        stream << L"not found";
    } else {
        stream << L"ready (" << connected_ready << L")";
    }
    stream << L" • Built-in AI: " << built_in_status;

    if (const auto selected = selected_model_id(); selected.has_value()) {
        if (const auto* profile = registry.find(*selected); profile != nullptr) {
            stream << L" • Chosen: " << widen(profile->display_name);
        }
    }
    return stream.str();
}
std::string MainWindow::requested_runtime_heading() const {
    switch (inference_source_) {
    case InferenceSource::local_bridge:
        return "ASSISTANT — CONNECTED LOCAL LLM";
    case InferenceSource::custom_model:
        return "ASSISTANT — THREADSPIRE INTERNAL MODEL";
    case InferenceSource::automatic:
        return "ASSISTANT — AUTOMATIC AI SOURCE";
    }
    return "ASSISTANT";
}

std::string MainWindow::actual_runtime_heading(const std::string_view model_id) const {
    if (model_id == "verified-memory") {
        return "ASSISTANT — THREADSPIRE VERIFIED MEMORY";
    }
    const auto snapshot = runtime_model_snapshot();
    const auto* model = snapshot.find(model_id);
    if (model == nullptr) {
        return requested_runtime_heading();
    }
    std::string result = models::is_internal_model(*model)
        ? "ASSISTANT — THREADSPIRE INTERNAL MODEL — "
        : "ASSISTANT — CONNECTED LOCAL LLM — ";
    result += model->display_name;
    return result;
}

bool MainWindow::is_workspace_surface(const RequestSurface surface) const noexcept {
    return surface == RequestSurface::assistant ||
           surface == RequestSurface::workspace_coding;
}

bool MainWindow::is_coding_surface(const RequestSurface surface) const noexcept {
    return surface == RequestSurface::workspace_coding ||
           surface == RequestSurface::coding;
}

models::ModelRegistry MainWindow::runtime_model_snapshot() const {
    std::lock_guard lock(model_mutex_);
    auto snapshot = model_registry_;
    if (!backend_server_.running()) {
        for (auto& model : snapshot.models()) {
            if (models::is_internal_model(model)) {
                model.enabled = false;
            }
        }
    }
    return snapshot;
}

std::optional<models::ModelProfile> MainWindow::best_coding_model() const {
    auto snapshot = runtime_model_snapshot();
    apply_source_filter(snapshot);
    if (const auto selected = selected_model_id(); selected.has_value()) {
        if (const auto* model = snapshot.find(*selected);
            model != nullptr &&
            model->enabled &&
            (model->coding_agent || models::has_capability(*model, "cpp23"))) {
            return *model;
        }
    }
    const models::ModelProfile* best = nullptr;
    for (const auto& model : snapshot.models()) {
        if (!model.enabled ||
            (!model.coding_agent && !models::has_capability(model, "cpp23"))) {
            continue;
        }
        if (best == nullptr || coding_score(model) > coding_score(*best)) {
            best = &model;
        }
    }
    return best == nullptr ? std::nullopt : std::optional<models::ModelProfile>(*best);
}

void MainWindow::preview_route() {
    const auto prompt = narrow(control_text(id::chat_prompt));
    if (prompt.empty()) {
        set_status(L"Enter a message first");
        return;
    }

    auto snapshot = runtime_model_snapshot();
    apply_source_filter(snapshot);
    if (const auto selected = selected_model_id(); selected.has_value()) {
        for (auto& model : snapshot.models()) {
            model.enabled = model.id == *selected && model.enabled;
        }
    }

    routing::TaskAnalyzer analyzer;
    auto analysis = analyzer.analyze(prompt);
    if (workspace_coding_mode()) {
        analysis.kind = routing::TaskKind::code_generation;
        analysis.tier = std::max(
            analysis.tier,
            routing::ComplexityTier::fast);
        analysis.risk = std::max(analysis.risk, 0.22);
        if (std::ranges::find(analysis.required_capabilities, "cpp23") ==
            analysis.required_capabilities.end()) {
            analysis.required_capabilities.push_back("cpp23");
        }
    }

    if (workspace_task() == WorkspaceTask::tool) {
        analysis.kind = routing::TaskKind::tool_action;
        analysis.needs_tools = true;
        if (std::ranges::find(analysis.required_capabilities, "tools") ==
            analysis.required_capabilities.end()) {
            analysis.required_capabilities.push_back("tools");
        }
    }
    routing::AdaptiveRouter router;
    const auto plan = router.plan(analysis, snapshot);
    set_control_text(id::chat_plan_label, L"How it would run");
    set_control_text(id::chat_plan, format_plan(plan, &snapshot));
    set_status(plan.stages.empty()
        ? L"No responding model in the selected AI source can handle this message"
        : L"Choices checked; no AI was run");
}

void MainWindow::send_prompt() {
    switch (workspace_task()) {
    case WorkspaceTask::repository:
        run_coding_agent();
        return;
    case WorkspaceTask::image:
        run_media_request(backend::MediaKind::image);
        return;
    case WorkspaceTask::speech:
        run_media_request(backend::MediaKind::speech);
        return;
    case WorkspaceTask::code_help:
        send_request(RequestSurface::workspace_coding);
        return;
    case WorkspaceTask::tool:
    case WorkspaceTask::ask:
        send_request(RequestSurface::assistant);
        return;
    }
}

void MainWindow::run_media_request(const backend::MediaKind kind) {
    if (chat_operation_.running() || media_operation_.running() ||
        long_horizon_operation_.running()) {
        set_status(L"Another foreground task is already running");
        return;
    }
    const auto prompt = narrow(control_text(id::chat_prompt));
    if (prompt.empty()) {
        set_status(kind == backend::MediaKind::image
            ? L"Enter an image prompt first"
            : L"Enter text to speak first");
        return;
    }

    yield_automatic_bootstrap_to_foreground();

    auto snapshot = runtime_model_snapshot();
    apply_source_filter(snapshot);
    for (auto& model : snapshot.models()) {
        model.enabled = model.enabled && model_supports_workspace_task(model);
    }
    const models::ModelProfile* selected = nullptr;
    if (const auto requested = selected_model_id(); requested.has_value()) {
        selected = snapshot.find(*requested);
        if (selected != nullptr && !selected->enabled) {
            selected = nullptr;
        }
    } else {
        for (const auto& model : snapshot.models()) {
            if (!model.enabled) {
                continue;
            }
            if (selected == nullptr) {
                selected = &model;
                continue;
            }
            const auto score = [](const models::ModelProfile& value) {
                return value.priority * 1000.0 +
                    value.quality * value.reliability * 100.0;
            };
            if (score(model) > score(*selected)) {
                selected = &model;
            }
        }
    }
    if (selected == nullptr) {
        const auto capability = kind == backend::MediaKind::image ? L"image" : L"speech/audio";
        const auto error = L"No ready " + std::wstring(capability) +
            L" model is configured. Add an OpenAI-compatible endpoint in Connections and advertise that capability.";
        set_control_text(id::chat_response, error);
        set_control_text(id::chat_plan, L"No compatible endpoint was selected; no request was sent.");
        set_status(error);
        return;
    }

    last_prompt_ = prompt;
    last_response_.clear();
    last_model_id_ = selected->id;
    last_models_used_ = {selected->id};
    last_tool_events_.clear();
    routing::TaskAnalyzer analyzer;
    last_analysis_ = analyzer.analyze(prompt);
    workspace_transcript_ += workspace_transcript_.empty() ? "" : "\r\n";
    workspace_transcript_ += kind == backend::MediaKind::image
        ? "YOU — IMAGE REQUEST\r\n\r\n"
        : "YOU — SPEECH REQUEST\r\n\r\n";
    workspace_transcript_ += prompt + "\r\n\r\nGENERATING — " + selected->display_name + "\r\n";
    set_control_text(id::chat_response, widen(workspace_transcript_));
    set_control_text(id::chat_prompt, L"");
    set_control_text(
        id::chat_plan,
        widen("Capability: " + std::string(kind == backend::MediaKind::image ? "image" : "speech") +
            "\r\nModel: " + selected->display_name +
            "\r\nEndpoint: " + selected->endpoint +
            "\r\nOutput: " + (paths_.root / "outputs").string()));
    set_status(kind == backend::MediaKind::image
        ? L"Generating image…"
        : L"Generating speech…");

    learning_service_->set_interactive_busy(true);
    media_result_pending_ = true;
    const backend::MediaRequest request{
        .kind = kind,
        .model = *selected,
        .prompt = prompt,
        .output_root = paths_.root / "outputs",
    };
    if (!media_operation_.start(
            [this, request](const std::stop_token token) {
                struct BusyReset final {
                    learning::ContinualLearningService* service;
                    ~BusyReset() { service->set_interactive_busy(false); }
                } reset{learning_service_.get()};
                backend::UnifiedProviderRouter provider_router;
                provider_router.register_provider(std::make_shared<backend::MediaTaskProvider>());
                auto unified = provider_router.execute(
                    backend::UnifiedTaskRequest{
                        .modality = request.kind == backend::MediaKind::image
                            ? backend::WorkModality::image_generation
                            : backend::WorkModality::audio_generation,
                        .profile = backend::ExecutionProfile::interactive,
                        .preferred_provider_id = "native-media",
                        .prompt = request.prompt,
                        .model = request.model,
                        .output_root = request.output_root,
                    },
                    token);
                backend::MediaResult result{
                    .succeeded = unified.succeeded,
                    .kind = request.kind,
                    .model_id = request.model.id,
                    .error = std::move(unified.error),
                };
                if (!unified.artifacts.empty()) {
                    result.output_path = unified.artifacts.front().path;
                    result.mime_type = unified.artifacts.front().mime_type;
                }
                const auto succeeded = result.succeeded;
                const auto message = succeeded ? "media generation complete" : result.error;
                {
                    std::lock_guard lock(result_mutex_);
                    pending_media_result_ = std::move(result);
                }
                return util::OperationResult{
                    .state = succeeded ? util::OperationState::succeeded :
                        (token.stop_requested() ? util::OperationState::cancelled : util::OperationState::failed),
                    .exit_code = succeeded ? 0 : 1,
                    .message = message,
                };
            },
            kind == backend::MediaKind::image ? "generating image" : "generating speech")) {
        learning_service_->set_interactive_busy(false);
        media_result_pending_ = false;
        set_status(L"Unable to schedule media generation");
    }
}

void MainWindow::send_coding_prompt() {
    send_request(RequestSurface::coding);
}

void MainWindow::send_request(const RequestSurface surface) {
    if (chat_operation_.running()) {
        set_status(L"A local model request is already running");
        return;
    }
    if (long_horizon_operation_.running()) {
        set_status(L"The build/test coding agent is using the inference lane");
        return;
    }
    if (media_operation_.running()) {
        set_status(L"Media generation is using the foreground inference lane");
        return;
    }

    const auto workspace = is_workspace_surface(surface);
    const auto coding = is_coding_surface(surface);
    const auto prompt_id = workspace ? id::chat_prompt : id::coding_task;
    const auto response_id = workspace ? id::chat_response : id::coding_response;
    const auto plan_id = workspace ? id::chat_plan : id::coding_plan;
    const auto raw_prompt = narrow(control_text(prompt_id));
    if (raw_prompt.empty()) {
        set_status(coding ? L"Enter a coding task first" : L"Enter a message first");
        return;
    }

    yield_automatic_bootstrap_to_foreground();

    // User inference always wins over synthetic teacher-data generation.
    if (piggyback_operation_.running()) {
        piggyback_operation_.cancel();
        set_control_text(
            id::chat_learning_status,
            L"Background teacher generation is yielding to this request.");
    }

    auto snapshot = runtime_model_snapshot();
    apply_source_filter(snapshot);
    if (workspace) {
        for (auto& model : snapshot.models()) {
            model.enabled = model.enabled && model_supports_workspace_task(model);
        }
    }
    if (const auto selected = selected_model_id(); selected.has_value()) {
        const auto* selected_profile = snapshot.find(*selected);
        if (selected_profile == nullptr || !selected_profile->enabled) {
            const auto error =
                L"The selected model is no longer responding. Select Automatic or Discover again.";
            set_control_text(response_id, error);
            set_status(error);
            refresh_model_selectors();
            return;
        }
        for (auto& model : snapshot.models()) {
            model.enabled = model.id == *selected && model.enabled;
        }
    }

    const auto has_usable_model = std::ranges::any_of(
        snapshot.models(),
        [](const models::ModelProfile& model) { return model.enabled; });
    if (!has_usable_model) {
        const auto error = inference_source_ == InferenceSource::custom_model
            ? L"The built-in AI is not ready. No other AI was used."
            : inference_source_ == InferenceSource::local_bridge
                ? L"No connected AI is ready."
                : L"No AI is ready.";
        set_control_text(response_id, error);
        set_control_text(plan_id, L"The selected AI is not ready.");
        set_status(error);
        return;
    }

    const auto recent_context = recent_conversation_context(conversation_history_);
    std::string execution_prompt = raw_prompt;
    std::string execution_context;
    if (coding) {
        const auto repository = narrow(control_text(
            workspace ? id::chat_repository : id::coding_repository));
        execution_context =
            "Act as the primary C++23 coding agent. Give complete, directly usable source changes. "
            "Preserve platform-neutral/platform-specific separation, explicit ownership, and "
            "explain only material tradeoffs.\n\n";
        if (!recent_context.empty()) {
            execution_context += recent_context;
        }
        if (!repository.empty()) {
            execution_context += "Repository root: " + repository + "\n";
        }
    } else if (!recent_context.empty()) {
        execution_context = recent_context;
    }
    if (workspace && workspace_task() == WorkspaceTask::tool) {
        execution_context = "Use the available MCP tools to complete this task. Report each tool action and its result.\n\n" +
            execution_context;
    }

    last_prompt_ = raw_prompt;
    last_response_.clear();
    last_model_id_.clear();
    last_models_used_.clear();
    last_tool_events_.clear();
    last_response_surface_ = surface;

    if (workspace) {
        if (!workspace_transcript_.empty()) {
            if (!workspace_transcript_.ends_with("\r\n\r\n")) {
                workspace_transcript_ += "\r\n\r\n";
            }
            workspace_transcript_ +=
                "------------------------------------------------------------\r\n\r\n";
        }
        workspace_transcript_ += coding ? "YOU — CODING REQUEST\r\n\r\n" : "YOU\r\n\r\n";
        workspace_transcript_ += raw_prompt;
        workspace_transcript_ += "\r\n\r\n";
        const auto heading = requested_runtime_heading();
        pending_transcript_heading_offset_ = workspace_transcript_.size();
        pending_transcript_heading_length_ = heading.size();
        workspace_transcript_ += heading;
        workspace_transcript_ += "\r\n\r\n";
        set_control_text(response_id, widen(workspace_transcript_));
        set_control_text(prompt_id, L"");
    } else {
        pending_transcript_heading_offset_ = std::string::npos;
        pending_transcript_heading_length_ = 0;
        set_control_text(response_id, L"");
    }
    if (workspace) {
        set_control_text(id::chat_plan_label, L"How it is running");
    }
    set_control_text(plan_id, L"Routing and connecting…");

    {
        std::lock_guard lock(stream_mutex_);
        pending_stream_text_.clear();
        stream_target_control_ = response_id;
    }

    const auto request_status = inference_source_ == InferenceSource::custom_model
        ? (coding
            ? L"Running the selected built-in coding AI…"
            : L"Running the selected built-in AI…")
        : inference_source_ == InferenceSource::local_bridge
            ? (coding
                ? L"Sending through the selected connected local LLM for coding…"
                : L"Sending through the selected connected local LLM…")
            : (coding
                ? L"Routing to the fastest capable coding AI source…"
                : L"Routing to the fastest available AI source…");
    set_status(request_status);
    set_control_text(
        id::chat_learning_status,
        L"Answering now. Automatic learning is waiting.");

    learning_service_->set_interactive_busy(true);
    const auto internal_base_url = backend_server_.running()
        ? backend_server_.base_url()
        : std::string{};
    auto experts = learning_service_->expert_snapshot();
    const auto* memory_store = &learning_service_->memory_store();
    const auto mcp_client = mcp_client_;

    const auto tool_request = workspace && workspace_task() == WorkspaceTask::tool;
    const auto routing_prompt = tool_request
        ? std::string{"Use MCP tools to complete this task: "} + raw_prompt
        : raw_prompt;
    const auto task_modality = tool_request
        ? backend::WorkModality::tool_action
        : backend::WorkModality::interactive_text;
    const auto execution_profile = coding
        ? backend::ExecutionProfile::coding_agent
        : backend::ExecutionProfile::interactive;
    const auto allow_automatic_fallback =
        inference_source_ == InferenceSource::automatic &&
        !selected_model_id().has_value();
    const auto started = chat_operation_.start(
        [this,
         surface,
         analysis_prompt = routing_prompt,
         execution_prompt = std::move(execution_prompt),
         execution_context = std::move(execution_context),
         internal_base_url,
         snapshot = std::move(snapshot),
         experts = std::move(experts),
         memory_store,
         mcp_client,
         response_id,
         task_modality,
         execution_profile,
         allow_automatic_fallback](const std::stop_token stop_token) mutable {
            struct BusyReset final {
                learning::ContinualLearningService* service;
                ~BusyReset() { service->set_interactive_busy(false); }
            } reset{learning_service_.get()};

            backend::UnifiedProviderRouter provider_router;
            provider_router.register_provider(std::make_shared<backend::AgentTaskProvider>(
                snapshot,
                std::move(experts),
                *memory_store,
                internal_base_url,
                mcp_client.get()));
            auto unified = provider_router.execute(
                backend::UnifiedTaskRequest{
                    .modality = task_modality,
                    .profile = execution_profile,
                    .preferred_provider_id = "native-agent",
                    .prompt = std::move(execution_prompt),
                    .analysis_prompt = std::move(analysis_prompt),
                    .context = std::move(execution_context),
                    .stream = true,
                    .allow_automatic_fallback = allow_automatic_fallback,
                    .on_event = [this, response_id, stop_token](const backend::TaskEvent& event) {
                        if (event.kind == backend::TaskEventKind::text_delta) {
                            std::lock_guard lock(stream_mutex_);
                            stream_target_control_ = response_id;
                            pending_stream_text_.append(event.text);
                        }
                        return !stop_token.stop_requested();
                    },
                },
                stop_token);
            backend::AgentResult result{
                .succeeded = unified.succeeded,
                .response = std::move(unified.content),
                .error = std::move(unified.error),
                .plan = std::move(unified.plan),
                .models_used = std::move(unified.models_used),
                .tool_events = std::move(unified.tool_events),
                .effective_tokens_per_second = unified.effective_tokens_per_second,
            };

            {
                std::lock_guard lock(result_mutex_);
                pending_chat_result_ = result;
                pending_chat_surface_ = surface;
                chat_result_consumed_ = false;
            }
            {
                std::lock_guard lock(model_mutex_);
                for (const auto& updated : snapshot.models()) {
                    if (auto* current = model_registry_.find(updated.id);
                        current != nullptr) {
                        current->tokens_per_second = updated.tokens_per_second;
                        current->first_token_ms = updated.first_token_ms;
                        current->quality = updated.quality;
                        current->reliability = updated.reliability;
                    }
                }
                try {
                    model_registry_.save(paths_.model_registry);
                } catch (...) {
                }
            }
            return util::OperationResult{
                .state = result.succeeded
                    ? util::OperationState::succeeded
                    : (stop_token.stop_requested()
                        ? util::OperationState::cancelled
                        : util::OperationState::failed),
                .exit_code = result.succeeded ? 0 : 1,
                .message = result.succeeded ? "response complete" : result.error,
            };
        },
        coding ? "running local coding model" : "running local assistant");
    if (!started) {
        learning_service_->set_interactive_busy(false);
        if (workspace && pending_transcript_heading_offset_ != std::string::npos) {
            const auto heading = std::string{"ERROR — REQUEST NOT STARTED"};
            workspace_transcript_.replace(
                pending_transcript_heading_offset_,
                pending_transcript_heading_length_,
                heading);
            pending_transcript_heading_offset_ = std::string::npos;
            pending_transcript_heading_length_ = 0;
            workspace_transcript_ +=
                "The request could not be scheduled. Try again after the current operation stops.\r\n\r\n";
            set_control_text(response_id, widen(workspace_transcript_));
        }
        set_control_text(
            id::chat_learning_status,
            L"Background learning ready.");
        set_status(L"Unable to schedule the selected AI source");
    }
}

void MainWindow::flush_stream_text() {
    std::string text;
    int target = 0;
    {
        std::lock_guard lock(stream_mutex_);
        text.swap(pending_stream_text_);
        target = stream_target_control_;
    }
    if (!text.empty() && target != 0) {
        append_control_text(target, widen(text));
    }
}

void MainWindow::accept_last_response() {
    if (last_prompt_.empty() || last_response_.empty()) {
        set_status(L"There is no completed response to accept");
        return;
    }
    const auto tool_trace = join_tool_events(last_tool_events_);
    const auto accepted = learning_service_->submit(memory::Episode{
        .id = {},
        .skill = skill_for(last_analysis_),
        .prompt = last_prompt_,
        .response = last_response_,
        .model_id = last_model_id_,
        .score = 0.92,
        .compiler_passed = false,
        .tests_passed = false,
        .human_accepted = true,
        .human_rejected = false,
        .deterministic_verified = false,
        .reference_verified = false,
        .self_generated = true,
        .eligible_for_training = false,
        .verifier_model_id = {},
        .verification_notes = {},
        .provenance = is_coding_surface(last_response_surface_)
            ? "coding-agent-accept"
            : "interactive-accept",
        .knowledge_destination = "episodic-memory",
        .content_hash = {},
        .correction = {},
        .tool_trace_hash = tool_trace,
        .created_unix_ms = 0,
    });
    if (accepted) {
        std::lock_guard lock(model_mutex_);
        std::ranges::sort(last_models_used_);
        last_models_used_.erase(
            std::ranges::unique(last_models_used_).begin(),
            last_models_used_.end());
        for (const auto& model_id : last_models_used_) {
            model_registry_.record_feedback(model_id, true);
        }
        try {
            model_registry_.save(paths_.model_registry);
        } catch (...) {
        }
    }
    set_status(accepted
        ? L"Accepted into immediate memory; verified repetition can graduate it into background weight learning"
        : L"The response did not pass the learning-policy gate");
    update_skill_report();
    update_learning_report();
}

void MainWindow::reject_last_response() {
    if (last_prompt_.empty() || last_response_.empty()) {
        set_status(L"There is no completed response to reject");
        return;
    }
    const auto stored = learning_service_->submit(memory::Episode{
        .id = {},
        .skill = skill_for(last_analysis_),
        .prompt = last_prompt_,
        .response = last_response_,
        .model_id = last_model_id_,
        .score = 0.0,
        .compiler_passed = false,
        .tests_passed = false,
        .human_accepted = false,
        .human_rejected = true,
        .deterministic_verified = false,
        .reference_verified = false,
        .self_generated = true,
        .eligible_for_training = false,
        .verifier_model_id = {},
        .verification_notes = {},
        .provenance = is_coding_surface(last_response_surface_)
            ? "coding-agent-reject"
            : "interactive-reject",
        .knowledge_destination = "episodic-memory",
        .content_hash = {},
        .correction = {},
        .tool_trace_hash = join_tool_events(last_tool_events_),
        .created_unix_ms = 0,
    });
    {
        std::lock_guard lock(model_mutex_);
        std::ranges::sort(last_models_used_);
        last_models_used_.erase(
            std::ranges::unique(last_models_used_).begin(),
            last_models_used_.end());
        for (const auto& model_id : last_models_used_) {
            model_registry_.record_feedback(model_id, false);
        }
        try {
            model_registry_.save(paths_.model_registry);
        } catch (...) {
        }
    }
    last_response_.clear();
    last_model_id_.clear();
    last_models_used_.clear();
    last_tool_events_.clear();
    set_status(stored
        ? L"Rejected response retained as negative evidence and excluded from positive training"
        : L"That rejection was already recorded");
}

void MainWindow::discover_local_ai() {
    if (discovery_operation_.running()) {
        set_status(L"Local AI discovery is already running");
        return;
    }
    set_control_text(id::models_discovery_status, L"Scanning localhost model APIs…");
    set_control_text(id::chat_backend, L"Scanning local model servers…");
    set_control_text(id::coding_backend, L"Scanning local model servers…");
    static_cast<void>(discovery_operation_.start(
        [this](const std::stop_token token) {
            if (token.stop_requested()) {
                return util::OperationResult{
                    .state = util::OperationState::cancelled,
                    .exit_code = 1,
                    .message = "discovery cancelled",
                };
            }
            auto report = models::discover_local_models(std::chrono::milliseconds(650));
            const auto count = report.models.size();
            {
                std::lock_guard lock(result_mutex_);
                pending_discovery_report_ = std::move(report);
            }
            return util::OperationResult{
                .state = util::OperationState::succeeded,
                .exit_code = 0,
                .message = count == 0U
                    ? "no local model API responded"
                    : std::to_string(count) + " local model(s) found",
            };
        },
        "discovering local AI servers"));
}

void MainWindow::add_manual_local_ai() {
    const auto endpoint = narrow(control_text(id::models_manual_endpoint));
    const auto model_name = narrow(control_text(id::models_manual_model));
    if (endpoint.empty() || model_name.empty()) {
        set_status(L"Enter both an endpoint URL and a model ID");
        return;
    }
    const auto selection = static_cast<int>(::SendMessageW(
        control(id::models_manual_protocol), CB_GETCURSEL, 0, 0));
    const auto protocol = selection == 1 ? std::string{"ollama"} : std::string{"openai"};
    auto profile = models::make_manual_local_model(
        endpoint,
        model_name,
        protocol,
        checked(id::models_manual_coding),
        checked(id::models_manual_teacher));
    const auto capabilities = narrow(control_text(id::models_manual_capabilities));
    if (!capabilities.empty()) {
        profile.capabilities.clear();
        std::size_t begin = 0U;
        while (begin <= capabilities.size()) {
            const auto end = capabilities.find(',', begin);
            auto item = capabilities.substr(
                begin,
                end == std::string::npos ? std::string::npos : end - begin);
            const auto first = item.find_first_not_of(" \t\r\n");
            const auto last = item.find_last_not_of(" \t\r\n");
            if (first != std::string::npos) {
                item = item.substr(first, last - first + 1U);
                profile.capabilities.push_back(std::move(item));
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1U;
        }
        profile.supports_tools = models::has_capability(profile, "tools");
        profile.coding_agent = profile.coding_agent || models::has_capability(profile, "code") ||
            models::has_capability(profile, "repository") || models::has_capability(profile, "cpp23");
    }
    const auto display_name = profile.display_name;
    {
        std::lock_guard lock(model_mutex_);
        model_registry_.upsert(std::move(profile));
        model_registry_.save(paths_.model_registry);
    }
    load_models_editor();
    refresh_model_selectors();
    set_status(widen("Added local model: " + display_name));
}

void MainWindow::connect_mcp() {
    if (mcp_operation_.running()) {
        set_status(L"MCP connection is already in progress");
        return;
    }
    const auto endpoint = narrow(control_text(id::models_mcp_endpoint));
    if (endpoint.empty()) {
        set_status(L"Enter an MCP Streamable HTTP endpoint");
        return;
    }
    set_control_text(id::models_mcp_status, L"Connecting…");
    static_cast<void>(mcp_operation_.start(
        [this, endpoint](const std::stop_token token) {
            if (token.stop_requested()) {
                return util::OperationResult{
                    .state = util::OperationState::cancelled,
                    .exit_code = 1,
                    .message = "MCP connection cancelled",
                };
            }
            auto result = mcp_client_->connect_http(endpoint);
            {
                std::lock_guard lock(result_mutex_);
                pending_mcp_result_ = result;
            }
            return util::OperationResult{
                .state = result.succeeded
                    ? util::OperationState::succeeded
                    : util::OperationState::failed,
                .exit_code = result.succeeded ? 0 : 1,
                .message = result.succeeded
                    ? "MCP connected"
                    : result.error,
            };
        },
        "connecting MCP server"));
}

std::string MainWindow::selected_training_skill() const {
    const auto selection = static_cast<int>(::SendMessageW(
        control(id::learning_piggyback_skill), CB_GETCURSEL, 0, 0));
    if (selection >= 0 &&
        selection < custom_training_target_index) {
        return std::string(training_targets[static_cast<std::size_t>(selection)].skill);
    }
    if (selection == custom_training_target_index) {
        const auto value = trim_line_endings(narrow(control_text(id::learning_custom_skill)));
        return normalize_training_skill(
            value.empty() ? std::string_view{"custom-skill"} : std::string_view{value});
    }
    return training_target_.empty()
        ? std::string{"english-foundation"}
        : normalize_training_skill(training_target_);
}

std::filesystem::path MainWindow::selected_training_dataset() const {
    auto value = control_text(id::learning_dataset_path);
    while (!value.empty() && (value.back() == L'\r' || value.back() == L'\n')) {
        value.pop_back();
    }
    return value.empty() ? custom_training_dataset_ : std::filesystem::path(value);
}

std::string MainWindow::training_cursor_key(
    const std::string_view skill,
    const std::filesystem::path& dataset) const {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    auto hash = offset;
    const auto normalized_path = dataset.lexically_normal().generic_wstring();
    for (const auto character : normalized_path) {
        const auto value = static_cast<std::uint32_t>(character);
        for (auto shift = 0U; shift < 32U; shift += 8U) {
            hash ^= static_cast<std::uint8_t>((value >> shift) & 0xFFU);
            hash *= prime;
        }
    }
    std::ostringstream stream;
    stream << normalize_training_skill(skill) << '-' << std::hex << hash;
    return stream.str();
}

void MainWindow::select_training_skill(const std::string_view skill) {
    const auto normalized = normalize_training_skill(skill);
    const auto selection = training_target_index(normalized);
    if (selection == custom_training_target_index) {
        custom_training_skill_ = normalized;
        training_target_ = normalized;
        set_control_text(id::learning_custom_skill, widen(custom_training_skill_));
    } else {
        training_target_ = std::string(
            training_targets[static_cast<std::size_t>(selection)].skill);
    }
    static_cast<void>(::SendMessageW(
        control(id::learning_piggyback_skill),
        CB_SETCURSEL,
        static_cast<WPARAM>(selection),
        0));
    const auto custom = selection == custom_training_target_index;
    static_cast<void>(::EnableWindow(control(id::learning_custom_skill_label), custom));
    static_cast<void>(::EnableWindow(control(id::learning_custom_skill), custom));
}

std::size_t MainWindow::piggyback_cursor_for(const std::string_view key) {
    const auto normalized = std::string(key);
    if (const auto iterator = piggyback_seed_offsets_.find(normalized);
        iterator != piggyback_seed_offsets_.end()) {
        return iterator->second;
    }

    std::size_t cursor = 0U;
    try {
        const auto contents = trim_line_endings(read_text(
            paths_.root / "state" / "piggyback-cursors" / (normalized + ".txt")));
        if (!contents.empty()) {
            cursor = static_cast<std::size_t>(std::stoull(contents));
        }
    } catch (...) {
        cursor = 0U;
    }
    piggyback_seed_offsets_[normalized] = cursor;
    return cursor;
}

void MainWindow::persist_piggyback_cursor(
    const std::string_view key,
    const std::size_t cursor) {
    const auto normalized = std::string(key);
    piggyback_seed_offsets_[normalized] = cursor;
    try {
        write_text(
            paths_.root / "state" / "piggyback-cursors" / (normalized + ".txt"),
            std::to_string(cursor) + "\n");
    } catch (...) {
    }
}

void MainWindow::persist_training_configuration() const {
    try {
        write_text(
            paths_.root / "config" / "training-target.txt",
            training_target_ + "\n");
        write_text(
            paths_.root / "config" / "custom-training-skill.txt",
            custom_training_skill_ + "\n");
        write_text(
            paths_.root / "config" / "custom-training-dataset.txt",
            narrow(custom_training_dataset_.wstring()) + "\n");
    } catch (...) {
    }
}

void MainWindow::stop_training_pass() {
    const auto had_foundry = piggyback_operation_.running();
    if (had_foundry) {
        piggyback_operation_.cancel();
    }
    learning_service_->pause(true);
    set_checked(id::learning_pause, true);
    set_status(had_foundry
        ? L"Stopping the current example pass; weight training pauses at its next safe checkpoint"
        : L"Background weight training is paused; accepted evidence and checkpoints remain intact");
    update_learning_report();
}

void MainWindow::handle_training_target_change() {
    const auto previous = training_target_;
    const auto selection = static_cast<int>(::SendMessageW(
        control(id::learning_piggyback_skill), CB_GETCURSEL, 0, 0));
    if (selection == custom_training_target_index) {
        const auto custom = trim_line_endings(narrow(control_text(id::learning_custom_skill)));
        custom_training_skill_ = normalize_training_skill(
            custom.empty() ? std::string_view{"custom-skill"} : std::string_view{custom});
    }
    training_target_ = selected_training_skill();
    const auto custom_selected = selection == custom_training_target_index;
    static_cast<void>(::EnableWindow(control(id::learning_custom_skill_label), custom_selected));
    static_cast<void>(::EnableWindow(control(id::learning_custom_skill), custom_selected));
    persist_training_configuration();

    if (training_target_ != previous) {
        set_status(widen(
            "Training target changed to " + training_target_ +
            ". The current pass keeps its captured target; the next scheduled pass uses the new selection."));
    }
    update_learning_report();
}

void MainWindow::browse_custom_training_data() {
    const auto selected = choose_training_dataset();
    if (!selected.has_value()) {
        return;
    }
    custom_training_dataset_ = selected->lexically_normal();
    set_control_text(id::learning_dataset_path, custom_training_dataset_.wstring());
    persist_training_configuration();
    set_status(L"Custom training dataset selected");
}

void MainWindow::import_custom_training_data() {
    custom_training_dataset_ = selected_training_dataset().lexically_normal();
    if (!std::filesystem::is_regular_file(custom_training_dataset_)) {
        set_status(L"Select an existing JSONL training dataset first");
        return;
    }
    persist_training_configuration();
    generate_piggyback(false, true);
}

void MainWindow::generate_piggyback(
    const bool automatic,
    const bool trusted_only) {
    if (piggyback_operation_.running()) {
        if (!automatic) {
            set_status(L"A teacher or dataset-import batch is already running");
        }
        return;
    }
    const auto pipeline = learning_service_->pipeline_snapshot();
    if (pipeline.active_job.has_value() || pipeline.queued_updates > 0U) {
        if (!automatic) {
            set_status(
                L"Weight training is already active or queued. The example pass "
                L"will wait so the current checkpoint can finish without disruption.");
        }
        next_piggyback_at_ =
            std::chrono::steady_clock::now() + std::chrono::minutes(2);
        return;
    }

    const auto target_skill = selected_training_skill();
    const auto dataset = trusted_only
        ? selected_training_dataset().lexically_normal()
        : (paths_.data / "seed.jsonl");
    if (!std::filesystem::is_regular_file(dataset)) {
        if (!automatic) {
            set_status(widen("Training dataset does not exist: " + dataset.string()));
        }
        return;
    }

    models::ModelProfile teacher{};
    std::optional<models::ModelProfile> verifier;
    if (!trusted_only) {
        auto snapshot = runtime_model_snapshot();
        std::vector<models::ModelProfile> teachers;
        for (const auto& model : snapshot.models()) {
            if (model.enabled &&
                !models::is_internal_model(model) &&
                model.teacher &&
                models::has_capability(model, "chat")) {
                teachers.push_back(model);
            }
        }
        if (teachers.empty()) {
            if (!automatic) {
                set_status(L"No external teacher model is available. Start a capable local LLM, mark it as a teacher, and discover it first.");
            }
            next_piggyback_at_ =
                std::chrono::steady_clock::now() + std::chrono::minutes(2);
            return;
        }

        std::ranges::sort(teachers, [](const auto& left, const auto& right) {
            const auto left_score = left.quality * left.reliability +
                (left.teacher ? 0.12 : 0.0);
            const auto right_score = right.quality * right.reliability +
                (right.teacher ? 0.12 : 0.0);
            return left_score > right_score;
        });
        teacher = teachers.front();

        std::vector<models::ModelProfile> verifiers;
        for (const auto& candidate : snapshot.models()) {
            if (candidate.enabled &&
                !models::is_internal_model(candidate) &&
                models::has_capability(candidate, "chat") &&
                candidate.id != teacher.id &&
                (candidate.verifier || candidate.quality >= 0.68)) {
                verifiers.push_back(candidate);
            }
        }
        std::ranges::sort(verifiers, [](const auto& left, const auto& right) {
            const auto left_score = left.quality * left.reliability +
                (left.verifier ? 0.18 : 0.0) +
                std::min(0.08, left.tokens_per_second / 1000.0);
            const auto right_score = right.quality * right.reliability +
                (right.verifier ? 0.18 : 0.0) +
                std::min(0.08, right.tokens_per_second / 1000.0);
            return left_score > right_score;
        });
        if (!verifiers.empty()) {
            verifier = verifiers.front();
        }
    }

    std::size_t maximum_examples = trusted_only ? 100000U : 8U;
    if (!trusted_only) {
        try {
            maximum_examples = static_cast<std::size_t>(std::stoull(
                narrow(control_text(id::learning_piggyback_count))));
        } catch (...) {
            if (!automatic) {
                set_status(L"Teacher examples per batch must be an integer from 1 to 64");
            }
            return;
        }
        maximum_examples = std::clamp<std::size_t>(maximum_examples, 1U, 64U);
    }

    training_target_ = target_skill;
    active_piggyback_skill_ = target_skill;
    active_piggyback_trusted_only_ = trusted_only;
    active_piggyback_cursor_key_ = training_cursor_key(target_skill, dataset);
    custom_training_dataset_ = trusted_only ? dataset : custom_training_dataset_;
    persist_training_configuration();

    if (trusted_only) {
        set_control_text(
            id::learning_piggyback_status,
            widen("Importing trusted JSONL into target: " + target_skill));
    } else {
        set_control_text(
            id::learning_piggyback_status,
            widen("Target: " + target_skill + " • teacher: " + teacher.display_name +
                  (verifier.has_value()
                      ? " • verifier: " + verifier->display_name
                      : " • no independent verifier")));
    }

    const auto request = learning::PiggybackRequest{
        .seed_dataset = dataset,
        .target_skill = target_skill,
        .restrict_to_target_domain = !trusted_only,
        .generate_teacher_variations = !trusted_only,
        .internal_base_url = backend_server_.running()
            ? backend_server_.base_url()
            : std::string{},
        .teacher = teacher,
        .verifier = verifier,
        .seed_offset = trusted_only
            ? 0U
            : piggyback_cursor_for(active_piggyback_cursor_key_),
        .maximum_examples = maximum_examples,
        .maximum_output_tokens = 768,
        .minimum_verifier_score = 0.86,
    };

    // The idle lease serializes evidence generation with new weight jobs. The
    // pipeline guard above ensures acquiring it cannot preempt an existing job.
    learning_service_->set_interactive_busy(true);
    const auto started = piggyback_operation_.start(
        [this, request](const std::stop_token token) {
            struct BusyReset final {
                learning::ContinualLearningService* service;
                ~BusyReset() { service->set_interactive_busy(false); }
            } reset{learning_service_.get()};
            auto result = learning::run_piggyback_foundry(
                request,
                *learning_service_,
                [this](const learning::PiggybackProgress& progress) {
                    piggyback_operation_.set_message(progress.message);
                },
                token);
            {
                std::lock_guard lock(result_mutex_);
                pending_piggyback_result_ = result;
            }
            return util::OperationResult{
                .state = result.succeeded
                    ? util::OperationState::succeeded
                    : (result.cancelled
                        ? util::OperationState::cancelled
                        : util::OperationState::failed),
                .exit_code = result.succeeded ? 0 : 1,
                .message = result.succeeded
                    ? (request.generate_teacher_variations
                        ? "teacher-data pass complete"
                        : "trusted JSONL import complete")
                    : result.error,
            };
        },
        trusted_only
            ? "importing trusted custom training data"
            : "generating training examples from local teacher models");
    if (!started) {
        learning_service_->set_interactive_busy(false);
    }
    next_piggyback_at_ =
        std::chrono::steady_clock::now() + (started
            ? std::chrono::minutes(10)
            : std::chrono::minutes(2));
}

void MainWindow::create_internal_model(
    const bool bootstrap,
    const bool automatic) {
    if (backend_operation_.running()) {
        if (!automatic) {
            set_status(L"Another built-in AI action is already running");
        }
        return;
    }

    auto selected_paths = paths_;
    selected_paths.python = selected_python_;
    if (!runtime::backend_files_present(selected_paths)) {
        if (!automatic) {
            set_status(L"Built-in AI setup files are not available yet");
        }
        return;
    }

    const auto state = runtime::inspect_internal_model_state(paths_);
    // Automatic setup starts with the smallest profile and its 24k quality target.
    // Existing models keep their recorded profile; backup controls may explicitly choose another.
    const auto selected_index = automatic
        ? 0
        : static_cast<int>(::SendMessageW(
            control(id::models_internal_profile), CB_GETCURSEL, 0, 0));
    std::string profile = selected_index == 2
        ? "large-local"
        : (selected_index == 1 ? "standard" : "micro");
    std::size_t bootstrap_target = selected_index == 2
        ? 50000U
        : (selected_index == 1 ? 36000U : 24000U);

    if (state.manifest_valid) {
        bootstrap_target = runtime::default_bootstrap_target(state);
        const auto lowered_name = [&state] {
            auto value = state.profile_name;
            std::ranges::transform(value, value.begin(), [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return value;
        }();
        profile = lowered_name.find("large") != std::string::npos
            ? "large-local"
            : (lowered_name.find("standard") != std::string::npos ? "standard" : "micro");
    }
    if (automatic) {
        bootstrap_target = adaptive_bootstrap_target(paths_, state);
    }

    if (bootstrap && state.bootstrap_complete) {
        if (!backend_server_.running()) {
            start_backend(automatic);
        }
        return;
    }
    if (bootstrap && automatic_learning_limit_reached(paths_, state)) {
        lifecycle_last_error_ =
            "Built-in AI reached its bounded learning target without passing every quality check; "
            "no unapproved model was activated.";
        set_status(L"Built-in AI reached its quality limit. The unapproved result was not activated.");
        return;
    }

    const bool needs_initialization = !state.manifest_valid;
    const bool force_reinitialize = state.exists && !state.manifest_valid;
    const auto remaining_steps = !state.manifest_valid
        ? bootstrap_target
        : (bootstrap_target > state.steps ? bootstrap_target - state.steps : 1U);

    backend_server_.stop();
    piggyback_operation_.cancel();
    learning_service_->set_interactive_busy(true);
    std::error_code cleanup_error;
    std::filesystem::remove(
        paths_.root / "state" / "internal-model-progress.json",
        cleanup_error);

    const auto log = paths_.logs / (bootstrap
        ? "internal-bootstrap.log"
        : "internal-create.log");
    set_active_log(log);
    set_control_text(
        id::models_internal_status,
        bootstrap
            ? (state.manifest_valid
                ? L"Continuing built-in AI setup from the last saved point…"
                : L"Creating the built-in AI, then teaching it the basics…")
            : L"Rebuilding the built-in AI from scratch…");

    const auto started = backend_operation_.start(
        [this,
         selected_paths,
         log,
         profile,
         bootstrap,
         bootstrap_target,
         remaining_steps,
         needs_initialization,
         force_reinitialize](const std::stop_token token) {
            struct BusyReset final {
                learning::ContinualLearningService* service;
                ~BusyReset() { service->set_interactive_busy(false); }
            } reset{learning_service_.get()};

            util::OperationResult result{
                .state = util::OperationState::succeeded,
                .exit_code = 0,
                .message = "Existing internal generation is ready",
            };
            if (needs_initialization) {
                result = runtime::initialize_internal_model(
                    selected_paths,
                    log,
                    profile,
                    force_reinitialize,
                    token);
            }
            if (result.state != util::OperationState::succeeded || !bootstrap) {
                return result;
            }

            result = runtime::train_internal_model(
                selected_paths,
                log,
                "english-foundation",
                remaining_steps,
                true,
                bootstrap_target,
                token);
            if (result.state != util::OperationState::succeeded) {
                return result;
            }

            const auto trained_state = runtime::inspect_internal_model_state(selected_paths);
            if (!trained_state.manifest_valid || !trained_state.bootstrap_complete) {
                return util::OperationResult{
                    .state = util::OperationState::failed,
                    .exit_code = 3,
                    .message =
                        "The training process exited without publishing a ready generation; "
                        "an explicit repair retry must continue from the saved candidate",
                };
            }

            std::string message;
            const auto host_started = backend_server_.start(
                selected_paths.python,
                token,
                message);
            return util::OperationResult{
                .state = host_started
                    ? util::OperationState::succeeded
                    : util::OperationState::failed,
                .exit_code = host_started ? 0 : 1,
                .message = host_started
                    ? "Built-in AI setup completed and it is now running"
                    : "Bootstrap completed, but the model host did not start: " + message,
            };
        },
        bootstrap
            ? (automatic
                ? "automatically teaching the built-in AI from its last saved point"
                : "continuing built-in AI setup from its last saved point")
            : "rebuilding the built-in AI from scratch");

    if (started) {
        lifecycle_action_ = LifecycleAction::bootstrap_model;
    } else {
        learning_service_->set_interactive_busy(false);
    }
}

void MainWindow::install_backend(const bool automatic) {
    if (backend_operation_.running()) {
        if (!automatic) {
            set_status(L"Another built-in AI action is already running");
        }
        return;
    }
    if (backend_server_.running()) {
        if (!automatic) {
            set_status(L"Stop the built-in AI before replacing its setup files");
        }
        return;
    }

    std::optional<std::filesystem::path> source;
    if (automatic) {
        source = automatic_training_pack_source(paths_, selected_pack_source_);
    } else {
        const auto selected = std::filesystem::path(
            control_text(id::models_python)).lexically_normal();
        if (training_pack_payload_ready(selected)) {
            source = selected;
        }
    }
    if (!source.has_value()) {
        if (!automatic) {
            set_status(L"Choose a complete built-in AI setup folder first");
        }
        return;
    }

    selected_pack_source_ = source->lexically_normal();
    set_control_text(id::models_python, selected_pack_source_.wstring());
    try {
        write_text(
            paths_.root / "config" / "internal-ai-setup-files.txt",
            selected_pack_source_.string() + "\n");
    } catch (...) {
    }

    const auto catalog = runtime::CapabilityPackManager::catalog();
    const auto descriptor = std::ranges::find(
        catalog, std::string_view{"internal-pytorch-training"},
        &runtime::CapabilityPackDescriptor::id);
    if (descriptor == catalog.end()) {
        set_status(L"The built-in AI setup description is missing");
        return;
    }
    const runtime::CapabilityPackInstallRequest request{
        .descriptor = *descriptor,
        .source_directory = selected_pack_source_,
    };
    const auto packs_root = paths_.packs;
    const auto started = backend_operation_.start(
        [packs_root, request](const std::stop_token token) {
            runtime::CapabilityPackManager manager(packs_root);
            const auto result = manager.install_transactional(request, token);
            return util::OperationResult{
                .state = result.succeeded
                    ? util::OperationState::succeeded
                    : (result.cancelled
                        ? util::OperationState::cancelled
                        : util::OperationState::failed),
                .exit_code = result.succeeded ? 0 : 1,
                .message = result.succeeded
                    ? "Built-in AI setup files are ready; the working model and current task were not changed"
                    : result.error,
            };
        },
        automatic
            ? "automatically preparing the built-in AI files"
            : "replacing the built-in AI files from backup");
    if (started) {
        lifecycle_action_ = LifecycleAction::install_backend;
        set_status(automatic
            ? L"Preparing the built-in AI in the background"
            : L"Replacing the built-in AI setup files; the current task is unchanged");
    }
}

void MainWindow::verify_backend(const bool automatic) {
    if (backend_operation_.running()) {
        return;
    }
    auto selected_paths = paths_;
    selected_paths.python = selected_python_;
    const auto log = paths_.logs / "backend-verify.log";
    set_active_log(log);
    const auto started = backend_operation_.start(
        [selected_paths, log](const std::stop_token token) {
            return runtime::verify_backend(selected_paths, log, token);
        },
        automatic
            ? "automatically checking the built-in AI"
            : "checking the built-in AI from backup controls");
    if (started) {
        lifecycle_action_ = LifecycleAction::verify_backend;
    }
}
void MainWindow::start_backend(const bool automatic) {
    if (backend_operation_.running()) {
        return;
    }
    if (!internal_model_ready(paths_)) {
        if (!automatic) {
            set_status(L"Built-in AI setup is incomplete. Choose Continue setup under Backup and repair.");
            show_page(Page::models);
        }
        return;
    }
    set_active_log(backend_server_.log_path());
    const auto started = backend_operation_.start(
        [this](const std::stop_token token) {
            std::string message;
            const auto success = backend_server_.start(selected_python_, token, message);
            return util::OperationResult{
                .state = success ? util::OperationState::succeeded : util::OperationState::failed,
                .exit_code = success ? 0 : 1,
                .message = std::move(message),
            };
        },
        automatic
            ? "automatically starting the built-in AI"
            : "starting the built-in AI from backup controls");
    if (started) {
        lifecycle_action_ = LifecycleAction::start_host;
    }
}
void MainWindow::stop_backend() {
    automatic_lifecycle_enabled_ = false;
    set_checked(id::models_automation_toggle, false);
    try {
        write_text(paths_.root / "config" / "automatic-lifecycle.txt", "off\n");
    } catch (...) {
    }
    backend_operation_.cancel();
    lifecycle_action_ = LifecycleAction::none;
    backend_server_.stop();
    set_status(L"Built-in AI stopped; automatic restart is paused");
    set_control_text(id::models_status, L"Built-in AI stopped from backup controls");
}
void MainWindow::browse_python() {
    const auto selected = choose_directory(L"Choose the built-in AI setup folder");
    if (!selected.has_value()) {
        return;
    }
    selected_pack_source_ = selected->lexically_normal();
    set_control_text(id::models_python, selected_pack_source_.wstring());
    try {
        write_text(
            paths_.root / "config" / "internal-ai-setup-files.txt",
            selected_pack_source_.string() + "\n");
    } catch (...) {
    }
    set_status(L"Backup setup folder selected; automatic setup will use it when ready");
}
void MainWindow::load_models_editor() {
    set_control_text(id::models_editor, widen(read_text(paths_.model_registry)));
}

void MainWindow::save_models_editor() {
    try {
        write_text(paths_.model_registry, narrow(control_text(id::models_editor)));
        auto loaded = models::ModelRegistry::load(paths_.model_registry);
        {
            std::lock_guard lock(model_mutex_);
            model_registry_ = std::move(loaded);
        }
        refresh_model_selectors();
        set_status(L"Model registry saved and reloaded");
    } catch (const std::exception& error) {
        set_status(widen(std::string{"Unable to save model registry: "} + error.what()));
    }
}

void MainWindow::bootstrap_core_skills() {
    if (!internal_model_ready(paths_)) {
        show_page(Page::models);
        create_internal_model(true);
        return;
    }
    learning_service_->request_skill_update("english-foundation");
    select_training_skill("english-foundation");
    persist_training_configuration();
    set_status(L"English foundation candidate queued; C++ specialization remains manual");
    update_learning_report();
}

void MainWindow::queue_skill_update() {
    auto skill = narrow(control_text(id::skills_skill));
    if (skill.empty()) {
        set_status(L"Enter a skill identifier first");
        return;
    }
    learning_service_->request_skill_update(skill);
    set_status(widen("Queued skill update: " + skill));
    update_learning_report();
}

void MainWindow::rollback_skill() {
    const auto skill = narrow(control_text(id::skills_skill));
    if (skill.empty()) {
        set_status(L"Enter a skill identifier first");
        return;
    }
    const auto rolled_back = learning_service_->rollback_skill(skill);
    set_status(rolled_back
        ? widen("Rolled back active skill: " + skill)
        : widen("No rollback generation exists for: " + skill));
    update_skill_report();
}

void MainWindow::browse_coding_repository() {
    const auto selected = choose_directory(L"Select the repository for the coding agent");
    if (selected.has_value()) {
        set_control_text(id::coding_repository, selected->wstring());
    }
}

void MainWindow::run_coding_agent() {
    const auto repository = control_text(id::chat_repository);
    const auto task = control_text(id::chat_prompt);
    if (repository.empty() || task.empty()) {
        set_status(L"Select a repository and enter a coding task");
        return;
    }
    last_prompt_ = narrow(task);
    workspace_transcript_ += workspace_transcript_.empty() ? "" : "\r\n";
    workspace_transcript_ += "YOU — REPOSITORY TASK\r\n\r\n" + last_prompt_ +
        "\r\n\r\nRUN — BUILD / TEST / REPAIR\r\n\r\nStarting…\r\n";
    set_control_text(id::chat_response, widen(workspace_transcript_));
    set_control_text(id::chat_prompt, L"");
    set_control_text(id::horizon_repository, repository);
    set_control_text(id::horizon_task, task);
    long_horizon_from_coding_ = true;
    start_long_horizon();
}

void MainWindow::browse_repository() {
    const auto selected = choose_directory(L"Select a clean Git repository for the autonomous coding loop");
    if (selected.has_value()) {
        set_control_text(id::horizon_repository, selected->wstring());
    }
}

void MainWindow::start_long_horizon() {
    if (long_horizon_operation_.running()) {
        set_status(L"An autonomous coding loop is already running");
        return;
    }
    if (media_operation_.running()) {
        set_status(L"Wait for media generation before starting the repository task");
        return;
    }
    if (chat_operation_.running()) {
        set_status(L"Wait for the current model response before starting the build/test agent");
        return;
    }

    const auto repository = std::filesystem::path(control_text(id::horizon_repository));
    const auto task = narrow(control_text(id::horizon_task));
    const auto build_command = narrow(control_text(id::horizon_build));
    const auto test_command = narrow(control_text(id::horizon_test));
    if (!std::filesystem::is_directory(repository) || task.empty() || build_command.empty()) {
        set_status(L"Select a repository and enter both a task and build command");
        return;
    }

    std::size_t maximum_iterations = 8;
    try {
        maximum_iterations = static_cast<std::size_t>(std::stoull(
            narrow(control_text(id::horizon_iterations))));
    } catch (...) {
        set_status(L"Maximum loops must be an integer from 1 to 32");
        return;
    }
    maximum_iterations = std::clamp<std::size_t>(maximum_iterations, 1U, 32U);

    yield_automatic_bootstrap_to_foreground();

    const auto repair_model = best_coding_model();
    if (!repair_model.has_value()) {
        set_status(L"No responding coding model is available. Discover a local coding model first.");
        show_page(Page::models);
        select_page_tab(Page::models);
        return;
    }

    std::filesystem::path adapter;
    std::filesystem::path module;
    const auto experts = learning_service_->expert_snapshot();
    const learning::ExpertModule* selected_expert = nullptr;
    for (const auto requested_skill : {
             std::string_view{"cpp23-repository"},
             std::string_view{"cpp23-core"}}) {
        const auto iterator = std::ranges::find(experts, requested_skill, &learning::ExpertModule::skill);
        if (iterator != experts.end() && iterator->base_model == repair_model->model_name) {
            selected_expert = &*iterator;
            break;
        }
    }
    if (selected_expert != nullptr) {
        if (selected_expert->state == learning::ExpertState::full_rank_active) {
            module = selected_expert->module_path;
        }
        if (selected_expert->state == learning::ExpertState::lora_active ||
            selected_expert->state == learning::ExpertState::full_rank_active) {
            adapter = selected_expert->adapter_path;
        }
    }

    const long_horizon::Request request{
        .repository = repository,
        .state_root = paths_.root,
        .backend_base_url = backend_server_.running() ? backend_server_.base_url() : std::string{},
        .repair_model = *repair_model,
        .task = task,
        .build_command = build_command,
        .test_command = test_command,
        .adapter_path = std::move(adapter),
        .module_path = std::move(module),
        .maximum_iterations = maximum_iterations,
        .require_clean_worktree = checked(id::horizon_clean),
        .rollback_on_failure = true,
    };

    set_control_text(
        id::horizon_report,
        L"Starting implementation -> build -> diagnose -> repair -> test loop…");
    if (long_horizon_from_coding_) {
        set_control_text(
            id::chat_response,
            widen(workspace_transcript_ + "\r\nRepository progress is shown in Execution details.\r\n"));
    }
    set_status(L"Repository task started; background weight learning is yielding");
    learning_service_->set_interactive_busy(true);
    static_cast<void>(long_horizon_operation_.start(
        [this, request, task, model_id = repair_model->id](const std::stop_token token) mutable {
            struct BusyReset final {
                learning::ContinualLearningService* service;
                ~BusyReset() { service->set_interactive_busy(false); }
            } reset{learning_service_.get()};

            backend::UnifiedProviderRouter provider_router;
            provider_router.register_provider(std::make_shared<backend::RepositoryTaskProvider>(
                request.state_root,
                request.backend_base_url,
                request.repair_model));
            auto unified = provider_router.execute(
                backend::UnifiedTaskRequest{
                    .modality = backend::WorkModality::repository_work,
                    .profile = backend::ExecutionProfile::long_horizon,
                    .preferred_provider_id = "native-repository",
                    .prompt = request.task,
                    .model = request.repair_model,
                    .repository = request.repository,
                    .output_root = request.state_root / "outputs",
                    .build_command = request.build_command,
                    .test_command = request.test_command,
                    .adapter_path = request.adapter_path,
                    .module_path = request.module_path,
                    .maximum_iterations = request.maximum_iterations,
                    .require_clean_worktree = request.require_clean_worktree,
                    .rollback_on_failure = request.rollback_on_failure,
                },
                token);
            long_horizon::Result result{
                .operation = {
                    .state = unified.succeeded
                        ? util::OperationState::succeeded
                        : (unified.cancelled
                            ? util::OperationState::cancelled
                            : util::OperationState::failed),
                    .exit_code = unified.succeeded ? 0 : 1,
                    .message = unified.succeeded
                        ? "repository task verified"
                        : unified.error,
                },
                .completed = unified.succeeded,
                .compiler_passed = unified.compiler_passed,
                .tests_passed = unified.tests_passed,
                .iterations = unified.iterations,
                .run_directory = std::move(unified.run_directory),
                .final_diff = std::move(unified.content),
            };
            for (const auto& artifact : unified.artifacts) {
                if (artifact.kind == backend::ArtifactKind::patch) {
                    result.final_diff_path = artifact.path;
                } else if (artifact.kind == backend::ArtifactKind::log) {
                    result.trajectory_path = artifact.path;
                }
            }
            for (const auto& event : unified.tool_events) {
                result.events.push_back({
                    .phase = event,
                    .exit_code = unified.succeeded ? 0 : 1,
                });
            }
            if (result.completed && result.compiler_passed && result.tests_passed) {
                auto verified_response = result.final_diff;
                if (verified_response.empty()) {
                    verified_response = "The requested repository task completed with no staged source diff.";
                }
                static_cast<void>(learning_service_->submit(memory::Episode{
                    .id = {},
                    .skill = "cpp23-repository",
                    .prompt = task,
                    .response = std::move(verified_response),
                    .model_id = model_id,
                    .score = 0.98,
                    .compiler_passed = true,
                    .tests_passed = true,
                    .human_accepted = false,
                    .human_rejected = false,
                    .deterministic_verified = true,
                    .reference_verified = false,
                    .self_generated = true,
                    .eligible_for_training = false,
                    .verifier_model_id = "compiler+tests",
                    .verification_notes = "Repository build and tests passed.",
                    .provenance = "compiler-and-test-verified-trajectory",
                    .knowledge_destination = "lora-incubation",
                    .content_hash = {},
                    .correction = {},
                    .tool_trace_hash = result.trajectory_path.generic_string(),
                    .created_unix_ms = 0,
                }));
            }
            const auto operation = result.operation;
            {
                std::lock_guard lock(result_mutex_);
                pending_long_horizon_result_ = std::move(result);
            }
            return operation;
        },
        "generating the initial repository patch"));
}

void MainWindow::cancel_long_horizon() {
    if (!long_horizon_operation_.running()) {
        set_status(L"No autonomous coding loop is running");
        return;
    }
    long_horizon_operation_.cancel();
    set_status(L"Cancellation requested; rollback will occur at the next safe boundary");
}

void MainWindow::update_long_horizon_result() {
    std::optional<long_horizon::Result> result;
    {
        std::lock_guard lock(result_mutex_);
        if (pending_long_horizon_result_.has_value()) {
            result = std::move(pending_long_horizon_result_);
            pending_long_horizon_result_.reset();
        }
    }
    if (!result.has_value()) {
        return;
    }

    std::wostringstream report;
    report << L"State: " << widen(util::to_string(result->operation.state))
           << L"\r\n" << widen(result->operation.message)
           << L"\r\nIterations: " << result->iterations
           << L"\r\nCompiler passed: " << (result->compiler_passed ? L"yes" : L"no")
           << L"\r\nTests passed: " << (result->tests_passed ? L"yes" : L"no")
           << L"\r\nRun directory: " << result->run_directory.wstring()
           << L"\r\n\r\nTrajectory\r\n";
    for (const auto& event : result->events) {
        report << L"  [" << event.iteration << L"] " << widen(event.phase)
               << L"  exit=" << event.exit_code
               << L"  " << event.duration.count() << L" ms\r\n";
    }
    if (!result->final_diff.empty()) {
        report << L"\r\nVerified staged diff\r\n" << widen(result->final_diff);
    }
    const auto report_text = report.str();
    set_control_text(id::horizon_report, report_text);
    if (long_horizon_from_coding_) {
        workspace_transcript_ += "\r\nREPOSITORY RUN RESULT\r\n\r\n" + narrow(report_text) + "\r\n";
        set_control_text(id::chat_response, widen(workspace_transcript_));
        last_response_surface_ = RequestSurface::workspace_coding;
        last_response_ = result->final_diff.empty()
            ? result->operation.message
            : result->final_diff;
        const auto coding_model = best_coding_model();
        last_model_id_ = coding_model.has_value() ? coding_model->id : "coding-agent";
        routing::TaskAnalyzer analyzer;
        last_analysis_ = analyzer.analyze(last_prompt_ + " repository implementation");
    }
    long_horizon_from_coding_ = false;
    if (!result->trajectory_path.empty()) {
        set_active_log(result->trajectory_path);
    }
    set_status(widen(result->operation.message));
    update_skill_report();
    update_learning_report();
}

void MainWindow::schedule_lifecycle_retry(std::string message) {
    lifecycle_last_error_ = std::move(message);
    if (!automatic_lifecycle_enabled_) {
        return;
    }
    constexpr std::array delays{
        std::chrono::seconds(5),
        std::chrono::seconds(15),
        std::chrono::seconds(30),
        std::chrono::seconds(60),
        std::chrono::seconds(120),
        std::chrono::seconds(300),
    };
    const auto index = std::min(lifecycle_failures_, delays.size() - 1U);
    lifecycle_retry_at_ = std::chrono::steady_clock::now() + delays[index];
    ++lifecycle_failures_;
}

void MainWindow::handle_lifecycle_completion() {
    if (lifecycle_action_ == LifecycleAction::none) {
        return;
    }
    const auto result = backend_operation_.snapshot();
    if (result.state == util::OperationState::running ||
        result.state == util::OperationState::idle) {
        return;
    }

    const auto completed_action = lifecycle_action_;
    lifecycle_action_ = LifecycleAction::none;
    if (result.state == util::OperationState::succeeded) {
        lifecycle_failures_ = 0U;
        lifecycle_last_error_.clear();
        lifecycle_retry_at_ = std::chrono::steady_clock::now();
        if (completed_action == LifecycleAction::verify_backend) {
            backend_verified_ = true;
            backend_repair_required_ = false;
        } else if (completed_action == LifecycleAction::install_backend) {
            // File replacement is isolated. The automatic coordinator may check and
            // continue setup on a later tick, after this transaction has completed.
            backend_verified_ = false;
            backend_repair_required_ = false;
            selected_python_ = paths_.python;
            learning_service_->set_python_executable(selected_python_);
            update_storage_report(true);
        }
        return;
    }

    if (completed_action == LifecycleAction::verify_backend) {
        backend_verified_ = false;
        backend_repair_required_ = true;
    }

    auto detail = (
        result.state == util::OperationState::cancelled &&
        !lifecycle_last_error_.empty())
        ? lifecycle_last_error_
        : result.message;
    const auto progress = runtime::read_internal_model_progress(paths_);
    if (completed_action == LifecycleAction::bootstrap_model &&
        progress.available &&
        (progress.phase == "failed" ||
         progress.phase == "rejected" ||
         progress.phase == "repair-required")) {
        if (!progress.message.empty()) {
            detail = progress.message;
        }
        if (!progress.error.empty() && progress.error != progress.message) {
            if (!detail.empty()) {
                detail += ": ";
            }
            detail += progress.error;
        }
    }
    if (detail.empty()) {
        detail = result.state == util::OperationState::cancelled
            ? "internal-model operation was cancelled"
            : "internal-model operation failed without an error message";
    }
    set_status(widen(detail));
    schedule_lifecycle_retry(std::move(detail));
}

void MainWindow::drive_automatic_lifecycle() {
    handle_lifecycle_completion();

    const auto state = runtime::inspect_internal_model_state(paths_);
    if (bootstrap_priority_lease_ && state.bootstrap_complete) {
        learning_service_->set_interactive_busy(false);
        bootstrap_priority_lease_ = false;
    }

    if (backend_operation_.running()) {
        if (automatic_lifecycle_enabled_ &&
            lifecycle_action_ == LifecycleAction::bootstrap_model) {
            const auto progress = runtime::read_internal_model_progress(paths_);
            if (progress.available && progress.heartbeat_unix_ms > 0) {
                const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                constexpr std::int64_t maximum_silent_ms = 15LL * 60LL * 1000LL;
                if (now_ms > progress.heartbeat_unix_ms + maximum_silent_ms) {
                    lifecycle_last_error_ =
                        "Built-in AI setup stopped reporting progress and will resume "
                        "from its last saved point.";
                    backend_operation_.cancel();
                }
            }
        }
        return;
    }
    if (lifecycle_action_ != LifecycleAction::none ||
        !automatic_lifecycle_enabled_ ||
        std::chrono::steady_clock::now() < lifecycle_retry_at_) {
        return;
    }

    // Setup yields to anything the user is doing. Installing files, teaching the
    // local model, and starting its helper never interrupt an active task.
    const auto reserved_bootstrap_lease = bootstrap_priority_lease_ ? 1U : 0U;
    if (chat_operation_.running() || media_operation_.running() ||
        long_horizon_operation_.running() || piggyback_operation_.running() ||
        learning_service_->interactive_lease_count() > reserved_bootstrap_lease) {
        return;
    }
    const auto learning_pipeline = learning_service_->pipeline_snapshot();
    if (learning_pipeline.active_job.has_value()) {
        return;
    }

    auto selected_paths = paths_;
    selected_paths.python = selected_python_;
    if (backend_repair_required_ || !runtime::backend_files_present(selected_paths)) {
        if (automatic_training_pack_source(paths_, selected_pack_source_).has_value()) {
            install_backend(true);
        }
        return;
    }
    if (!backend_verified_) {
        verify_backend(true);
        return;
    }

    if (!state.manifest_valid || !state.bootstrap_complete) {
        if (automatic_learning_limit_reached(paths_, state)) {
            lifecycle_last_error_ =
                "Built-in AI reached its bounded learning target without passing every quality check; "
                "no unapproved model was activated.";
            return;
        }
        create_internal_model(true, true);
        return;
    }
    if (!backend_server_.running()) {
        start_backend(true);
    }
}
void MainWindow::update_internal_model_lifecycle_ui() {
    const auto state = runtime::inspect_internal_model_state(paths_);
    const auto progress = runtime::read_internal_model_progress(paths_);
    set_control_text(id::models_internal_status, internal_model_status_text(paths_));

    double percent = 0.0;
    const auto target = runtime::default_bootstrap_target(state);
    if (state.bootstrap_complete) {
        percent = 100.0;
    } else if (lifecycle_action_ == LifecycleAction::bootstrap_model && progress.available) {
        percent = progress.percent;
    } else if (state.manifest_valid && target > 0U) {
        percent = 100.0 * static_cast<double>(state.steps) / static_cast<double>(target);
    }
    percent = std::clamp(percent, 0.0, 100.0);
    static_cast<void>(::SendMessageW(
        control(id::models_progress),
        PBM_SETPOS,
        static_cast<WPARAM>(std::lround(percent * 10.0)),
        0));

    std::wostringstream detail;
    const auto now = std::chrono::steady_clock::now();
    auto selected_paths = paths_;
    selected_paths.python = selected_python_;
    const auto setup_files_ready = runtime::backend_files_present(selected_paths);
    const auto setup_source_ready =
        automatic_training_pack_source(paths_, selected_pack_source_).has_value();
    const auto show_progress =
        backend_operation_.running() || state.exists || setup_source_ready;
    static_cast<void>(::ShowWindow(
        control(id::models_progress),
        show_progress ? SW_SHOW : SW_HIDE));

    if (!automatic_lifecycle_enabled_) {
        detail << L"Automatic setup is paused. Backup and repair options can turn it back on.";
    } else if (now < lifecycle_retry_at_ && !lifecycle_last_error_.empty()) {
        const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            lifecycle_retry_at_ - now).count();
        detail << L"ThreadSpire will try again in "
               << std::max<std::int64_t>(0, remaining)
               << L" seconds. Technical details are under Backup and repair.";
    } else if (lifecycle_action_ == LifecycleAction::bootstrap_model && progress.available) {
        detail << L"Teaching the built-in AI";
        const auto completed = progress.total_steps > 0U
            ? progress.total_steps
            : progress.prior_total_steps + progress.step;
        const auto progress_target = progress.bootstrap_target_steps > 0U
            ? progress.bootstrap_target_steps
            : target;
        if (progress_target > 0U) {
            detail << L" • " << completed << L"/" << progress_target;
        } else if (progress.maximum_steps > 0U) {
            detail << L" • " << progress.step << L"/" << progress.maximum_steps;
        }
        if (progress.elapsed_seconds > 0.0) {
            detail << L" • elapsed " << format_duration(progress.elapsed_seconds);
        }
        if (progress.eta_seconds.has_value()) {
            detail << L" • about " << format_duration(*progress.eta_seconds) << L" remaining";
        }
        detail << L" • this training copy cannot answer until every check passes";
        if (progress.stale) {
            detail << L" • progress is delayed; the saved work remains safe";
        }
    } else if (backend_operation_.running()) {
        switch (lifecycle_action_) {
        case LifecycleAction::install_backend:
            detail << L"Preparing the optional built-in AI files…";
            break;
        case LifecycleAction::verify_backend:
            detail << L"Checking the built-in AI…";
            break;
        case LifecycleAction::start_host:
            detail << L"Starting the built-in AI…";
            break;
        case LifecycleAction::bootstrap_model:
            detail << L"Teaching the built-in AI…";
            break;
        case LifecycleAction::none:
            detail << L"ThreadSpire is checking what is ready…";
            break;
        }
    } else if (state.bootstrap_complete && backend_server_.running()) {
        detail << L"Built-in AI is ready and running. ThreadSpire will keep it available automatically.";
    } else if (automatic_learning_limit_reached(paths_, state)) {
        detail << L"Built-in AI reached its quality limit. No unapproved result was activated; Chat can keep using connected AI.";
    } else if (!setup_files_ready && setup_source_ready) {
        detail << L"Built-in AI files were found. ThreadSpire will finish setup automatically.";
    } else if (!setup_files_ready) {
        detail << L"No setup is running because the built-in AI package is not installed. Choose Automatic yourself to use another ready AI.";
    } else if (!backend_verified_) {
        detail << L"ThreadSpire is checking the built-in AI before using it.";
    } else if (!state.bootstrap_complete) {
        detail << L"ThreadSpire will continue built-in AI learning automatically when the computer is idle.";
    } else {
        detail << L"ThreadSpire is starting the built-in AI automatically.";
    }
    set_control_text(id::models_progress_detail, detail.str());
}
void MainWindow::update_periodic_state() {
    update_chat_result();
    update_media_result();
    update_discovery_result();
    update_mcp_result();
    update_piggyback_result();
    update_long_horizon_result();
    update_storage_report();
    drive_automatic_lifecycle();
    update_internal_model_lifecycle_ui();

    const auto backend_result = backend_operation_.snapshot();
    set_control_text(
        id::models_status,
        widen(std::string(util::to_string(backend_result.state)) + ": " + backend_result.message));

    const auto backend_running = backend_server_.running();
    const auto bootstrap_running =
        lifecycle_action_ == LifecycleAction::bootstrap_model &&
        backend_operation_.running();
    if (backend_running != backend_running_last_ ||
        bootstrap_running != bootstrap_running_last_) {
        backend_running_last_ = backend_running;
        bootstrap_running_last_ = bootstrap_running;
        refresh_model_selectors();
    }

    models::ModelRegistry snapshot;
    {
        std::lock_guard lock(model_mutex_);
        snapshot = model_registry_;
    }
    const auto summary = runtime_summary(snapshot, backend_running);
    const auto teacher_available = has_available_teacher(runtime_model_snapshot());
    const auto learning_pipeline = learning_service_->pipeline_snapshot();
    const auto weight_update_busy =
        learning_pipeline.active_job.has_value() ||
        learning_pipeline.queued_updates > 0U;
    set_control_text(id::chat_backend, summary);
    set_control_text(id::coding_backend, summary);

    if (mcp_client_->connected()) {
        std::wostringstream mcp_status;
        mcp_status << L"Optional tools: connected — " << mcp_client_->tools().size()
                   << L" available";
        set_control_text(id::chat_mcp_status, mcp_status.str());
    } else {
        set_control_text(
            id::chat_mcp_status,
            L"Optional tools are not connected. Set them up under AI Setup only if needed.");
    }

    if (chat_operation_.running()) {
        set_control_text(
            id::chat_learning_status,
            L"Answering now. Automatic learning is waiting.");
    } else if (media_operation_.running()) {
        set_control_text(
            id::chat_learning_status,
            L"Creating media now. Automatic learning is waiting.");
    } else if (piggyback_operation_.running()) {
        set_control_text(
            id::chat_learning_status,
            L"Improving the built-in AI automatically from approved examples. Chat remains available.");
    } else if (learning_pipeline.active_job.has_value()) {
        set_control_text(
            id::chat_learning_status,
            L"Improving the built-in AI in the background. You can keep using Chat.");
    } else if (learning_pipeline.queued_updates > 0U) {
        std::wostringstream queued;
        queued << L"Automatic learning has "
               << learning_pipeline.queued_updates << L" saved improvement"
               << (learning_pipeline.queued_updates == 1U ? L"" : L"s")
               << L" waiting.";
        set_control_text(id::chat_learning_status, queued.str());
    } else if (learning_service_->paused()) {
        set_control_text(id::chat_learning_status, L"Automatic learning is paused.");
    } else if (!checked(id::learning_auto)) {
        set_control_text(
            id::chat_learning_status,
            L"Automatic learning is off.");
    } else if (!teacher_available) {
        set_control_text(
            id::chat_learning_status,
            L"Automatic learning is on and waiting for another AI to help create examples.");
    } else {
        set_control_text(
            id::chat_learning_status,
            L"Automatic learning is on and will run only while the app is idle.");
    }

    if (checked(id::learning_auto) &&
        teacher_available &&
        !learning_service_->paused() &&
        !piggyback_operation_.running() &&
        !weight_update_busy &&
        !chat_operation_.running() &&
        !long_horizon_operation_.running() &&
        !media_operation_.running() &&
        std::chrono::steady_clock::now() >= next_piggyback_at_) {
        generate_piggyback(true);
    }

    if (page_ == Page::learning) {
        update_learning_report();
    } else if (page_ == Page::skills) {
        update_skill_report();
    } else if (page_ == Page::long_horizon && long_horizon_operation_.running()) {
        const auto state = long_horizon_operation_.snapshot();
        set_control_text(
            id::horizon_report,
            widen(std::string(util::to_string(state.state)) + ": " + state.message));
    } else if (page_ == Page::logs) {
        update_log_view();
    }
}

void MainWindow::update_chat_result() {
    std::optional<backend::AgentResult> result;
    RequestSurface surface = RequestSurface::assistant;
    {
        std::lock_guard lock(result_mutex_);
        if (!chat_result_consumed_ && pending_chat_result_.has_value()) {
            result = std::move(pending_chat_result_);
            pending_chat_result_.reset();
            if (pending_chat_surface_.has_value()) {
                surface = *pending_chat_surface_;
                pending_chat_surface_.reset();
            }
            chat_result_consumed_ = true;
        }
    }
    if (!result.has_value()) {
        return;
    }

    flush_stream_text();
    const auto workspace = is_workspace_surface(surface);
    const auto plan_id = workspace ? id::chat_plan : id::coding_plan;
    const auto response_id = workspace ? id::chat_response : id::coding_response;
    const auto route_models = runtime_model_snapshot();
    if (workspace) {
        set_control_text(id::chat_plan_label, L"How it ran");
    }
    set_control_text(plan_id, format_plan(result->plan, &route_models));
    last_analysis_ = result->plan.analysis;
    last_response_surface_ = surface;

    if (!result->succeeded) {
        if (workspace) {
            if (pending_transcript_heading_offset_ != std::string::npos) {
                const auto error_heading = std::string{"ERROR — AI COULD NOT ANSWER"};
                workspace_transcript_.replace(
                    pending_transcript_heading_offset_,
                    pending_transcript_heading_length_,
                    error_heading);
            }
            pending_transcript_heading_offset_ = std::string::npos;
            pending_transcript_heading_length_ = 0;
            workspace_transcript_ += result->error.empty()
                ? "The local model request failed without an error message."
                : result->error;
            workspace_transcript_ += "\r\n\r\n";
            set_control_text(response_id, widen(workspace_transcript_));
        } else {
            set_control_text(
                response_id,
                widen("Error: " + (
                    result->error.empty()
                        ? std::string{"local model request failed"}
                        : result->error)));
        }
        set_control_text(
            id::chat_learning_status,
            L"Foreground request ended. Background learning may resume.");
        set_status(result->error == "generation cancelled"
            ? L"AI request cancelled"
            : L"The selected AI could not answer");
        return;
    }

    last_response_ = result->response;
    conversation_history_.emplace_back(last_prompt_, last_response_);
    constexpr std::size_t maximum_conversation_turns = 8U;
    if (conversation_history_.size() > maximum_conversation_turns) {
        conversation_history_.erase(
            conversation_history_.begin(),
            conversation_history_.begin() +
                static_cast<std::ptrdiff_t>(
                    conversation_history_.size() - maximum_conversation_turns));
    }
    last_models_used_ = result->models_used;
    last_tool_events_ = result->tool_events;
    last_model_id_ = result->models_used.empty()
        ? "unknown"
        : result->models_used.back();

    std::wstring execution_details;
    if (last_model_id_ == "verified-memory") {
        execution_details = L"Used: built-in verified knowledge";
    } else if (const auto* model = route_models.find(last_model_id_); model != nullptr) {
        execution_details = models::is_internal_model(*model)
            ? L"Used: built-in AI — "
            : L"Used: another AI app — ";
        execution_details += widen(model->display_name);
    } else {
        execution_details = widen(actual_runtime_heading(last_model_id_));
    }
    if (std::ranges::any_of(
            result->plan.stages,
            [](const routing::RouteStage& stage) {
                return stage.role == "automatic-fallback";
            })) {
        execution_details += L"\r\nAutomatic switched after the first AI could not answer.";
    }

    if (workspace) {
        if (pending_transcript_heading_offset_ != std::string::npos) {
            const auto actual_heading = actual_runtime_heading(last_model_id_);
            workspace_transcript_.replace(
                pending_transcript_heading_offset_,
                pending_transcript_heading_length_,
                actual_heading);
        }
        pending_transcript_heading_offset_ = std::string::npos;
        pending_transcript_heading_length_ = 0;
        workspace_transcript_ += result->response;
        workspace_transcript_ += "\r\n\r\n";
        set_control_text(response_id, widen(workspace_transcript_));
    } else {
        set_control_text(response_id, widen(result->response));
    }

    if (!result->tool_events.empty()) {
        execution_details += L"\r\n\r\nOptional tool activity\r\n";
        for (const auto& event : result->tool_events) {
            execution_details += L"• " + widen(event) + L"\r\n";
        }
    }
    set_control_text(plan_id, std::move(execution_details));

    if (!interactive_model_verified_) {
        interactive_model_verified_ = true;
        try {
            write_text(
                paths_.root / "inference-verified.flag",
                "A local model completed a foreground response successfully.\n");
        } catch (...) {
        }
    }
    next_piggyback_at_ =
        std::chrono::steady_clock::now() + std::chrono::minutes(2);

    std::wostringstream status;
    status << L"Reply complete";
    if (last_model_id_ == "verified-memory") {
        status << L" • built-in verified knowledge";
    } else if (const auto* model = route_models.find(last_model_id_); model != nullptr) {
        status << (models::is_internal_model(*model)
            ? L" • ThreadSpire internal model: "
            : L" • Connected local LLM: ")
               << widen(model->display_name);
    } else if (!last_models_used_.empty()) {
        status << L" • " << widen(last_models_used_.back());
    }
    if (last_model_id_ != "verified-memory") {
        status << L" • " << result->effective_tokens_per_second << L" effective tokens/s";
    }
    if (result->models_used.size() > 1U) {
        status << L" • " << result->models_used.size() << L" models used";
    }
    set_status(status.str());
    set_control_text(
        id::chat_learning_status,
        checked(id::learning_auto)
            ? L"Reply verified. Automatic learning is armed and will run while idle."
            : L"Reply verified. Automatic teacher-data generation is disabled.");
}

void MainWindow::update_media_result() {
    std::optional<backend::MediaResult> result;
    {
        std::lock_guard lock(result_mutex_);
        if (pending_media_result_.has_value()) {
            result = std::move(pending_media_result_);
            pending_media_result_.reset();
        }
    }
    if (!result.has_value()) {
        return;
    }
    media_result_pending_ = false;
    if (!result->succeeded) {
        workspace_transcript_ += "ERROR — MEDIA GENERATION\r\n\r\n" +
            (result->error.empty() ? std::string{"The media endpoint failed."} : result->error) +
            "\r\n";
        set_control_text(id::chat_response, widen(workspace_transcript_));
        set_status(result->error == "media generation cancelled"
            ? L"Media generation cancelled"
            : L"Media generation failed — details are shown in Task");
        return;
    }

    const auto kind = result->kind == backend::MediaKind::image ? "IMAGE" : "AUDIO";
    workspace_transcript_ += std::string{"OUTPUT — "} + kind + "\r\n\r\n" +
        result->output_path.string() + "\r\n";
    set_control_text(id::chat_response, widen(workspace_transcript_));
    set_control_text(
        id::chat_plan,
        widen("Completed\r\nModel: " + result->model_id +
            "\r\nMIME type: " + result->mime_type +
            "\r\nSaved file: " + result->output_path.string()));
    last_response_.clear();
    set_status(result->kind == backend::MediaKind::image
        ? L"Image generated and saved; select Open outputs to view it"
        : L"Speech generated and saved; select Open outputs to play it");
    update_storage_report(true);
}

void MainWindow::update_discovery_result() {
    std::optional<models::ProviderDiscoveryReport> report;
    {
        std::lock_guard lock(result_mutex_);
        if (pending_discovery_report_.has_value()) {
            report = std::move(pending_discovery_report_);
            pending_discovery_report_.reset();
        }
    }
    if (!report.has_value()) {
        return;
    }

    {
        std::lock_guard lock(model_mutex_);
        static_cast<void>(models::merge_discovered_models(model_registry_, report->models));
        try {
            model_registry_.save(paths_.model_registry);
        } catch (...) {
        }
    }
    load_models_editor();
    refresh_model_selectors();

    if (checked(id::learning_auto) &&
        !learning_service_->paused() &&
        has_available_teacher(runtime_model_snapshot())) {
        next_piggyback_at_ =
            std::chrono::steady_clock::now() + std::chrono::seconds(30);
    }

    std::wostringstream text;
    if (report->models.empty()) {
        text << L"No local provider answered. You can add an endpoint manually below.";
    } else {
        text << report->models.size() << L" model"
             << (report->models.size() == 1U ? L"" : L"s")
             << L" connected";
    }
    for (const auto& diagnostic : report->diagnostics) {
        text << L" • " << widen(diagnostic);
    }
    set_control_text(id::models_discovery_status, text.str());
    set_status(report->models.empty()
        ? L"No local model server found"
        : L"Connected local AI is ready for compatible Task capabilities");
}

void MainWindow::update_mcp_result() {
    std::optional<mcp::ConnectionResult> result;
    {
        std::lock_guard lock(result_mutex_);
        if (pending_mcp_result_.has_value()) {
            result = std::move(pending_mcp_result_);
            pending_mcp_result_.reset();
        }
    }
    if (!result.has_value()) {
        return;
    }
    if (!result->succeeded) {
        set_control_text(id::models_mcp_status, widen("Failed: " + result->error));
        set_control_text(
            id::chat_mcp_status,
            widen("MCP tools: connection failed — " + result->error));
        set_status(L"MCP connection failed");
        return;
    }
    std::wostringstream status;
    status << widen(result->server_name.empty() ? "MCP server" : result->server_name)
           << L" connected • " << result->tools.size() << L" tool"
           << (result->tools.size() == 1U ? L"" : L"s");
    set_control_text(id::models_mcp_status, status.str());
    set_control_text(id::chat_mcp_status, L"Optional tools: " + status.str());
    try {
        write_text(
            paths_.root / "config" / "mcp-endpoint.txt",
            narrow(control_text(id::models_mcp_endpoint)));
    } catch (...) {
    }
    set_status(L"MCP tools are available to capable local models");
}

void MainWindow::update_piggyback_result() {
    std::optional<learning::PiggybackResult> result;
    {
        std::lock_guard lock(result_mutex_);
        if (pending_piggyback_result_.has_value()) {
            result = std::move(pending_piggyback_result_);
            pending_piggyback_result_.reset();
        }
    }
    if (!result.has_value()) {
        const auto operation = piggyback_operation_.snapshot();
        if (operation.state == util::OperationState::running) {
            set_control_text(id::learning_piggyback_status, widen(operation.message));
        }
        return;
    }
    const auto trusted_only = active_piggyback_trusted_only_;
    if (!trusted_only && !active_piggyback_cursor_key_.empty()) {
        const auto current = piggyback_cursor_for(active_piggyback_cursor_key_);
        if (result->progress.next_seed_offset > current) {
            persist_piggyback_cursor(
                active_piggyback_cursor_key_,
                result->progress.next_seed_offset);
        }
    }
    std::wostringstream status;
    status << L"Target "
           << widen(result->progress.target_skill.empty()
               ? active_piggyback_skill_
               : result->progress.target_skill)
           << L" • examined " << result->progress.examined
           << L" • trusted " << result->progress.trusted_seeded
           << L" • generated " << result->progress.generated
           << L" • independently verified " << result->progress.verified
           << L" • stored " << result->progress.stored
           << L" • rejected " << result->progress.rejected;
    if (!result->error.empty()) {
        status << L" • " << widen(result->error);
    }
    set_control_text(id::learning_piggyback_status, status.str());
    const auto now = std::chrono::steady_clock::now();
    const auto trainable_evidence =
        result->progress.trusted_seeded + result->progress.verified;
    if (result->succeeded && trainable_evidence > 0U) {
        // Let the coalesced weight candidate drain before another teacher pass.
        next_piggyback_at_ = now + std::chrono::minutes(10);
    } else if (result->succeeded) {
        next_piggyback_at_ = now + std::chrono::minutes(2);
    } else {
        next_piggyback_at_ = now + std::chrono::minutes(5);
    }
    set_status(result->succeeded
        ? (trusted_only
            ? L"Trusted custom JSONL import completed"
            : L"Local teacher-data pass completed")
        : (trusted_only
            ? L"Trusted custom JSONL import did not complete"
            : L"Local teacher-data pass did not complete"));
    active_piggyback_skill_.clear();
    active_piggyback_cursor_key_.clear();
    active_piggyback_trusted_only_ = false;
    update_learning_report();
    update_skill_report();
}

void MainWindow::update_skill_report() {
    const auto modules = learning_service_->expert_snapshot();
    std::wostringstream report;
    report << L"Skill modules\r\n"
           << L"Accepted memory is immediate. LoRA is the fast candidate layer; promoted full-rank modules remain independently versioned.\r\n\r\n";
    if (modules.empty()) {
        report << L"No skill has accepted evidence yet. Accept a verified Task response or use local teacher generation.";
    }
    for (const auto& module : modules) {
        report << L"[" << widen(module.skill) << L"]\r\n"
               << L"  state: " << widen(learning::to_string(module.state)) << L"\r\n"
               << L"  base: " << widen(module.base_model) << L"\r\n"
               << L"  accepted episodes: " << module.accepted_episodes << L"\r\n"
               << L"  since LoRA update: " << module.episodes_since_update << L"\r\n"
               << L"  active generation: " << module.generation << L"\r\n"
               << L"  LoRA generations: " << module.lora_generations << L"\r\n"
               << L"  compiler verified: " << module.compiler_verified_episodes
               << L"  tests verified: " << module.test_verified_episodes << L"\r\n"
               << L"  validation: " << module.validation_score
               << L"  regression: " << module.regression_score << L"\r\n"
               << L"  promoted candidate: " << widen(module.promoted_candidate_id) << L"\r\n"
               << L"  adapter: " << module.adapter_path.wstring() << L"\r\n"
               << L"  full expert: " << module.module_path.wstring() << L"\r\n"
               << L"  rollback state: " << widen(learning::to_string(module.previous_state))
               << L"\r\n\r\n";
    }
    set_control_text(id::skills_report, report.str());
}

void MainWindow::update_training_library() {
    const auto query = trim_line_endings(narrow(control_text(id::learning_library_filter)));
    const auto selected = static_cast<int>(::SendMessageW(
        control(id::learning_library_source),
        CB_GETCURSEL,
        0,
        0));
    try {
        std::wstring summary;
        auto text = training_library_text(paths_, query, std::max(0, selected), summary);
        set_control_text(id::learning_library_header, std::move(summary));
        set_control_text(id::learning_library, std::move(text));
    } catch (const std::exception& error) {
        set_control_text(
            id::learning_library_header,
            L"Training Library — could not read the accepted examples");
        set_control_text(
            id::learning_library,
            widen("ThreadSpire could not build this view: " + std::string(error.what())));
    }
}
void MainWindow::update_learning_report() {
    const auto statuses = learning_service_->statuses();
    const auto candidates = learning_service_->candidate_snapshot();
    const auto modules = learning_service_->expert_snapshot();
    const auto settings = learning_service_->settings_snapshot();
    const auto pipeline = learning_service_->pipeline_snapshot();
    const auto foundry = piggyback_operation_.snapshot();
    const auto automatic_teacher = checked(id::learning_auto);
    const auto teacher_available = has_available_teacher(runtime_model_snapshot());
    const auto now = std::chrono::steady_clock::now();
    const auto selected_skill = selected_training_skill();
    const auto selected_dataset = selected_training_dataset();
    const auto selected_evidence =
        learning_service_->memory_store().training_count_for_skill(selected_skill);
    std::wostringstream report;
    report << L"SELECTED TRAINING TARGET: " << widen(selected_skill)
           << L"\r\nTrainable verified evidence for this target: " << selected_evidence
           << L"\r\nAutomatic source: bundled seed.jsonl, filtered to this target"
           << L"\r\nCustom import file: " << selected_dataset.wstring()
           << L"\r\nAutomatic mode never changes the selected target.\r\n\r\n"
           << L"Background learning: "
           << (learning_service_->paused() ? L"paused" : L"enabled")
           << L"\r\nInference priority: "
           << (learning_service_->interactive_busy()
               ? L"active — trainer yields at safe boundaries"
               : L"idle")
           << L"\r\nAutomatic teacher loop: "
           << (automatic_teacher ? L"enabled" : L"disabled")
           << L"\r\nTeacher model: "
           << (teacher_available ? L"external local teacher ready" : L"waiting for an external local teacher")
           << L"\r\nActive example pass target: "
           << (active_piggyback_skill_.empty() ? L"none" : widen(active_piggyback_skill_))
           << L"\r\nTeacher foundry: "
           << widen(std::string(util::to_string(foundry.state)));
    if (!foundry.message.empty()) {
        report << L" — " << widen(foundry.message);
    }
    report << L"\r\nWeight updates queued: " << pipeline.queued_updates
           << L"\r\nPromotion cadence: every "
           << std::max<std::size_t>(1U, settings.policy.lora_microbatch)
           << L" independently accepted training episode"
           << (settings.policy.lora_microbatch == 1U ? L"" : L"s")
           << L"\r\n\r\n";

    if (automatic_teacher && teacher_available && !piggyback_operation_.running()) {
        if (now >= next_piggyback_at_) {
            report << L"Next verified teacher pass: ready as soon as inference and weight training are idle.\r\n\r\n";
        } else {
            const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                next_piggyback_at_ - now).count();
            report << L"Next verified teacher pass: about "
                   << std::max<std::int64_t>(1, remaining + 1)
                   << L" second(s), provided the foreground remains idle.\r\n\r\n";
        }
    }

    report << L"Evidence awaiting weight updates\r\n";
    if (modules.empty()) {
        report << L"  No accepted skill evidence yet.\r\n";
    } else {
        const auto threshold = std::max<std::size_t>(1U, settings.policy.lora_microbatch);
        for (const auto& module : modules) {
            report << L"  " << widen(module.skill)
                   << L": " << module.episodes_since_update << L"/" << threshold
                   << L" eligible episode" << (module.episodes_since_update == 1U ? L"" : L"s")
                   << L" since the last promoted update; "
                   << module.accepted_episodes << L" accepted total.\r\n";
        }
    }

    if (pipeline.active_job.has_value()) {
        const auto& active = *pipeline.active_job;
        const auto progress = read_learning_progress(active.progress_path);
        report << L"\r\nACTIVE WEIGHT UPDATE\r\n"
               << L"  state: " << widen(learning::to_string(active.state)) << L"\r\n"
               << L"  skill: " << widen(active.skill) << L"\r\n"
               << L"  candidate: " << widen(active.candidate_id) << L"\r\n"
               << L"  status: " << widen(active.message) << L"\r\n";
        if (progress.available) {
            report << L"  phase: " << widen(progress.phase);
            if (!progress.message.empty() && progress.message != progress.phase) {
                report << L" — " << widen(progress.message);
            }
            report << L"\r\n";
            if (progress.maximum_steps > 0U) {
                report << L"  progress: step " << progress.step << L"/"
                       << progress.maximum_steps << L" • "
                       << std::fixed << std::setprecision(1) << progress.percent << L"%\r\n";
            }
            if (progress.loss.has_value()) {
                report << L"  loss: " << std::setprecision(6) << *progress.loss << L"\r\n";
            }
            if (progress.tokens_seen > 0U) {
                report << L"  verified target tokens: " << progress.tokens_seen << L"\r\n";
            }
            if (progress.elapsed_seconds > 0.0) {
                report << L"  elapsed: " << format_duration(progress.elapsed_seconds);
                if (progress.eta_seconds.has_value()) {
                    report << L" • ETA " << format_duration(*progress.eta_seconds);
                }
                report << L"\r\n";
            }
        }
        report << L"  progress file: " << active.progress_path.wstring() << L"\r\n"
               << L"  log: " << active.log_path.wstring() << L"\r\n";
        const auto log_tail = read_tail(active.log_path, 12U * 1024U);
        if (!log_tail.empty()) {
            report << L"\r\n  Live trainer log tail\r\n"
                   << widen(log_tail) << L"\r\n";
        }
        report << L"\r\n";
    }

    report << L"Recent weight jobs\r\n";
    if (statuses.empty()) {
        report << L"  No weight jobs yet. Accepted and verified memories remain immediately retrievable.\r\n";
    }
    const auto first_status = statuses.size() > 20U ? statuses.size() - 20U : 0U;
    for (auto index = statuses.size(); index > first_status; --index) {
        const auto& status = statuses[index - 1U];
        report << L"  " << widen(status.id) << L"  " << widen(learning::to_string(status.state))
               << L"\r\n    skill: " << widen(status.skill)
               << L"\r\n    candidate: " << widen(status.candidate_id)
               << L"\r\n    " << widen(status.message)
               << L"\r\n    log: " << status.log_path.wstring() << L"\r\n\r\n";
    }

    report << L"Candidate ledger (latest 20)\r\n";
    if (candidates.empty()) {
        report << L"  No staged candidates.\r\n";
    } else {
        const auto begin_index = candidates.size() > 20U ? candidates.size() - 20U : 0U;
        for (auto index = candidates.size(); index > begin_index; --index) {
            const auto& candidate = candidates[index - 1U];
            report << L"  " << widen(candidate.id)
                   << L"  " << widen(learning::to_string(candidate.kind))
                   << L"  " << widen(learning::to_string(candidate.state))
                   << L"  parent=" << candidate.parent_generation
                   << L"\r\n    " << widen(candidate.message) << L"\r\n";
        }
    }
    set_control_text(id::learning_jobs, report.str());
}

void MainWindow::update_storage_report(const bool force) {
    std::optional<runtime::StorageSnapshot> snapshot;
    {
        std::lock_guard lock(result_mutex_);
        if (pending_storage_snapshot_.has_value()) {
            snapshot = std::move(pending_storage_snapshot_);
            pending_storage_snapshot_.reset();
        }
    }
    if (snapshot.has_value()) {
        const auto find_card = [&snapshot](const std::string_view key)
            -> const runtime::StorageCard* {
            const auto found = std::ranges::find(snapshot->cards, key, &runtime::StorageCard::key);
            return found == snapshot->cards.end() ? nullptr : &*found;
        };
        const auto growth_text = [](const std::intmax_t growth) {
            if (growth == 0) {
                return std::wstring{L"no change"};
            }
            const auto magnitude = growth < 0
                ? static_cast<std::uintmax_t>(-(growth + 1)) + 1U
                : static_cast<std::uintmax_t>(growth);
            return std::wstring(growth > 0 ? L"+" : L"-") + format_bytes_compact(magnitude);
        };

        const auto* current_model = find_card("current-model");
        const auto* accepted_data = find_card("accepted-training-data");
        const auto* installed_packs = find_card("installed-packs");
        const auto* outputs_cache = find_card("outputs-cache");
        const auto* total_usage = find_card("total-usage");
        const auto* free_disk = find_card("free-disk");

        std::wostringstream model_card;
        model_card << L"CURRENT MODEL";
        if (!snapshot->active_generation.empty()) {
            model_card << L" — " << widen(snapshot->active_generation);
        }
        model_card << L"\r\n" << format_bytes_compact(
            current_model == nullptr ? snapshot->active_model_bytes : current_model->bytes);
        set_control_text(id::chat_active_model_size, model_card.str());

        std::wostringstream training_card;
        training_card << L"ACCEPTED TRAINING DATA\r\n"
                      << format_bytes_compact(
                          accepted_data == nullptr
                              ? snapshot->training_data_bytes
                              : accepted_data->bytes)
                      << L" • " << snapshot->training_data_files << L" files";
        set_control_text(id::chat_training_data_size, training_card.str());
        set_control_text(
            id::chat_installed_packs_size,
            L"INSTALLED PACKS\r\n" + format_bytes_compact(
                installed_packs == nullptr ? 0U : installed_packs->bytes));
        set_control_text(
            id::chat_outputs_cache_size,
            L"OUTPUTS / CACHE\r\n" + format_bytes_compact(
                outputs_cache == nullptr ? 0U : outputs_cache->bytes));
        set_control_text(
            id::chat_total_usage_size,
            L"TOTAL USAGE\r\n" + format_bytes_compact(
                total_usage == nullptr ? snapshot->total_bytes : total_usage->bytes) +
                L" • " + growth_text(
                    total_usage == nullptr ? snapshot->growth_bytes : total_usage->growth_bytes));
        set_control_text(
            id::chat_free_disk_size,
            L"FREE DISK\r\n" + format_bytes_compact(
                free_disk == nullptr ? snapshot->free_disk_bytes : free_disk->bytes));

        std::wostringstream report;
        report << L"LIVE STORAGE CARDS\r\n";
        for (const auto& card : snapshot->cards) {
            report << L"  " << widen(card.label) << L": " << format_bytes(card.bytes);
            if (card.growth_bytes != 0) {
                report << L" • growth " << growth_text(card.growth_bytes);
            }
            if (card.files > 0U) {
                report << L" • " << card.files << L" files";
            }
            report << L"\r\n";
        }
        report << L"  Legacy/private PyTorch runtime (not training data): "
               << format_bytes(snapshot->runtime_bytes)
               << L"\r\n\r\nModel generations: " << snapshot->generation_directories
               << L" • repository trajectories: " << snapshot->trajectory_directories
               << L"\r\nGrowth since previous sample: " << growth_text(snapshot->growth_bytes)
               << L"\r\n\r\nTOP-LEVEL USAGE\r\n";
        for (const auto& category : snapshot->categories) {
            report << L"  " << widen(category.name) << L": "
                   << format_bytes(category.bytes) << L" • "
                   << category.files << L" files\r\n";
        }
        report << L"\r\nLARGEST DIRECTORIES\r\n";
        for (const auto& directory : snapshot->largest_directories) {
            report << L"  " << directory.relative_path.wstring() << L": "
                   << format_bytes(directory.bytes) << L" • "
                   << directory.files << L" files\r\n";
        }
        report << L"\r\nLARGEST FILES\r\n";
        for (const auto& file : snapshot->largest_files) {
            report << L"  " << file.relative_path.wstring() << L": "
                   << format_bytes(file.bytes) << L"\r\n";
        }
        report << L"\r\nCAPABILITY-PACK PROJECTIONS\r\n";
        for (const auto& pack : snapshot->pack_projections) {
            report << L"  " << widen(pack.name)
                   << L": download " << format_bytes(pack.download_bytes)
                   << L" • installed " << format_bytes(pack.install_bytes)
                   << L" • " << (pack.installed ? L"installed" : L"not installed")
                   << L"\r\n";
        }
        report << L"\r\nSAFE CLEANUP QUOTAS\r\n";
        for (const auto& quota : snapshot->quotas) {
            report << L"  " << widen(quota.name) << L": "
                   << format_bytes(quota.maximum_bytes);
            if (quota.maximum_age_days > 0U) {
                report << L" • " << quota.maximum_age_days << L" days";
            }
            if (quota.minimum_entries_to_keep > 0U) {
                report << L" • keep at least " << quota.minimum_entries_to_keep;
            }
            report << L"\r\n";
        }
        previous_storage_bytes_ = snapshot->total_bytes;
        storage_sample_available_ = true;
        set_control_text(id::logs_storage, report.str());
    }

    const auto now = std::chrono::steady_clock::now();
    if (storage_operation_.running() || (!force && now < next_storage_refresh_)) {
        return;
    }
    next_storage_refresh_ = now + std::chrono::seconds(5);
    if (force) {
        set_status(L"Refreshing application-data sizes in the background…");
    }
    const auto previous_total = storage_sample_available_
        ? std::optional<std::uintmax_t>{previous_storage_bytes_}
        : std::nullopt;
    static_cast<void>(storage_operation_.start(
        [this, previous_total](const std::stop_token token) {
            auto value = runtime::inspect_storage_with_growth(
                paths_.root, previous_total, 8U, 8U, token);
            {
                std::lock_guard lock(result_mutex_);
                pending_storage_snapshot_ = std::move(value);
            }
            return util::OperationResult{
                .state = token.stop_requested()
                    ? util::OperationState::cancelled
                    : util::OperationState::succeeded,
                .exit_code = token.stop_requested() ? 1 : 0,
                .message = token.stop_requested() ? "storage scan cancelled" : "storage scan complete",
            };
        },
        "measuring application-data storage"));
}
void MainWindow::update_log_view() {
    set_control_text(id::logs_view, widen(read_tail(active_log_)));
}

HWND MainWindow::control(const int identifier) const {
    const auto iterator = controls_.find(identifier);
    return iterator == controls_.end() ? nullptr : iterator->second;
}

std::wstring MainWindow::control_text(const int identifier) const {
    return window_text(control(identifier));
}

void MainWindow::set_control_text(const int identifier, std::wstring text) const {
    const auto handle = control(identifier);
    if (handle == nullptr) {
        return;
    }

    const auto edit = is_edit_control(handle);
    if (edit) {
        text = normalize_edit_newlines(text);
    }
    if (window_text(handle) == text) {
        return;
    }

    if (!edit) {
        static_cast<void>(::SetWindowTextW(handle, text.c_str()));
        return;
    }

    const auto viewport = capture_edit_viewport(handle);
    static_cast<void>(::SendMessageW(handle, WM_SETREDRAW, FALSE, 0));
    static_cast<void>(::SetWindowTextW(handle, text.c_str()));
    if (identifier == id::chat_response || identifier == id::coding_response) {
        style_chat_transcript(handle, text);
    }
    restore_edit_viewport(handle, viewport);
    static_cast<void>(::SendMessageW(handle, WM_SETREDRAW, TRUE, 0));
    redraw_control(handle);
}

void MainWindow::append_control_text(const int identifier, const std::wstring_view text) const {
    const auto handle = control(identifier);
    if (handle == nullptr || text.empty()) {
        return;
    }

    if (!is_edit_control(handle)) {
        auto combined = window_text(handle);
        combined.append(text);
        static_cast<void>(::SetWindowTextW(handle, combined.c_str()));
        return;
    }

    const auto viewport = capture_edit_viewport(handle);
    const auto owned = normalize_edit_newlines(text);
    static_cast<void>(::SendMessageW(handle, WM_SETREDRAW, FALSE, 0));
    static_cast<void>(::SendMessageW(
        handle,
        EM_SETSEL,
        static_cast<WPARAM>(-1),
        static_cast<LPARAM>(-1)));
    static_cast<void>(::SendMessageW(
        handle,
        EM_REPLACESEL,
        FALSE,
        reinterpret_cast<LPARAM>(owned.c_str())));
    if (identifier == id::chat_response || identifier == id::coding_response) {
        style_chat_transcript(handle, window_text(handle));
    }
    restore_edit_viewport(handle, viewport);
    static_cast<void>(::SendMessageW(handle, WM_SETREDRAW, TRUE, 0));
    redraw_control(handle);
}

bool MainWindow::checked(const int identifier) const {
    const auto state = ::SendMessageW(control(identifier), BM_GETCHECK, 0, 0);
    return state == static_cast<LRESULT>(BST_CHECKED);
}

void MainWindow::set_checked(const int identifier, const bool value) const {
    static_cast<void>(::SendMessageW(
        control(identifier),
        BM_SETCHECK,
        static_cast<WPARAM>(value ? BST_CHECKED : BST_UNCHECKED),
        0));
}

void MainWindow::apply_font(const HWND control_handle) const {
    if (font_ != nullptr) {
        static_cast<void>(::SendMessageW(
            control_handle,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font_),
            TRUE));
    }
}

void MainWindow::recreate_ui_font() {
    if (font_ != nullptr) {
        static_cast<void>(::DeleteObject(font_));
        font_ = nullptr;
    }

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = static_cast<UINT>(sizeof(metrics));
    if (::SystemParametersInfoForDpi(
            SPI_GETNONCLIENTMETRICS,
            static_cast<UINT>(sizeof(metrics)),
            &metrics,
            0,
            dpi_) != FALSE) {
        font_ = ::CreateFontIndirectW(&metrics.lfMessageFont);
    }

    if (font_ == nullptr) {
        LOGFONTW fallback{};
        fallback.lfHeight = -::MulDiv(10, static_cast<int>(dpi_), 72);
        fallback.lfWeight = FW_NORMAL;
        fallback.lfQuality = CLEARTYPE_QUALITY;
        static_cast<void>(::lstrcpynW(
            fallback.lfFaceName,
            L"Segoe UI",
            LF_FACESIZE));
        font_ = ::CreateFontIndirectW(&fallback);
    }

    for (const auto& [identifier, handle] : controls_) {
        static_cast<void>(identifier);
        apply_font(handle);
        if (is_edit_control(handle)) {
            const auto margin = static_cast<LPARAM>(MAKELONG(
                scaled(6, dpi_),
                scaled(6, dpi_)));
            static_cast<void>(::SendMessageW(
                handle,
                EM_SETMARGINS,
                EC_LEFTMARGIN | EC_RIGHTMARGIN,
                margin));
        }
    }
}

void MainWindow::set_status(std::wstring text) const {
    const auto status = control(id::status);
    if (status != nullptr) {
        static_cast<void>(::SetWindowTextW(status, text.c_str()));
        redraw_control(status);
    }
}

void MainWindow::set_active_log(std::filesystem::path path) {
    active_log_ = std::move(path);
    update_log_view();
}

void MainWindow::open_path(const std::filesystem::path& path) const {
    if (path.empty()) {
        return;
    }
    static_cast<void>(::ShellExecuteW(
        window_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
}

std::optional<std::filesystem::path> MainWindow::choose_python() const {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(::CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    COMDLG_FILTERSPEC filters[]{{L"Python executable", L"python.exe"}, {L"Executables", L"*.exe"}};
    static_cast<void>(dialog->SetFileTypes(2, filters));
    static_cast<void>(dialog->SetTitle(L"Select the Python executable that owns PyTorch"));
    if (FAILED(dialog->Show(window_))) {
        dialog->Release();
        return std::nullopt;
    }
    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item))) {
        dialog->Release();
        return std::nullopt;
    }
    PWSTR raw = nullptr;
    const auto result = item->GetDisplayName(SIGDN_FILESYSPATH, &raw);
    std::optional<std::filesystem::path> path;
    if (SUCCEEDED(result) && raw != nullptr) {
        path = std::filesystem::path(raw);
        ::CoTaskMemFree(raw);
    }
    item->Release();
    dialog->Release();
    return path;
}


std::optional<std::filesystem::path> MainWindow::choose_training_dataset() const {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(::CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }

    COMDLG_FILTERSPEC filters[]{
        {L"JSON Lines training data", L"*.jsonl"},
        {L"JSON files", L"*.json"},
        {L"Text files", L"*.txt"},
        {L"All files", L"*.*"},
    };
    static_cast<void>(dialog->SetFileTypes(
        static_cast<UINT>(std::size(filters)),
        filters));
    static_cast<void>(dialog->SetFileTypeIndex(1));
    static_cast<void>(dialog->SetTitle(L"Select a custom training JSONL file"));
    FILEOPENDIALOGOPTIONS options{};
    static_cast<void>(dialog->GetOptions(&options));
    static_cast<void>(dialog->SetOptions(
        options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM));

    if (FAILED(dialog->Show(window_))) {
        dialog->Release();
        return std::nullopt;
    }
    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item))) {
        dialog->Release();
        return std::nullopt;
    }
    PWSTR raw = nullptr;
    const auto display_result = item->GetDisplayName(SIGDN_FILESYSPATH, &raw);
    std::optional<std::filesystem::path> selected;
    if (SUCCEEDED(display_result) && raw != nullptr) {
        selected = std::filesystem::path(raw);
        ::CoTaskMemFree(raw);
    }
    item->Release();
    dialog->Release();
    return selected;
}

std::optional<std::filesystem::path> MainWindow::choose_directory(
    const std::wstring_view title) const {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(::CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    FILEOPENDIALOGOPTIONS options{};
    static_cast<void>(dialog->GetOptions(&options));
    static_cast<void>(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM));
    const std::wstring owned_title(title);
    static_cast<void>(dialog->SetTitle(owned_title.c_str()));
    if (FAILED(dialog->Show(window_))) {
        dialog->Release();
        return std::nullopt;
    }
    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item))) {
        dialog->Release();
        return std::nullopt;
    }
    PWSTR raw = nullptr;
    const auto display_result = item->GetDisplayName(SIGDN_FILESYSPATH, &raw);
    std::optional<std::filesystem::path> path;
    if (SUCCEEDED(display_result) && raw != nullptr) {
        path = std::filesystem::path(raw);
        ::CoTaskMemFree(raw);
    }
    item->Release();
    dialog->Release();
    return path;
}

} // namespace threadspire::ai::app
