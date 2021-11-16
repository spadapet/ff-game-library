#include "pch.h"
#include "commands.h"
#include "device_reset_priority.h"
#include "fence.h"
#include "globals.h"
#include "mem_allocator.h"
#include "mem_range.h"
#include "queue.h"
#include "resource.h"

static std::unique_ptr<ff::dx12::commands> get_copy_commands(ff::dx12::commands*& commands)
{
    std::unique_ptr<ff::dx12::commands> new_commands;
    if (!commands)
    {
        new_commands = std::make_unique<ff::dx12::commands>(ff::dx12::copy_queue().new_commands());
        commands = new_commands.get();
    }

    return new_commands;
}

static bool validate_texture_range(const D3D12_RESOURCE_DESC& desc, size_t sub_index, size_t sub_count, const ff::rect_size* source_rect, size_t& width, size_t& height, size_t& mip_count, size_t& array_count)
{
    const size_t mip_start = sub_index % desc.MipLevels;

    width = source_rect ? source_rect->width() : static_cast<size_t>(desc.Width >> mip_start);
    height = source_rect ? source_rect->height() : static_cast<size_t>(desc.Height >> mip_start);
    mip_count = std::min<size_t>(sub_count, desc.MipLevels);
    array_count = std::max<size_t>(sub_count / desc.MipLevels, 1);

    // For multiple images in an array, all of the mip levels must be captured too
    return sub_count && (mip_start + sub_count <= desc.MipLevels || (!mip_start && sub_count % desc.MipLevels == 0));
}

size_t ff::dx12::resource::readback_texture_data::image_count() const
{
    return this->mem_ranges.size();
}

const DirectX::Image& ff::dx12::resource::readback_texture_data::image(size_t index)
{
    this->fence_value.wait(nullptr);
    return this->mem_ranges[index].second;
}

