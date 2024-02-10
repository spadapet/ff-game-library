#include "pch.h"
#include "converters.h"
#include "font_provider.h"
#include "init.h"
#include "key_map.h"
#include "render_device.h"
#include "resource_cache.h"
#include "set_panel_child_focus_action.h"
#include "texture_provider.h"
#include "ui.h"
#include "ui_view.h"
#include "xaml_provider.h"

// https://www.noesisengine.com/trial
constexpr const char* DEFAULT_NAME = "f5025c38-29c4-476b-b18f-243889e0f620";
constexpr const char* DEFAULT_KEY = "FHHHPA88xzzkNh12lLZv7ozN4Cdd/5DLg3+G/FqFbBH1Q3pK";

static ff::init_ui_params ui_params;
static std::vector<ff::ui_view*> views;
static std::vector<ff::ui_view*> keyboard_input_views;
static std::vector<ff::ui_view*> mouse_input_views;
static std::vector<ff::ui_view*> rendered_views;
static Noesis::Ptr<ff::internal::ui::font_provider> font_provider;
static Noesis::Ptr<ff::internal::ui::texture_provider> texture_provider;
static Noesis::Ptr<ff::internal::ui::xaml_provider> xaml_provider;
static Noesis::Ptr<ff::internal::ui::render_device> render_device;
static Noesis::Ptr<Noesis::ResourceDictionary> application_resources;
static ff::ui_view* focused_view;
static Noesis::AssertHandler assert_handler;
static Noesis::ErrorHandler error_handler;
static Noesis::LogHandler log_handler;
static std::vector<ff::input_device_event> device_events;
static ff::signal_connection device_events_connection;
static std::mutex device_events_mutex;
static std::unique_ptr<ff::internal::ui::resource_cache> resource_cache;

static bool valid_view(ff::ui_view* view)
{
    return view && std::find(::views.cbegin(), ::views.cend(), view) != ::views.cend();
}

static bool valid_keyboard_view(ff::ui_view* view)
{
    return view && std::find(::keyboard_input_views.cbegin(), ::keyboard_input_views.cend(), view) != ::keyboard_input_views.cend();
}

static bool valid_mouse_view(ff::ui_view* view)
{
    return view && std::find(::mouse_input_views.cbegin(), ::mouse_input_views.cend(), view) != ::mouse_input_views.cend();
}

static bool valid_rendered_view(ff::ui_view* view)
{
    return view && std::find(::rendered_views.cbegin(), ::rendered_views.cend(), view) != ::rendered_views.cend();
}

template<class T>
static void erase(std::vector<T>& vec, T item)
{
    auto i = std::find(vec.cbegin(), vec.cend(), item);
    if (i != vec.cend())
    {
        vec.erase(i);
    }
}

static std::string_view log_levels[] =
{
    "Trace",
    "Debug",
    "Info",
    "Warning",
    "Error",
};

static void noesis_log_handler(const char* filename, uint32_t line, uint32_t level, const char* channel, const char* message)
{
    std::string_view log_level = (level < NS_COUNTOF(log_levels)) ? log_levels[level] : "";
    std::string_view channel2 = channel;
    std::string_view message2 = message;

    ff::log::write(ff::log::type::ui, "[NOESIS/", channel2, "/", log_level, "] ", message2);

    if (::log_handler)
    {
        ::log_handler(filename, line, level, channel, message);
    }
}

static bool noesis_assert_handler(const char* file, uint32_t line, const char* expr)
{
    if (ff::constants::debug_build && ::IsDebuggerPresent())
    {
        __debugbreak();
    }

    return ::assert_handler ? ::assert_handler(file, line, expr) : false;
}

static void noesis_error_handler(const char* file, uint32_t line, const char* message, bool fatal)
{
    if (::error_handler)
    {
        ::error_handler(file, line, message, fatal);
    }

    debug_fail_msg(message);
}

static void* noesis_alloc(void* user, size_t size)
{
    void* ptr = std::malloc(size);
    ff::log::write(ff::log::type::ui_mem, "NOESIS alloc: ", ptr, " (", size, ")");
    return ptr;
}

