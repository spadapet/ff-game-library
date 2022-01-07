#include "pch.h"
#include "commands.h"
#include "descriptor_allocator.h"
#include "device_reset_priority.h"
#include "globals.h"
#include "target_window.h"
#include "texture_util.h"
#include "queue.h"

ff::dx12::target_window::target_window()
    : target_window(ff::window::main())
{}

ff::dx12::target_window::target_window(ff::window* window)
    : window(window)
    , cached_size{}
    , main_window(ff::window::main() == window)
    , window_message_connection(window->message_sink().connect(std::bind(&target_window::handle_message, this, std::placeholders::_1)))
    , was_full_screen_on_close(false)
#if UWP_APP
    , use_xaml_composition(false)
    , cached_full_screen_uwp(false)
    , full_screen_uwp(false)
#endif
    , target_ready_event(ff::win_handle::create_event())
    , target_views(ff::dx12::cpu_target_descriptors().alloc_range(ff::dx12::target_window::BACK_BUFFER_COUNT))
    , back_buffer_index(0)
{
    this->size(this->window->size());

    ff::dx12::add_device_child(this, ff::dx12::device_reset_priority::target_window);

    if (this->main_window)
    {
        ff::dxgi_host().full_screen_target(this);
    }
}

ff::dx12::target_window::~target_window()
{
    ff::dx12::wait_for_idle();

    if (this->main_window && this->swap_chain)
    {
        this->swap_chain->SetFullscreenState(FALSE, nullptr);
    }

    ff::dxgi_host().remove_target(this);
    ff::dx12::remove_device_child(this);
}

ff::dx12::target_window::operator bool() const
{
    return this->swap_chain && this->window;
}

DXGI_FORMAT ff::dx12::target_window::format() const
{
    return DXGI_FORMAT_B8G8R8A8_UNORM;
}

ff::window_size ff::dx12::target_window::size() const
{
    return this->cached_size;
}

ff::dx12::resource& ff::dx12::target_window::dx12_target_texture()
{
    return *this->target_textures[this->back_buffer_index];
}

D3D12_CPU_DESCRIPTOR_HANDLE ff::dx12::target_window::dx12_target_view()
{
    return this->target_views.cpu_handle(this->back_buffer_index);
}

void ff::dx12::target_window::clear(ff::dxgi::command_context_base& context, const DirectX::XMFLOAT4& clear_color)
{
    ff::dx12::commands::get(context).clear(*this, clear_color);
}

void ff::dx12::target_window::wait_for_render_ready()
{
    if (*this)
    {
        ff::stack_vector<HANDLE, 2> handles;

        if (this->frame_latency_handle)
        {
            handles.push_back(this->frame_latency_handle);
        }

        if (this->target_fence_values[this->back_buffer_index].set_event(this->target_ready_event))
        {
            handles.push_back(this->target_ready_event);
        }

        if (!handles.empty())
        {
            ::WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), TRUE, INFINITE);
        }
    }
}

bool ff::dx12::target_window::frame_started(const DirectX::XMFLOAT4* clear_color)
{
    if (*this)
    {
        if (clear_color)
        {
            this->clear(ff::dx12::frame_commands(), *clear_color);
        }
        else
        {
            ff::dx12::frame_commands().discard(*this);
        }

        return true;
    }

    return false;
}

bool ff::dx12::target_window::present()
{
    if (*this)
    {
        ff::dx12::frame_commands().resource_state(*this->target_textures[this->back_buffer_index], D3D12_RESOURCE_STATE_PRESENT);
        this->target_fence_values[this->back_buffer_index] = ff::dx12::frame_commands().queue().execute(ff::dx12::frame_commands());

        HRESULT hr = this->swap_chain->Present(1, 0);
        if (hr != DXGI_ERROR_DEVICE_RESET && hr != DXGI_ERROR_DEVICE_REMOVED)
        {
            this->back_buffer_index = static_cast<size_t>(this->swap_chain->GetCurrentBackBufferIndex());
            this->render_presented_.notify(this);

            return true;
        }
    }

    return false;
}

void ff::dx12::target_window::before_resize()
{
    ff::dx12::wait_for_idle();
    this->frame_latency_handle.close();

    for (size_t i = 0; i < ff::dx12::target_window::BACK_BUFFER_COUNT; i++)
    {
        this->target_textures[i].reset();
        this->target_fence_values[i] = {};
    }
}

void ff::dx12::target_window::internal_reset()
{
    this->before_resize();
    this->swap_chain.Reset();
}

ff::signal_sink<ff::dxgi::target_base*>& ff::dx12::target_window::render_presented()
{
    return this->render_presented_;
}