ff::dx12::resource::resource(
    const D3D12_RESOURCE_DESC& desc,
    D3D12_RESOURCE_STATES initial_state,
    D3D12_CLEAR_VALUE optimized_clear_value,
    std::shared_ptr<ff::dx12::mem_range> mem_range,
    bool allocate_mem_range)
    : mem_range_(mem_range)
    , optimized_clear_value(optimized_clear_value)
    , state_(initial_state)
    , desc_(desc)
    , alloc_info_(ff::dx12::device()->GetResourceAllocationInfo(0, 1, &desc))
{
    assert(desc.Dimension != D3D12_RESOURCE_DIMENSION_UNKNOWN && this->alloc_info_.SizeInBytes > 0);

    if (allocate_mem_range)
    {
        if (!mem_range || ff::math::align_up(mem_range->start(), this->alloc_info_.Alignment) != mem_range->start() || mem_range->size() < this->alloc_info_.SizeInBytes)
        {
            ff::dx12::mem_allocator& allocator = (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
                ? ff::dx12::static_buffer_allocator()
                : ff::dx12::texture_allocator();
            this->mem_range_ = std::make_shared<ff::dx12::mem_range>(
                allocator.alloc_bytes(this->alloc_info_.SizeInBytes, this->alloc_info_.Alignment));
        }
    }
    else
    {
        assert(!mem_range);
    }

    this->reset();
    assert(*this && (!mem_range || mem_range == this->mem_range_));

    ff::dx12::add_device_child(this, ff::dx12::device_reset_priority::resource);
}

ff::dx12::resource::resource(resource& other, ff::dx12::commands* commands)
    : resource(other.desc_, D3D12_RESOURCE_STATE_COPY_DEST, other.optimized_clear_value)
{
    std::unique_ptr<ff::dx12::commands> new_commands = ::get_copy_commands(commands);
    other.state(D3D12_RESOURCE_STATE_COPY_SOURCE, commands);
    commands->copy_resource(*this, other);
    this->write_fence_value = commands->next_fence_value();
}

ff::dx12::resource::resource(resource&& other) noexcept
    : state_(D3D12_RESOURCE_STATE_COMMON)
{
    *this = std::move(other);
    ff::dx12::add_device_child(this, ff::dx12::device_reset_priority::resource);
}

ff::dx12::resource::~resource()
{
    this->destroy(false);
    ff::dx12::remove_device_child(this);
}

ff::dx12::resource& ff::dx12::resource::operator=(resource&& other) noexcept
{
    if (this != &other)
    {
        this->destroy(false);

        bool active = other.active();
        other.active(false, nullptr);

        std::swap(this->resource_, other.resource_);
        std::swap(this->mem_range_, other.mem_range_);
        std::swap(this->optimized_clear_value, other.optimized_clear_value);
        std::swap(this->state_, other.state_);
        std::swap(this->desc_, other.desc_);
        std::swap(this->alloc_info_, other.alloc_info_);
        std::swap(this->read_fence_values, other.read_fence_values);
        std::swap(this->write_fence_value, other.write_fence_value);

        this->active(active, nullptr);
    }

    return *this;
}

ff::dx12::resource::operator bool() const
{
    return this->resource_ != nullptr;
}

void ff::dx12::resource::active(bool value, ff::dx12::commands* commands)
{
    if (this->mem_range_ && !value != !this->active())
    {
        this->mem_range_->active_resource(value ? this : nullptr, commands);
    }
}

bool ff::dx12::resource::active() const
{
    return !this->mem_range_ || this->mem_range_->active_resource() == this;
}

void ff::dx12::resource::activated()
{}

void ff::dx12::resource::deactivated()
{}

const std::shared_ptr<ff::dx12::mem_range>& ff::dx12::resource::mem_range() const
{
    return this->mem_range_;
}

D3D12_RESOURCE_STATES ff::dx12::resource::state(D3D12_RESOURCE_STATES state, ff::dx12::commands* commands)
{
    D3D12_RESOURCE_STATES state_before = this->state_;

    if (commands)
    {
        bool for_write = (state & static_cast<D3D12_RESOURCE_STATES>(
            D3D12_RESOURCE_STATE_RENDER_TARGET |
            D3D12_RESOURCE_STATE_DEPTH_WRITE |
            D3D12_RESOURCE_STATE_STREAM_OUT |
            D3D12_RESOURCE_STATE_COPY_DEST |
            D3D12_RESOURCE_STATE_RESOLVE_DEST |
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE |
            D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE |
            D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE)) != 0;

        commands->wait_before_execute().add(this->write_fence_value);

        if (for_write)
        {
            commands->wait_before_execute().add(this->read_fence_values);
            this->write_fence_value = commands->next_fence_value();
        }
        else
        {
            this->read_fence_values.add(commands->next_fence_value());
        }
    }

    if (state != this->state_)
    {
        this->state_ = state;

        if (commands)
        {
            commands->resource_barrier(this, state_before, this->state_);
        }
    }

    return state_before;
}

D3D12_RESOURCE_STATES ff::dx12::resource::state() const
{
    return this->state_;
}

const D3D12_RESOURCE_DESC& ff::dx12::resource::desc() const
{
    return this->desc_;
}

const D3D12_RESOURCE_ALLOCATION_INFO& ff::dx12::resource::alloc_info() const
{
    return this->alloc_info_;
}

ff::dx12::fence_value ff::dx12::resource::update_buffer(ff::dx12::commands* commands, const void* data, uint64_t offset, uint64_t size)
{
    if (!size || !data || offset + size > this->alloc_info_.SizeInBytes)
    {
        assert(!size);
        return {};
    }

    std::unique_ptr<ff::dx12::commands> new_commands = ::get_copy_commands(commands);
    ff::dx12::mem_range mem_range = ff::dx12::upload_allocator().alloc_buffer(size, commands->next_fence_value());
    if (!mem_range || !mem_range.cpu_data())
    {
        assert(false);
        return {};
    }

    ::memcpy(mem_range.cpu_data(), data, static_cast<size_t>(size));

    D3D12_RESOURCE_STATES state_before = this->state(D3D12_RESOURCE_STATE_COPY_DEST, commands);
    commands->update_buffer(*this, offset, mem_range);
    this->state(state_before, commands);

    return commands->next_fence_value();
}

std::pair<ff::dx12::fence_value, ff::dx12::mem_range> ff::dx12::resource::readback_buffer(ff::dx12::commands* commands, uint64_t offset, uint64_t size)
{
    if (!size || offset + size > this->alloc_info_.SizeInBytes)
    {
        assert(!size);
        return {};
    }

    std::unique_ptr<ff::dx12::commands> new_commands = ::get_copy_commands(commands);
    ff::dx12::mem_range mem_range = ff::dx12::readback_allocator().alloc_buffer(size, commands->next_fence_value());
    if (!mem_range || !mem_range.cpu_data())
    {
        assert(false);
        return {};
    }

    D3D12_RESOURCE_STATES state_before = this->state(D3D12_RESOURCE_STATE_COPY_SOURCE, commands);
    commands->readback_buffer(mem_range, *this, offset);
    this->state(state_before, commands);

    return std::make_pair(commands->next_fence_value(), std::move(mem_range));
}

std::vector<uint8_t> ff::dx12::resource::capture_buffer(ff::dx12::commands* commands, uint64_t offset, uint64_t size)
{
    auto [fence_value, result_mem_range] = this->readback_buffer(commands, offset, size);
    if (!fence_value)
    {
        assert(!size);
        return {};
    }

    std::vector<uint8_t> result_bytes;
    result_bytes.resize(static_cast<size_t>(size));
    fence_value.wait(nullptr);
    std::memcpy(result_bytes.data(), result_mem_range.cpu_data(), static_cast<size_t>(size));

    return result_bytes;
}

ff::dx12::fence_value ff::dx12::resource::update_texture(ff::dx12::commands* commands, const DirectX::Image* images, size_t sub_index, size_t sub_count, ff::point_size dest_pos)
{
    std::unique_ptr<ff::dx12::commands> new_commands = ::get_copy_commands(commands);
    D3D12_RESOURCE_STATES state_before = this->state(D3D12_RESOURCE_STATE_COPY_DEST, commands);

    size_t temp_width, temp_height, temp_mip_count, temp_array_count;
    if (!::validate_texture_range(this->desc_, sub_index, sub_count, nullptr, temp_width, temp_height, temp_mip_count, temp_array_count))
    {
        assert(!sub_count);
        return {};
    }

    for (size_t i = 0; i < sub_count; i++)
    {
        const DirectX::Image& src_image = images[i];
        DirectX::Image dest_image = src_image;

        if (FAILED(DirectX::ComputePitch(src_image.format, src_image.width, src_image.height, dest_image.rowPitch, dest_image.slicePitch)))
        {
            assert(false);
            return {};
        }

        const size_t scan_lines = DirectX::ComputeScanlines(dest_image.format, dest_image.height);
        dest_image.rowPitch = ff::math::align_up<size_t>(dest_image.rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        dest_image.slicePitch = dest_image.rowPitch * scan_lines;

        ff::dx12::mem_range mem_range = ff::dx12::upload_allocator().alloc_texture(dest_image.slicePitch, commands->next_fence_value());
        dest_image.pixels = static_cast<uint8_t*>(mem_range.cpu_data());

        if (!mem_range || !mem_range.cpu_data() || FAILED(DirectX::CopyRectangle(
            src_image, DirectX::Rect(0, 0, src_image.width, src_image.height), dest_image, DirectX::TEX_FILTER_DEFAULT, 0, 0)))
        {
            assert(false);
            return {};
        }

        const size_t relative_mip_level = i % this->desc_.MipLevels;
        const ff::point_size pos(dest_pos.x >> relative_mip_level, dest_pos.y >> relative_mip_level);
        commands->update_texture(*this, sub_index + i, pos, mem_range, D3D12_SUBRESOURCE_FOOTPRINT
        {
            dest_image.format, static_cast<UINT>(dest_image.width), static_cast<UINT>(dest_image.height), 1, static_cast<UINT>(dest_image.rowPitch),
        });
    }

    this->state(state_before, commands);
    return commands->next_fence_value();
}

ff::dx12::resource::readback_texture_data ff::dx12::resource::readback_texture(ff::dx12::commands* commands, size_t sub_index, size_t sub_count, const ff::rect_size* source_rect)
{
    readback_texture_data result{};
    result.mem_ranges.reserve(sub_count);

    if (!::validate_texture_range(this->desc_, sub_index, sub_count, source_rect, result.width, result.height, result.mip_count, result.array_count))
    {
        assert(!sub_count);
        return {};
    }

    std::unique_ptr<ff::dx12::commands> new_commands = ::get_copy_commands(commands);
    D3D12_RESOURCE_STATES state_before = this->state(D3D12_RESOURCE_STATE_COPY_SOURCE, commands);

    for (size_t i = 0; i < sub_count; i++)
    {
        const size_t relative_mip_level = i % this->desc_.MipLevels;
        const size_t absolute_mip_level = (sub_index + i) % this->desc_.MipLevels;
        const ff::rect_size rect = source_rect
            ? ff::rect_size(source_rect->left >> relative_mip_level, source_rect->top >> relative_mip_level, source_rect->right >> relative_mip_level, source_rect->bottom >> relative_mip_level)
            : ff::rect_size(0, 0, result.width >> absolute_mip_level, result.height >> absolute_mip_level);

        DirectX::Image image;
        image.format = this->desc_.Format;
        image.width = rect.width();
        image.height = rect.height();

        if (FAILED(DirectX::ComputePitch(image.format, image.width, image.height, image.rowPitch, image.slicePitch)))
        {
            assert(false);
            return {};
        }

        const size_t scan_lines = DirectX::ComputeScanlines(this->desc_.Format, image.height);
        image.rowPitch = ff::math::align_up<size_t>(image.rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        image.slicePitch = image.rowPitch * scan_lines;

        ff::dx12::mem_range mem_range = ff::dx12::readback_allocator().alloc_texture(image.slicePitch, commands->next_fence_value());
        if (!mem_range || !mem_range.cpu_data())
        {
            assert(false);
            return {};
        }

        image.pixels = static_cast<uint8_t*>(mem_range.cpu_data());

        const D3D12_SUBRESOURCE_FOOTPRINT layout
        {
            image.format,
            static_cast<UINT>(image.width),
            static_cast<UINT>(image.height), 1, // depth
            static_cast<UINT>(image.rowPitch),
        };

        commands->readback_texture(mem_range, layout, *this, sub_index + i, rect);
        result.mem_ranges.push_back(std::make_pair(std::move(mem_range), std::move(image)));
    }

    this->state(state_before, commands);
    result.fence_value = commands->next_fence_value();
    return result;
}

DirectX::ScratchImage ff::dx12::resource::capture_texture(ff::dx12::commands* commands, size_t sub_index, size_t sub_count, const ff::rect_size* source_rect)
{
    readback_texture_data result = this->readback_texture(commands, sub_index, sub_count, source_rect);
    DirectX::ScratchImage scratch;

    if (!result.fence_value || !result.image_count() || result.array_count * result.mip_count != result.image_count() ||
        FAILED(scratch.Initialize2D(this->desc_.Format, result.width, result.height, result.array_count, result.mip_count)))
    {
        assert(false);
        return {};
    }

    for (size_t i = 0; i < result.image_count(); i++)
    {
        const DirectX::Image& image = result.image(i);
        if (FAILED(DirectX::CopyRectangle(image, DirectX::Rect(0, 0, image.width, image.height), scratch.GetImages()[i], DirectX::TEX_FILTER_DEFAULT, 0, 0)))
        {
            assert(false);
            return {};
        }
    }

    return scratch;
}

void ff::dx12::resource::destroy(bool for_reset)
{
    if (this->mem_range_ && this->active())
    {
        this->mem_range_->active_resource(nullptr, nullptr);
    }

    if (!for_reset)
    {
        ff::dx12::fence_values fence_values = std::move(this->read_fence_values);
        fence_values.add(this->write_fence_value);
        this->write_fence_value = {};

        ff::dx12::keep_alive_resource(std::move(*this), std::move(fence_values));

        this->mem_range_.reset();
    }
    else
    {
        this->read_fence_values.clear();
        this->write_fence_value = {};
    }

    this->resource_.Reset();
}

void ff::dx12::resource::before_reset()
{
    this->destroy(true);
}

bool ff::dx12::resource::reset()
{
    if (!this->mem_range_)
    {
        if (FAILED(ff::dx12::device()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &this->desc_,
            this->state_,
            (this->optimized_clear_value.Format != DXGI_FORMAT_UNKNOWN) ? &this->optimized_clear_value : nullptr,
            IID_PPV_ARGS(&this->resource_))))
        {
            return false;
        }
    }
    else
    {
        if (FAILED(ff::dx12::device()->CreatePlacedResource(
            ff::dx12::get_heap(*this->mem_range_->heap()),
            this->mem_range_->start(),
            &this->desc_,
            this->state_,
            (this->optimized_clear_value.Format != DXGI_FORMAT_UNKNOWN) ? &this->optimized_clear_value : nullptr,
            IID_PPV_ARGS(&this->resource_))))
        {
            return false;
        }
    }

    return true;
}
