module;

#include <array>
#include <cstddef>
#include <cstdint>

export module epoch.gui.input;

export import epoch.gui;

export namespace epochengine::gui_lib::input
{
    enum class PointerButton : std::uint8_t
    {
        left,
        right,
        middle,
        auxiliary_1,
        auxiliary_2,
        count
    };

    enum class Key : std::uint8_t
    {
        escape,
        enter,
        tab,
        space,
        backspace,
        delete_key,
        arrow_left,
        arrow_right,
        arrow_up,
        arrow_down,
        home,
        end,
        shift,
        control,
        alt,
        super,
        count
    };

    struct DigitalState
    {
        bool down{};
        bool pressed{};
        bool released{};
        bool repeated{};
        std::uint8_t click_count{};
    };

    struct ModifierState
    {
        bool shift{};
        bool control{};
        bool alt{};
        bool super{};
    };

    struct InputFrame
    {
        Vec2 pointer_position{};
        Vec2 pointer_delta{};
        Vec2 wheel_delta{};
        std::array<DigitalState, static_cast<std::size_t>(PointerButton::count)> pointer_buttons{};
        std::array<DigitalState, static_cast<std::size_t>(Key::count)> keys{};
        ModifierState modifiers{};
        std::uint64_t sequence{};

        [[nodiscard]] const DigitalState& pointer(PointerButton button) const noexcept;
        [[nodiscard]] const DigitalState& key(Key key_code) const noexcept;
    };

    class InputTracker final
    {
    public:
        InputTracker() = default;

        void reset() noexcept;
        void finish_frame() noexcept;
        void set_pointer_position(Vec2 position) noexcept;
        void add_wheel_delta(Vec2 delta) noexcept;
        void set_pointer_button(
            PointerButton button,
            bool down,
            std::uint8_t click_count = 1) noexcept;
        void set_key(Key key_code, bool down, bool repeated = false) noexcept;
        void set_modifiers(ModifierState modifiers) noexcept;

        [[nodiscard]] const InputFrame& frame() const noexcept;

    private:
        InputFrame frame_{};
    };

    enum class ModalInputMode : std::uint8_t
    {
        inactive,
        bounded,
        blocked_until_clear
    };

    class ModalInputArbiter final
    {
    public:
        void begin(Rect bounds) noexcept;
        void block_until_clear() noexcept;
        void clear() noexcept;

        [[nodiscard]] ModalInputMode mode() const noexcept;
        [[nodiscard]] Rect bounds() const noexcept;
        [[nodiscard]] bool active() const noexcept;
        [[nodiscard]] bool keyboard_captured() const noexcept;
        [[nodiscard]] bool pointer_allowed(Vec2 position) const noexcept;
        [[nodiscard]] bool background_input_allowed() const noexcept;

    private:
        Rect bounds_{};
        ModalInputMode mode_{ ModalInputMode::inactive };
    };

    struct ContextMenuRequest
    {
        Vec2 position{};
        bool requested{};
    };

    [[nodiscard]] ContextMenuRequest context_menu_request(
        const InputFrame& frame,
        Rect region,
        bool trigger_on_press = false) noexcept;

    [[nodiscard]] FloatingWindowInput floating_window_input(
        const InputFrame& frame) noexcept;

    [[nodiscard]] PopupInput popup_input(
        const InputFrame& frame,
        bool open_requested = false,
        bool toggle_requested = false,
        bool close_requested = false,
        bool owner_pressed = false) noexcept;

    enum class WindowChromeRegion : std::uint8_t
    {
        outside,
        client,
        caption,
        resize_left,
        resize_right,
        resize_top,
        resize_bottom,
        resize_top_left,
        resize_top_right,
        resize_bottom_left,
        resize_bottom_right,
        minimize_button,
        maximize_button,
        close_button
    };

    enum class WindowCommand : std::uint8_t
    {
        none,
        minimize,
        toggle_maximize,
        close
    };

    struct BorderlessWindowChromeOptions
    {
        Rect bounds{};
        float title_bar_height{ 42.0f };
        float resize_border{ 7.0f };
        float caption_padding_left{ 12.0f };
        float button_width{ 46.0f };
        bool movable{ true };
        bool resizable{ true };
        bool minimizable{ true };
        bool maximizable{ true };
        bool closable{ true };
    };

    struct BorderlessWindowChromeLayout
    {
        Rect bounds{};
        Rect title_bar{};
        Rect caption{};
        Rect client{};
        Rect minimize_button{};
        Rect maximize_button{};
        Rect close_button{};
        float resize_border{};
        bool movable{};
        bool resizable{};
        bool valid{};
    };

    [[nodiscard]] BorderlessWindowChromeLayout make_borderless_window_chrome_layout(
        const BorderlessWindowChromeOptions& options) noexcept;

    [[nodiscard]] WindowChromeRegion hit_test_borderless_window_chrome(
        const BorderlessWindowChromeLayout& layout,
        Vec2 point) noexcept;

    [[nodiscard]] WindowCommand borderless_window_command(
        const BorderlessWindowChromeLayout& layout,
        const InputFrame& frame) noexcept;
}