static void* noesis_realloc(void* user, void* ptr, size_t size)
{
    void* ptr2 = std::realloc(ptr, size);
    ff::log::write(ff::log::type::ui_mem, "NOESIS realloc: ", ptr, " -> ", ptr2, " (", size, ")");
    return ptr2;
}

static void noesis_dealloc(void* user, void* ptr)
{
    ff::log::write(ff::log::type::ui_mem, "NOESIS dealloc: ", ptr);
    return std::free(ptr);
}

static size_t noesis_alloc_size(void* user, void* ptr)
{
    size_t size = _msize(ptr);
    ff::log::write(ff::log::type::ui_mem, "NOESIS alloc size: ", ptr, " (", size, ")");
    return size;
}

static void noesis_dump_mem_usage()
{
    ff::log::write(ff::log::type::ui, "NOESIS memory now: ", Noesis::GetAllocatedMemory(), ", Total: ", Noesis::GetAllocatedMemoryAccum());
}

static Noesis::MemoryCallbacks memory_callbacks =
{
    nullptr,
    ::noesis_alloc,
    ::noesis_realloc,
    ::noesis_dealloc,
    ::noesis_alloc_size,
};

static void update_cursor_callback(void* user, Noesis::IView* internal_view, Noesis::Cursor* cursor)
{
    for (ff::ui_view* view : ::views)
    {
        if (view->internal_view() == internal_view)
        {
            view->cursor(cursor ? cursor->Type() : Noesis::CursorType_None);
            break;
        }
    }
}

static void open_url_callback(void* user, const char* url)
{
#if UWP_APP
    winrt::hstring purl = winrt::to_hstring(std::string_view(url));
    ff::thread_dispatch::get_main()->post([purl]()
        {
            winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(purl));
        });
