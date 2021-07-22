#pragma once

#include "target_base.h"

namespace ff
{
    class target_window_base : public ff::target_base
    {
    public:
        using ff::target_base::size;

        virtual bool size(const ff::window_size& size) = 0;
        virtual ff::signal_sink<ff::window_size>& size_changed() = 0;

        virtual bool allow_full_screen() const = 0;
        virtual bool full_screen() = 0;
        virtual bool full_screen(bool value) = 0;
    };
}
