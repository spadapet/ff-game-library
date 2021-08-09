#pragma once

namespace ff::internal
{
    namespace graphics_reset_priorities
    {
        static const int dx12_descriptors_cpu = 100;
        static const int dx12_descriptors_gpu = 99;
        static const int dx12_command_queue = 98;
        static const int dx12_commands = 97;
        static const int normal = 0;
        static const int target_window = -100;
    };

    class graphics_child_base
    {
    public:
        virtual ~graphics_child_base() = default;

        virtual bool reset() = 0;
        virtual int reset_priority() const;
    };
}