ff::dxgi::target_access_base& ff::dx12::target_window::target_access()
{
    return *this;
}

size_t ff::dx12::target_window::target_array_start() const
{
    return 0;
}

size_t ff::dx12::target_window::target_array_size() const
{
    return 1;
}

size_t ff::dx12::target_window::target_mip_start() const
{
    return 0;
}

size_t ff::dx12::target_window::target_mip_size() const
{
    return 1;
}

bool ff::dx12::target_window::size(const ff::window_size& size)
{
    ff::window_size old_size = this->cached_size;
    ff::point_t<UINT> buffer_size = size.rotated_pixel_size().cast<UINT>();
    this->cached_size = size;
#if UWP_APP
    this->cached_full_screen_uwp = false;
#endif

    if (this->swap_chain && old_size != size) // on game thread
    {
        this->before_resize();

        DXGI_SWAP_CHAIN_DESC1 desc;
        this->swap_chain->GetDesc1(&desc);
        if (FAILED(this->swap_chain->ResizeBuffers(0, buffer_size.x, buffer_size.y, desc.Format, desc.Flags)))
        {
            debug_fail_ret_val(false);
        }
    }
    else if (!this->swap_chain) // first init on UI thread, reset is on game thread
    {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = buffer_size.x;
        desc.Height = buffer_size.y;
        desc.Format = this->format();
        desc.SampleDesc.Count = 1;
        desc.BufferCount = ff::dx12::target_window::BACK_BUFFER_COUNT;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT; // sets latency to 1 frame

        Microsoft::WRL::ComPtr<IDXGISwapChain1> new_swap_chain;
        Microsoft::WRL::ComPtr<IDXGIFactoryX> factory = ff::dx12::factory();
        Microsoft::WRL::ComPtr<ID3D12CommandQueueX> device = ff::dx12::get_command_queue(ff::dx12::direct_queue());

        ff::thread_dispatch::get_main()->send([this, factory, device, &new_swap_chain, &desc]()
        {
#if UWP_APP
            if (this->window)
            {
                Windows::UI::Xaml::Controls::SwapChainPanel^ swap_chain_panel = this->window->swap_chain_panel();
                this->use_xaml_composition = (swap_chain_panel != nullptr);

                if (this->use_xaml_composition)
                {
                    Microsoft::WRL::ComPtr<ISwapChainPanelNative> native_panel;

                    if (FAILED(reinterpret_cast<IUnknown*>(swap_chain_panel)->QueryInterface(IID_PPV_ARGS(&native_panel))) ||
                        FAILED(factory->CreateSwapChainForComposition(device.Get(), &desc, nullptr, &new_swap_chain)) ||
                        FAILED(native_panel->SetSwapChain(new_swap_chain.Get())))
                    {
                        debug_fail();
                    }
                }
                else if (FAILED(factory->CreateSwapChainForCoreWindow(device.Get(), reinterpret_cast<IUnknown*>(this->window->handle()), &desc, nullptr, &new_swap_chain)))
                {
                    debug_fail();
                }
            }
#else
            if (!*this->window ||
                FAILED(factory->CreateSwapChainForHwnd(device.Get(), *this->window, &desc, nullptr, nullptr, &new_swap_chain)) ||
                FAILED(factory->MakeWindowAssociation(*this->window, DXGI_MWA_NO_WINDOW_CHANGES)))
            {
                debug_fail();
            }
#endif
        });

        if (!new_swap_chain || FAILED(new_swap_chain.As(&this->swap_chain)))
        {
            debug_fail_ret_val(false);
        }
    }

    DXGI_MODE_ROTATION display_rotation = ff::dxgi::get_display_rotation(
        ff::dxgi::get_dxgi_rotation(size.native_rotation),
        ff::dxgi::get_dxgi_rotation(size.current_rotation));

#if UWP_APP
    // Scale the back buffer to the panel
    DXGI_MATRIX_3X2_F inverse_scale{};
    inverse_scale._11 = inverse_scale._22 = 1 / static_cast<float>(size.dpi_scale);
#endif

    if (!this->swap_chain ||
#if UWP_APP
        (this->use_xaml_composition && FAILED(this->swap_chain->SetMatrixTransform(&inverse_scale))) ||
#endif
        FAILED(this->swap_chain->SetRotation(display_rotation)))
    {
        debug_fail_ret_val(false);
    }

    this->back_buffer_index = static_cast<UINT>(this->swap_chain->GetCurrentBackBufferIndex());
    this->frame_latency_handle = ff::win_handle(this->swap_chain->GetFrameLatencyWaitableObject());

    for (size_t i = 0; i < ff::dx12::target_window::BACK_BUFFER_COUNT; i++)
    {
        if (!this->target_textures[i])
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            Microsoft::WRL::ComPtr<ID3D12ResourceX> resource_x;
            if (FAILED(this->swap_chain->GetBuffer(static_cast<UINT>(i), IID_PPV_ARGS(&resource))) ||
                FAILED(resource.As(&resource_x)))
            {
                debug_fail_ret_val(false);
            }

            this->target_textures[i] = std::make_unique<ff::dx12::resource>(resource_x.Get());
        }

        ff::dx12::create_target_view(this->target_textures[i].get(), this->target_views.cpu_handle(i));
    }

    this->size_changed_.notify(size);

    return true;
}

