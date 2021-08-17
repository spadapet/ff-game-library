#pragma once

#include "dx12_buffer_range.h"
#include "graphics_child_base.h"

#if DXVER == 12

namespace ff::internal
{
    class dx12_buffer_base
    {
        virtual void free_range(const ff::dx12_buffer_range& range) {}
        virtual void* cpu_address(size_t start) const = 0;
        virtual D3D12_GPU_VIRTUAL_ADDRESS gpu_address(size_t start) const = 0;
    };

    class dx12_buffer_ring : public ff::internal::dx12_buffer_base
    {
    public:
        dx12_buffer_ring(ID3D12HeapX* heap, size_t start, size_t size);
        dx12_buffer_ring(dx12_buffer_ring&& other) noexcept = delete;
        dx12_buffer_ring(const dx12_buffer_ring& other) = delete;

        dx12_buffer_ring& operator=(dx12_buffer_ring&& other) noexcept = delete;
        dx12_buffer_ring& operator=(const dx12_buffer_ring& other) = delete;

        void reset(ID3D12HeapX* heap);
        ff::dx12_buffer_range alloc_bytes(size_t size, size_t align);

        virtual void* cpu_address(size_t start) const override;
        virtual D3D12_GPU_VIRTUAL_ADDRESS gpu_address(size_t start) const override;

    private:
        bool render_frame_complete(uint64_t fence_value);

        struct range_t
        {
            size_t after_end() const;

            size_t start;
            size_t count;
            uint64_t fence_value;
        };

        ff::signal_connection render_frame_complete_connection;
        std::mutex ranges_mutex;
        std::list<range_t> ranges;
        size_t heap_start;
        size_t heap_size;
    };
}

namespace ff
{
    class dx12_allocator_base
    {
    public:
        virtual ~dx12_allocator_base() = default;

        virtual ff::dx12_buffer_range alloc_bytes(size_t size, size_t align) = 0;
    };

    // For transient allocations used in a single frame: Upload buffers, vertexes, or constants
    class dx12_frame_allocator : public ff::dx12_allocator_base, public ff::internal::graphics_child_base
    {
    public:
        dx12_frame_allocator(bool cpu_writable);
        dx12_frame_allocator(dx12_frame_allocator&& other) noexcept = default;
        dx12_frame_allocator(const dx12_frame_allocator& other) = delete;
        ~dx12_frame_allocator();

        dx12_frame_allocator& operator=(dx12_frame_allocator&& other) noexcept = default;
        dx12_frame_allocator& operator=(const dx12_frame_allocator& other) = delete;

        virtual ff::dx12_buffer_range alloc_bytes(size_t size, size_t align) override;

        // graphics_child_base
        virtual bool reset() override;
        virtual int reset_priority() const override;

    private:
    };

    // For long term constants, vertices, and textures
    class dx12_gpu_allocator : public ff::dx12_allocator_base, public ff::internal::graphics_child_base
    {
    public:
        dx12_gpu_allocator();
        dx12_gpu_allocator(dx12_gpu_allocator&& other) noexcept = default;
        dx12_gpu_allocator(const dx12_gpu_allocator& other) = delete;
        ~dx12_gpu_allocator();

        dx12_gpu_allocator& operator=(dx12_gpu_allocator&& other) noexcept = default;
        dx12_gpu_allocator& operator=(const dx12_gpu_allocator& other) = delete;

        virtual ff::dx12_buffer_range alloc_bytes(size_t size, size_t align) override;

        // graphics_child_base
        virtual bool reset() override;
        virtual int reset_priority() const override;

    private:
    };
}

#endif
