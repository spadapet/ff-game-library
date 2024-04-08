#include "pch.h"
#include "animation.h"
#include "font_file.h"
#include "graphics.h"
#include "init.h"
#include "palette_data.h"
#include "random_sprite.h"
#include "shader.h"
#include "sprite_font.h"
#include "sprite_list.h"
#include "sprite_resource.h"
#include "texture.h"
#include "texture_metadata.h"

static bool init_graphics_status;
static std::unique_ptr<ff::resource_objects> shader_resources;

namespace
{
    struct one_time_init_graphics
    {
        one_time_init_graphics(const ff::dxgi::client_functions& client_functions)
        {
            // Resource objects
            ff::resource_object_base::register_factory<ff::internal::animation_factory>("animation");
            ff::resource_object_base::register_factory<ff::internal::font_file_factory>("font_file");
            ff::resource_object_base::register_factory<ff::internal::palette_data_factory>("palette");
            ff::resource_object_base::register_factory<ff::internal::random_sprite_factory>("random_sprite");
            ff::resource_object_base::register_factory<ff::internal::sprite_list_factory>("sprites");
            ff::resource_object_base::register_factory<ff::internal::sprite_resource_factory>("sprite_resource");
            ff::resource_object_base::register_factory<ff::internal::sprite_font_factory>("font");
            ff::resource_object_base::register_factory<ff::internal::shader_factory>("shader");
            ff::resource_object_base::register_factory<ff::internal::texture_factory>("texture");
            ff::resource_object_base::register_factory<ff::internal::texture_metadata_factory>("texture_metadata");

            ::init_graphics_status = ff::internal::graphics::init(client_functions);
        }

        ~one_time_init_graphics()
        {
            ff::internal::graphics::destroy();
            ::shader_resources.reset();
            ::init_graphics_status = false;
        }
    };
}

static int init_graphics_refs;
static std::unique_ptr<one_time_init_graphics> init_graphics_data;
static std::mutex init_graphics_mutex;

static void host_set_shader_resource_data(std::shared_ptr<ff::data_base> data)
{
    ::shader_resources.reset();

    ff::dict dict;
    ff::data_reader reader(data);
    assert_ret(ff::dict::load(reader, dict));
    ::shader_resources = std::make_unique<ff::resource_objects>(dict);
}

static std::shared_ptr<ff::data_base> host_shader_data(std::string_view name)
{
    ff::auto_resource<ff::resource_file> res;

    if (::shader_resources)
    {
        res = ::shader_resources->get_resource_object(name);
    }

    if (!res.object())
    {
        res = name;
    }

    return res.object() ? res->loaded_data() : nullptr;
}

static const ff::dxgi::host_functions& get_dxgi_host_functions()
{
    static ff::dxgi::host_functions host_functions
    {
        ff::internal::graphics::on_frame_started,
        ff::internal::graphics::on_frame_complete,
        ff::graphics::defer::set_full_screen_target,
        ff::graphics::defer::remove_target,
        ff::graphics::defer::resize_target,
        ff::graphics::defer::full_screen,
        ff::graphics::defer::reset_device,
        ::host_set_shader_resource_data,
        ::host_shader_data,
    };

    return host_functions;
}

ff::init_graphics::init_graphics()
    : init_dx12(::get_dxgi_host_functions())
{
    std::scoped_lock lock(::init_graphics_mutex);

    if (::init_graphics_refs++ == 0 && this->init_resource && this->init_dx12)
    {
        ::init_graphics_data = std::make_unique<one_time_init_graphics>(init_dx12.client_functions());
    }
}

ff::init_graphics::~init_graphics()
{
    std::scoped_lock lock(::init_graphics_mutex);

    if (::init_graphics_refs-- == 1)
    {
        ::init_graphics_data.reset();
    }
}

ff::init_graphics::operator bool() const
{
    return this->init_resource && this->init_dx12 && ::init_graphics_status;
}
