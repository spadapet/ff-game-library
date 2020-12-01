#pragma once

#include "constants.h"

/// <summary>
/// Useful math helper functions
/// </summary>
namespace ff::math
{
    /// <summary>
    /// PI cast to a specific type
    /// </summary>
    /// <typeparam name="T">Type to return</typeparam>
    template<class T>
    constexpr T pi()
    {
        return static_cast<T>(ff::constants::pi);
    }

    /// <summary>
    /// 2 * PI cast to a specific type
    /// </summary>
    /// <typeparam name="T">Type to return</typeparam>
    template<class T>
    constexpr T pi2()
    {
        return static_cast<T>(ff::constants::pi2);
    }

    /// <summary>
    /// Convert radians to degrees
    /// </summary>
    /// <typeparam name="T">Type to return</typeparam>
    template<class T>
    constexpr T radians_to_degrees(T angle)
    {
        return angle * static_cast<T>(180.0) / ff::math::pi<T>();
    }

    /// <summary>
    /// Convert degrees to radians
    /// </summary>
    /// <typeparam name="T">Type to return</typeparam>
    template<class T>
    constexpr T degrees_to_radians(T angle)
    {
        return angle * ff::math::pi<T>() / static_cast<T>(180.0);
    }

    /// <summary>
    /// Round the input up to the next power of two, or return the input if it's already a power of two.
    /// </summary>
    /// <typeparam name="T">Type to return</typeparam>
    template<class T>
    constexpr T nearest_power_of_two(T num)
    {
        num -= (num != static_cast<T>(0));

        num |= num >> 1;
        num |= num >> 2;
        num |= num >> 4;

        if constexpr (sizeof(T) >= 2)
        {
            num |= num >> 8;
        }

        if constexpr (sizeof(T) >= 4)
        {
            num |= num >> 16;
        }

        if constexpr (sizeof(T) >= 8)
        {
            num |= num >> 32;
        }

        return num + 1;
    }

    template<class T>
    constexpr size_t round_up(T value, T multiple)
    {
        return (value + multiple - static_cast<T>(1)) / multiple * multiple;
    }
}