#else
    std::wstring purl = ff::string::to_wstring(url);
    ::ShellExecute(*ff::window::main(), L"open", purl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

static void play_sound_callback(void* user, const Noesis::Uri& uri, float volume)
{
    // Not implemented
}

static void software_keyboard_callback(void* user, Noesis::UIElement* focused, bool open)
{
    // Not implemented
}

static void load_assembly_callback(void* user, const char* assembly)
{
    // Not implemented
}

extern "C" void NsInitPackageAppMediaElement();
extern "C" void NsRegisterReflectionAppInteractivity();

static void register_components(std::function<void()>&& register_extra_components)
{
    ::NsInitPackageAppMediaElement();
    ::NsRegisterReflectionAppInteractivity();

    Noesis::RegisterComponent<ff::ui::bool_to_visible_converter>();
    Noesis::RegisterComponent<ff::ui::bool_to_collapsed_converter>();
    Noesis::RegisterComponent<ff::ui::bool_to_inverse_converter>();
    Noesis::RegisterComponent<ff::ui::bool_to_object_converter>();
    Noesis::RegisterComponent<ff::ui::level_to_indent_converter>();
    Noesis::RegisterComponent<ff::ui::object_to_visible_converter>();
    Noesis::RegisterComponent<ff::ui::object_to_collapsed_converter>();
    Noesis::RegisterComponent<ff::ui::object_to_object_converter>();
    Noesis::RegisterComponent<ff::ui::set_panel_child_focus_action>();

    if (register_extra_components)
    {
        register_extra_components();
    }

    if (::ui_params.register_components_func)
    {
        ::ui_params.register_components_func();
    }
}

static void init_fallback_fonts()
{
    std::array<const char*, 3> default_fonts{ "#Segoe UI Emoji", "#Segoe MDL2 Assets", "#Segoe UI" };
    Noesis::GUI::SetFontFallbacks(default_fonts.data(), static_cast<uint32_t>(default_fonts.size()));
    Noesis::GUI::SetFontDefaultProperties(12.0f, Noesis::FontWeight_Normal, Noesis::FontStretch_Normal, Noesis::FontStyle_Normal);
}

static void init_application_resources()
{
    if (::ui_params.application_resources_name.empty())
    {
        ff::auto_resource_value app_res = ::resource_cache->get_resource_object("application_resources.xaml");
        if (!app_res->value()->is_type<nullptr_t>())
        {
            ::application_resources = Noesis::GUI::LoadXaml<Noesis::ResourceDictionary>("application_resources.xaml");
        }
    }
    else
    {
        ::application_resources = Noesis::GUI::LoadXaml<Noesis::ResourceDictionary>(::ui_params.application_resources_name.c_str());
    }

    if (::application_resources)
    {
        Noesis::GUI::SetApplicationResources(::application_resources);
    }
}

static bool init_noesis(std::function<void()>&& register_extra_components)
{
    // Global handlers
    ::assert_handler = Noesis::SetAssertHandler(::noesis_assert_handler);
    ::error_handler = Noesis::SetErrorHandler(::noesis_error_handler);
    ::log_handler = Noesis::SetLogHandler(::noesis_log_handler);
    Noesis::SetMemoryCallbacks(::memory_callbacks);
    Noesis::SetLicense(
        ::ui_params.noesis_license_name.size() ? ::ui_params.noesis_license_name.c_str() : ::DEFAULT_NAME,
        ::ui_params.noesis_license_key.size() ? ::ui_params.noesis_license_key.c_str() : ::DEFAULT_KEY);

    Noesis::GUI::DisableHotReload();
    Noesis::GUI::DisableSocketInit();
    Noesis::GUI::DisableInspector();
    Noesis::GUI::Init();

    // Callbacks
    Noesis::GUI::SetCursorCallback(nullptr, ::update_cursor_callback);
    Noesis::GUI::SetOpenUrlCallback(nullptr, ::open_url_callback);
    Noesis::GUI::SetPlayAudioCallback(nullptr, ::play_sound_callback);
    Noesis::GUI::SetSoftwareKeyboardCallback(nullptr, ::software_keyboard_callback);
    Noesis::GUI::SetLoadAssemblyCallback(nullptr, ::load_assembly_callback);

    // Resource providers
    ::resource_cache = std::make_unique<ff::internal::ui::resource_cache>();
    ::render_device = Noesis::MakePtr<ff::internal::ui::render_device>();
    ::xaml_provider = Noesis::MakePtr<ff::internal::ui::xaml_provider>();
    ::font_provider = Noesis::MakePtr<ff::internal::ui::font_provider>();
    ::texture_provider = Noesis::MakePtr<ff::internal::ui::texture_provider>();

    Noesis::GUI::SetXamlProvider(::xaml_provider);
    Noesis::GUI::SetTextureProvider(::texture_provider);
    Noesis::GUI::SetFontProvider(::font_provider);

    ::register_components(std::move(register_extra_components));
    ::init_fallback_fonts();
    ::init_application_resources();
    ::noesis_dump_mem_usage();

    return true;
}

static void destroy_noesis()
{
    for (ff::ui_view* view : std::vector<ff::ui_view*>(::views))
    {
        view->destroy();
    }

    assert(::views.empty());

    ::application_resources.Reset();
    ::xaml_provider.Reset();
    ::font_provider.Reset();
    ::texture_provider.Reset();
    ::render_device.Reset();
    ::resource_cache.reset();

    Noesis::GUI::Shutdown();

    ::noesis_dump_mem_usage();
    assert(!Noesis::GetAllocatedMemory());

    Noesis::SetLogHandler(::log_handler);
    ::log_handler = nullptr;

    Noesis::SetErrorHandler(::error_handler);
    ::error_handler = nullptr;

    Noesis::SetAssertHandler(::assert_handler);
    ::assert_handler = nullptr;
}

bool ff::internal::ui::init(const ff::init_ui_params& params)
{
    ::ui_params = params;

    ::device_events_connection = ff::input::combined_devices().event_sink().connect([](const ff::input_device_event& event)
        {
            std::scoped_lock lock(::device_events_mutex);
            ::device_events.push_back(event);
        });

    return true;
}

void ff::internal::ui::destroy()
{
    ::device_events_connection.disconnect();
}

void ff::internal::ui::init_game_thread(std::function<void()>&& register_extra_components)
{
    bool status = ::init_noesis(std::move(register_extra_components));
    assert(status);
}

void ff::internal::ui::destroy_game_thread()
{
    ::destroy_noesis();
}

ff::internal::ui::font_provider* ff::internal::ui::global_font_provider()
{
    return ::font_provider;
}

ff::internal::ui::render_device* ff::internal::ui::global_render_device()
{
    return ::render_device;
}

ff::internal::ui::resource_cache* ff::internal::ui::global_resource_cache()
{
    return ::resource_cache.get();
}

ff::internal::ui::texture_provider* ff::internal::ui::global_texture_provider()
{
    return ::texture_provider;
}

ff::internal::ui::xaml_provider* ff::internal::ui::global_xaml_provider()
{
    return ::xaml_provider;
}

void ff::internal::ui::register_view(ff::ui_view* view)
{
    if (view && !::valid_view(view))
    {
        ::views.push_back(view);
    }
}

void ff::internal::ui::unregister_view(ff::ui_view* view)
{
    ::erase(::views, view);
    ::erase(::keyboard_input_views, view);
    ::erase(::mouse_input_views, view);
    ::erase(::rendered_views, view);

    if (::focused_view == view)
    {
        ::focused_view = nullptr;
    }
}

ff::internal::ui::render_device* ff::internal::ui::on_render_view(ff::ui_view* view)
{
    if (!::valid_rendered_view(view))
    {
        ::rendered_views.push_back(view);
    }

    if (view->keyboard_enabled() && !::valid_keyboard_view(view))
    {
        if (view->block_input_below())
        {
            ::keyboard_input_views.clear();
        }

        ::keyboard_input_views.push_back(view);
    }

    if (view->mouse_enabled() && !::valid_mouse_view(view))
    {
        if (view->block_input_below())
        {
            ::mouse_input_views.clear();
        }

        ::mouse_input_views.push_back(view);
    }

    return ff::internal::ui::global_render_device();
}

void ff::internal::ui::on_focus_view(ff::ui_view* view, bool focused)
{
    if (focused)
    {
        if (::focused_view && ::focused_view != view)
        {
            ::focused_view->focused(false);
        }

        ::focused_view = view;
    }
}

void ff::ui::state_advance_time()
{
    ::resource_cache->advance();
}

void ff::ui::state_advance_input()
{
    std::vector<ff::input_device_event> device_events;
    {
        std::scoped_lock lock(::device_events_mutex);
        std::swap(device_events, ::device_events);
    }

    for (const ff::input_device_event& event : device_events)
    {
        const ff::point_float event_posf = event.pos.cast<float>();

        switch (event.type)
        {
            case ff::input_device_event_type::key_press:
                if (::focused_view && ff::internal::ui::valid_key(event.id))
                {
                    if (event.count)
                    {
                        ::focused_view->internal_view()->KeyDown(ff::internal::ui::get_key(event.id));
                    }
                    else
                    {
                        ::focused_view->internal_view()->KeyUp(ff::internal::ui::get_key(event.id));
                    }
                }
                break;

            case ff::input_device_event_type::key_char:
                if (::focused_view)
                {
                    ::focused_view->internal_view()->Char(event.id);
                }
                break;

            case ff::input_device_event_type::mouse_press:
                if (ff::internal::ui::valid_mouse_button(event.id))
                {
                    for (auto i = ::mouse_input_views.rbegin(); i != ::mouse_input_views.rend(); i++)
                    {
                        bool handled = false;
                        ff::ui_view* view = *i;
                        ff::point_int pos = view->screen_to_view(event_posf).cast<int>();

                        if (event.count == 2)
                        {
                            handled = view->internal_view()->MouseDoubleClick(pos.x, pos.y, ff::internal::ui::get_mouse_button(event.id));
                        }
                        else if (event.count == 0)
                        {
                            handled = view->internal_view()->MouseButtonUp(pos.x, pos.y, ff::internal::ui::get_mouse_button(event.id));
                        }
                        else
                        {
                            handled = view->internal_view()->MouseButtonDown(pos.x, pos.y, ff::internal::ui::get_mouse_button(event.id));
                        }

                        if (!::valid_mouse_view(view))
                        {
                            view = nullptr;
                        }

                        if (handled)
                        {
                            if (view)
                            {
                                view->focused(true);
                            }

                            break;
                        }

                        if (view && view->hit_test(event_posf))
                        {
                            break;
                        }
                    }
                }
                break;

            case ff::input_device_event_type::mouse_move:
                for (auto i = ::mouse_input_views.rbegin(); i != ::mouse_input_views.rend(); i++)
                {
                    ff::ui_view* view = *i;
                    ff::point_int pos = view->screen_to_view(event_posf).cast<int>();

                    if (view->internal_view()->MouseMove(pos.x, pos.y))
                    {
                        break;
                    }

                    if (!::valid_mouse_view(view))
                    {
                        view = nullptr;
                    }

                    if (view && view->hit_test(event_posf))
                    {
                        break;
                    }
                }
                break;

            case ff::input_device_event_type::mouse_wheel_x:
                for (auto i = ::mouse_input_views.rbegin(); i != ::mouse_input_views.rend(); i++)
                {
                    ff::ui_view* view = *i;
                    ff::point_int pos = view->screen_to_view(event_posf).cast<int>();

                    if (view->internal_view()->MouseHWheel(pos.x, pos.y, event.count))
                    {
                        break;
                    }
                }
                break;

            case ff::input_device_event_type::mouse_wheel_y:
                for (auto i = ::mouse_input_views.rbegin(); i != ::mouse_input_views.rend(); i++)
                {
                    ff::ui_view* view = *i;
                    ff::point_int pos = view->screen_to_view(event_posf).cast<int>();

                    if (view->internal_view()->MouseWheel(pos.x, pos.y, event.count))
                    {
                        break;
                    }
                }
                break;

            case ff::input_device_event_type::touch_press:
                for (auto i = ::mouse_input_views.rbegin(); i != ::mouse_input_views.rend(); i++)
                {
                    bool handled = false;
                    ff::ui_view* view = *i;
                    ff::point_int pos = view->screen_to_view(event_posf).cast<int>();

                    if (event.count == 0)
                    {
                        handled = view->internal_view()->TouchUp(pos.x, pos.y, event.id);
                    }
                    else
                    {
                        handled = view->internal_view()->TouchDown(pos.x, pos.y, event.id);
                    }

                    if (!::valid_mouse_view(view))
                    {
                        view = nullptr;
                    }

                    if (handled)
                    {
                        if (view)
                        {
                            view->focused(true);
                        }

                        break;
                    }

                    if (view && view->hit_test(event_posf))
                    {
                        break;
                    }
                }
                break;

            case ff::input_device_event_type::touch_move:
                for (auto i = ::mouse_input_views.rbegin(); i != ::mouse_input_views.rend(); i++)
                {
                    ff::ui_view* view = *i;
                    ff::point_int pos = view->screen_to_view(event_posf).cast<int>();

                    if (view->internal_view()->TouchMove(pos.x, pos.y, event.id))
                    {
                        break;
                    }

                    if (!::valid_mouse_view(view))
                    {
                        view = nullptr;
                    }

                    if (view && view->hit_test(event_posf))
                    {
                        break;
                    }
                }
                break;
        }
    }
}

void ff::ui::state_rendering()
{
    // on_render_view will need to be called again for each view actually rendered
    ::keyboard_input_views.clear();
    ::mouse_input_views.clear();
    ::rendered_views.clear();
}

void ff::ui::state_rendered()
{
    // Fix focus among all views that were actually rendered

    if (::focused_view)
    {
        bool focus = ::valid_keyboard_view(::focused_view) && ff::window::main()->active();
        ::focused_view->focused(focus);
    }

    if ((!::focused_view || !::focused_view->focused()) && !::keyboard_input_views.empty() && ff::window::main()->active())
    {
        ff::log::write(ff::log::type::ui_focus, "No focus, choosing new view to focus.");

        // No visible view has focus, so choose a new one
        ff::ui_view* view = ::keyboard_input_views.back();
        view->focused(true);
        assert(::focused_view == view);
    }
}

const ff::dxgi::palette_base* ff::ui::global_palette()
{
    return ::ui_params.palette_func ? ::ui_params.palette_func() : nullptr;
}

const std::vector<ff::ui_view*>& ff::ui::input_views()
{
    return ::mouse_input_views;
}

const std::vector<ff::ui_view*>& ff::ui::rendered_views()
{
    return ::rendered_views;
}

bool ff::ui::is_hand_cursor()
{
    for (auto i = ::mouse_input_views.rbegin(); i != ::mouse_input_views.rend(); i++)
    {
        ff::ui_view* view = *i;
        if (view->cursor() == Noesis::CursorType_Hand)
        {
            return true;
        }
    }

    return false;
}