ff::signal_sink<ff::window_size>& ff::dx12::target_window::size_changed()
{
    return this->size_changed_;
}

bool ff::dx12::target_window::allow_full_screen() const
{
    return this->main_window;
}

bool ff::dx12::target_window::full_screen()
{
    if (this->main_window && *this)
    {
#if UWP_APP
        if (!this->cached_full_screen_uwp)
        {
            this->full_screen_uwp = this->window->application_view()->IsFullScreenMode;
            this->cached_full_screen_uwp = true;
        }

        return this->full_screen_uwp;
#else
        BOOL full_screen = FALSE;
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        return SUCCEEDED(this->swap_chain->GetFullscreenState(&full_screen, &output)) && full_screen;
#endif
    }

    return this->was_full_screen_on_close;
}

bool ff::dx12::target_window::full_screen(bool value)
{
    if (this->main_window && *this && !value != !this->full_screen())
    {
#if UWP_APP
        if (value)
        {
            return this->window->application_view()->TryEnterFullScreenMode();
        }
        else
        {
            this->window->application_view()->ExitFullScreenMode();
            return true;
        }
#else
        if (SUCCEEDED(this->swap_chain->SetFullscreenState(value, nullptr)))
        {
            return this->size(this->window->size());
        }
#endif
    }

    return false;
}

bool ff::dx12::target_window::reset()
{
    BOOL full_screen = FALSE;
    if (this->main_window && this->swap_chain)
    {
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        this->swap_chain->GetFullscreenState(&full_screen, &output);
        this->swap_chain->SetFullscreenState(FALSE, nullptr);
    }

    this->internal_reset();

    if (!this->window || !this->size(this->window->size()))
    {
        debug_fail_ret_val(false);
    }

    if (this->main_window && this->swap_chain && full_screen)
    {
        this->swap_chain->SetFullscreenState(TRUE, nullptr);
    }

    return *this;
}

void ff::dx12::target_window::handle_message(ff::window_message& msg)
{
    switch (msg.msg)
    {
        case WM_ACTIVATE:
            if (LOWORD(msg.wp) == WA_INACTIVE && this->main_window)
            {
                ff::dxgi_host().defer_full_screen(false);
            }
            break;

        case WM_SIZE:
            if (msg.wp != SIZE_MINIMIZED)
            {
                ff::dxgi_host().defer_resize(this, this->window->size());
            }
            break;

        case WM_DESTROY:
            this->window_message_connection.disconnect();

            if (this->main_window)
            {
                ff::thread_dispatch::get_game()->send([this]()
                    {
                        this->was_full_screen_on_close = this->full_screen();
                        this->full_screen(false);
                    });
            }

            this->window = nullptr;
            break;

        case WM_SYSKEYDOWN:
            if (this->main_window && msg.wp == VK_RETURN) // ALT-ENTER to toggle full screen mode
            {
                ff::dxgi_host().defer_full_screen(!this->full_screen());
                msg.result = 0;
                msg.handled = true;
            }
            else if (this->main_window && msg.wp == VK_BACK)
            {
#ifdef _DEBUG
                ff::dxgi_host().defer_validate_device(true);
#endif
            }
            break;

        case WM_SYSCHAR:
            if (this->main_window && msg.wp == VK_RETURN)
            {
                // prevent a 'ding' sound when switching between modes
                msg.result = 0;
                msg.handled = true;
            }
            break;

#if !UWP_APP
        case WM_WINDOWPOSCHANGED:
            if (this->main_window)
            {
                const WINDOWPOS& wp = *reinterpret_cast<const WINDOWPOS*>(msg.lp);
                if ((wp.flags & SWP_FRAMECHANGED) != 0 && !::IsIconic(msg.hwnd))
                {
                    ff::dxgi_host().defer_resize(this, this->window->size());
                }
            }
            break;
#endif
    }
}
