#pragma once

#if DXVER == 12

namespace ff
{
    class dx12_command_queue;
    class dx12_command_queues;

    class dx12_commands
    {
    public:
        dx12_commands(dx12_command_queue& owner, Microsoft::WRL::ComPtr<ID3D12GraphicsCommandListX>&& list, Microsoft::WRL::ComPtr<ID3D12CommandAllocatorX>&& allocator);
        dx12_commands(dx12_commands&& other) noexcept = default;
        dx12_commands(const dx12_commands& other) = delete;
        ~dx12_commands();

        dx12_commands& operator=(dx12_commands&& other) noexcept = default;
        dx12_commands& operator=(const dx12_commands& other) = delete;

        ID3D12GraphicsCommandListX* get() const;
        uint64_t execute();
        static uint64_t execute(dx12_commands* const* first, size_t count);

    private:
        dx12_command_queue& owner;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandListX> list;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocatorX> allocator;
        uint64_t allocator_fence_value;
    };

    class dx12_command_queue
    {
    public:
        dx12_command_queue(dx12_command_queues& owner, D3D12_COMMAND_LIST_TYPE type, uint64_t initial_fence_value);
        dx12_command_queue(dx12_command_queue&& other) noexcept = default;
        dx12_command_queue(const dx12_command_queue& other) = delete;
        ~dx12_command_queue();

        dx12_command_queue& operator=(dx12_command_queue&& other) noexcept = default;
        dx12_command_queue& operator=(const dx12_command_queue& other) = delete;

        ID3D12CommandQueueX* get() const;

        uint64_t signal_fence();
        bool fence_complete(uint64_t value);
        void wait_for_fence(uint64_t value);

        dx12_commands new_commands(ID3D12PipelineStateX* initial_state = nullptr);
        void return_commands(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandListX>&& list, Microsoft::WRL::ComPtr<ID3D12CommandAllocatorX>&& allocator, uint64_t allocator_fence_value);

    private:
        bool internal_fence_complete(uint64_t value);

        dx12_command_queues& owner;
        D3D12_COMMAND_LIST_TYPE type;
        Microsoft::WRL::ComPtr<ID3D12CommandQueueX> command_queue;

        Microsoft::WRL::ComPtr<ID3D12FenceX> fence;
        ff::win_handle fence_event;
        std::mutex completed_fence_mutex;
        std::mutex next_fence_mutex;
        uint64_t completed_fence_value;
        uint64_t next_fence_value;

        std::mutex lists_mutex;
        std::list<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandListX>> lists;

        std::mutex allocators_mutex;
        std::list<std::pair<uint64_t, Microsoft::WRL::ComPtr<ID3D12CommandAllocatorX>>> allocators;
    };

    class dx12_command_queues
    {
    public:
        dx12_command_queues(ID3D12DeviceX* device);
        dx12_command_queues(dx12_command_queues&& other) noexcept = default;
        dx12_command_queues(const dx12_command_queues& other) = delete;

        dx12_command_queues& operator=(dx12_command_queues&& other) noexcept = default;
        dx12_command_queues& operator=(const dx12_command_queues& other) = delete;

        ID3D12DeviceX* device() const;
        dx12_command_queue& direct();
        dx12_command_queue& compute();
        dx12_command_queue& copy();
        dx12_command_queue& from_type(D3D12_COMMAND_LIST_TYPE type);
        dx12_command_queue& from_fence(uint64_t value);

    private:
        Microsoft::WRL::ComPtr<ID3D12DeviceX> device_;
        dx12_command_queue direct_queue;
        dx12_command_queue compute_queue;
        dx12_command_queue copy_queue;
    };
}

#endif
