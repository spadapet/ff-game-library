#pragma once

#include "point.h"
#include "rect.h"
#include "signal.h"

namespace ff
{
    struct window_message
    {
#if !UWP_APP
        const HWND hwnd;
#endif
        const UINT msg;
        const WPARAM wp;
        const LPARAM lp;
        LRESULT result;
        bool handled;
    };

    struct window_size
    {
        bool operator==(const ff::window_size& other) const;
        bool operator!=(const ff::window_size& other) const;

        ff::point_size rotated_pixel_size() const;
        int rotated_degrees_from_native() const;

        template<class T>
        ff::rect_t<T> rotate_rect(const ff::rect_t<T>& rect) const
        {
            const ff::point_t<T> size = this->pixel_size.cast<T>();

            switch (this->current_rotation)
            {
                default:
                    return rect;

                case DMDO_90:
                    return { rect.top, size.x - rect.right, rect.bottom, size.x - rect.left };

                case DMDO_180:
                    return { size.x - rect.right, size.y - rect.bottom, size.x - rect.left, size.y - rect.top };

                case DMDO_270:
                    return { size.y - rect.bottom, rect.left, size.y - rect.top, rect.right };
            }
        }

        template<class T>
        ff::point_t<T> rotate_point(const ff::point_t<T>& point) const
        {
            return this->rotate_rect<T>({ point, point }).top_left();
        }

        template<class T>
        ff::rect_t<T> unrotate_rect(const ff::rect_t<T>& rect) const
        {
            const ff::point_t<T> size = this->rotated_pixel_size().cast<T>();

            switch (this->current_rotation)
            {
                default:
                    return rect;

                case DMDO_90:
                    return { size.y - rect.bottom, rect.left, size.y - rect.top, rect.right };

                case DMDO_180:
                    return { size.x - rect.right, size.y - rect.bottom, size.x - rect.left, size.y - rect.top };

                case DMDO_270:
                    return { rect.top, size.x - rect.right, rect.bottom, size.x - rect.left };
            }
        }

        template<class T>
        ff::point_t<T> unrotate_point(const ff::point_t<T>& point) const
        {
            return this->unrotate_rect<T>({ point, point }).top_left();
        }

        ff::point_size pixel_size;
        double dpi_scale;
        int native_rotation; // DMDO_DEFAULT|90|180|270
        int current_rotation; // DMDO_DEFAULT|90|180|270
    };

    enum class window_type
    {
        none,
        main,
    };

    class window
    {
    public:
        window(window_type type);
        window(window&& other) noexcept;
        window(const window& other) = delete;
        ~window();

        window& operator=(window&& other) noexcept;
        window& operator=(const window& other) = delete;

        operator bool() const;
        bool operator!() const;

#if UWP_APP
        using handle_type = typename Windows::UI::Core::CoreWindow^;

        bool allow_swap_chain_panel();
        void allow_swap_chain_panel(bool value);
        Windows::UI::Xaml::Controls::SwapChainPanel^ swap_chain_panel() const;
        Windows::Graphics::Display::DisplayInformation^ display_info() const;
        Windows::UI::ViewManagement::ApplicationView^ application_view() const;

        ff::signal_sink<bool, Windows::Gaming::Input::Gamepad^>& gamepad_message_sink();
        void notify_gamepad_message(bool added, Windows::Gaming::Input::Gamepad^ gamepad);

        ff::signal_sink<unsigned int, Windows::UI::Core::PointerEventArgs^>& pointer_message_sink();
        void notify_pointer_message(unsigned int msg, Windows::UI::Core::PointerEventArgs^ args);

#else
        using handle_type = HWND;

        static bool create_class(std::string_view name, DWORD style, HINSTANCE instance, HCURSOR cursor, HBRUSH brush, UINT menu, HICON large_icon, HICON small_icon);
        static window create(window_type type, std::string_view class_name, std::string_view window_name, HWND parent, DWORD style, DWORD ex_style, int x, int y, int cx, int cy, HINSTANCE instance, HMENU menu);
        static window create_blank(window_type type, std::string_view window_name, HWND parent, DWORD style, DWORD ex_style = 0, int x = 0, int y = 0, int cx = 0, int cy = 0, HMENU menu = nullptr);
        static window create_message_window();
#endif

        handle_type handle() const;
        operator handle_type() const;
        bool operator==(handle_type handle) const;

        static window* main();
        ff::signal_sink<ff::window_message&>& message_sink();
        void notify_message(ff::window_message& message);

        window_size size();
        double dpi_scale();
        bool active();
        bool visible();
        bool focused();
        bool close();

    private:
#if UWP_APP
        Platform::Agile<Windows::UI::Core::CoreWindow> core_window;
        Windows::Graphics::Display::DisplayInformation^ display_info_;
        Windows::UI::ViewManagement::ApplicationView^ application_view_;
        Platform::Object^ window_events;
        ff::signal<bool, Windows::Gaming::Input::Gamepad^> gamepad_message_signal;
        ff::signal<unsigned int, Windows::UI::Core::PointerEventArgs^> pointer_message_signal;
        double dpi_scale_;
        bool allow_swap_chain_panel_;
        bool active_;
        bool visible_;
#else
        void reset(HWND hwnd);
        void destroy();

        static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

        // cache state on main thread to access from game thread
        enum class state_t
        {
            none = 0,
            active = 0x01,
            focused = 0x02,
            iconic = 0x04,
            visible = 0x08,
        };

        HWND hwnd;
        state_t state;
#endif

        ff::signal<window_message&> message_signal;
    };
}
