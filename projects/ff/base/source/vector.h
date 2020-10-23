#pragma once

namespace ff::internal
{
    template<class T, size_t StackSize, class Enabled = void>
    class vector_stack_storage;

    template<class T, size_t StackSize>
    class alignas(T) vector_stack_storage<T, StackSize, std::enable_if_t<StackSize != 0>>
    {
    protected:
        T* get_stack_items()
        {
            return reinterpret_cast<T*>(this->item_stack);
        }

    private:
        alignas(T) uint8_t item_stack[sizeof(T) * StackSize];
    };

    template<class T, size_t StackSize>
    class vector_stack_storage<T, StackSize, std::enable_if_t<StackSize == 0>>
    {
    protected:
        constexpr T* get_stack_items()
        {
            return nullptr;
        }
    };

    class vector_type_helper
    {
    protected:
        template<class T, class... Args>
        static typename std::enable_if_t<!std::is_trivially_constructible<T, Args...>::value> construct_item(T* data, Args&&... args)
        {
            ::new(data) T(std::forward<Args>(args)...);
        }

        template<class T>
        static typename std::enable_if_t<std::is_trivially_constructible<T>::value> construct_item(T* data)
        {
        }

        template<class T>
        static typename std::enable_if_t<!std::is_trivially_default_constructible<T>::value> default_construct_items(T* data, size_t count)
        {
            for (T* endData = data + count; data != endData; data++)
            {
                ff::internal::vector_type_helper::construct_item(data);
            }
        }

        template<class T>
        static typename std::enable_if_t<std::is_trivially_default_constructible<T>::value> default_construct_items(T* data, size_t count)
        {
        }

        template<class T>
        static typename std::enable_if_t<!std::is_trivially_copy_constructible<T>::value> copy_construct_item(T* data, const T& source)
        {
            ff::internal::vector_type_helper::construct_item(data, source);
        }

        template<class T>
        static typename std::enable_if_t<std::is_trivially_copy_constructible<T>::value> copy_construct_item(T* data, const T& source)
        {
            std::memcpy(data, &source, sizeof(T));
        }

        template<class T>
        static typename std::enable_if_t<!std::is_trivially_move_constructible<T>::value> move_construct_item(T* data, T&& source)
        {
            ff::internal::vector_type_helper::construct_item(data, std::move(source));
        }

        template<class T>
        static typename std::enable_if_t<std::is_trivially_move_constructible<T>::value> move_construct_item(T* data, T&& source)
        {
            std::memcpy(data, &source, sizeof(T));
        }

        template<class T>
        static typename std::enable_if_t<!std::is_trivially_move_constructible<T>::value> shift_items(T* destData, T* sourceData, size_t count)
        {
            if (destData < sourceData)
            {
                for (T* endDestData = destData + count; destData != endDestData; destData++, sourceData++)
                {
                    ff::internal::vector_type_helper::move_construct_item(destData, std::move(*sourceData));
                    ff::internal::vector_type_helper::destruct_item(sourceData);
                }
            }
            else if (destData > sourceData)
            {
                for (T* endDestData = destData, *curDestData = destData + count, *curSourceData = sourceData + count;
                    curDestData != endDestData; curDestData--, curSourceData--)
                {
                    ff::internal::vector_type_helper::move_construct_item(curDestData - 1, std::move(curSourceData[-1]));
                    ff::internal::vector_type_helper::destruct_item(curSourceData - 1);
                }
            }
        }

        template<class T>
        static typename std::enable_if_t<std::is_trivially_move_constructible<T>::value> shift_items(T* destData, T* sourceData, size_t count)
        {
            std::memmove(destData, sourceData, count * sizeof(T));
        }

        template<class T>
        static typename std::enable_if_t<!std::is_trivially_destructible<T>::value> destruct_item(T* data)
        {
            data->~T();
        }

        template<class T>
        static typename std::enable_if_t<std::is_trivially_destructible<T>::value> destruct_item(T* data)
        {
        }

        template<class T>
        static typename std::enable_if_t<!std::is_trivially_destructible<T>::value> destruct_items(T* data, size_t count)
        {
            for (T* endData = data + count; data != endData; data++)
            {
                ff::internal::vector_type_helper::destruct_item(data);
            }
        }

        template<class T>
        static typename std::enable_if_t<std::is_trivially_destructible<T>::value> destruct_items(T* data, size_t count)
        {
        }
    };

    template<class T>
    constexpr bool is_iterator_t = std::_Is_iterator_v<T>;
}

namespace ff
{
    /// <summary>
    /// Replacement class for std::vector
    /// </summary>
    /// <remarks>
    /// This is a drop-in replacement for the std::vector class, but it allows stack storage.
    /// </remarks>
    /// <typeparam name="T">Item type</typeparam>
    /// <typeparam name="StackSize">Initial buffer size on the stack</typeparam>
    /// <typeparam name="Allocator">Used to allocate internal storage</typeparam>
    template<class T, size_t StackSize = 0, class Allocator = std::allocator<T>>
    class vector
        : private ff::internal::vector_stack_storage<T, StackSize>
        , private ff::internal::vector_type_helper
        , private Allocator
    {
    public:
        using vector_type = vector<T, StackSize, Allocator>;
        using value_type = T;
        using reference = T&;
        using const_reference = const T&;
        using allocator_type = typename Allocator;
        using pointer = typename std::allocator_traits<allocator_type>::pointer;
        using const_pointer = typename std::allocator_traits<allocator_type>::const_pointer;
        using size_type = typename std::allocator_traits<allocator_type>::size_type;
        using difference_type = typename std::allocator_traits<allocator_type>::difference_type;
        using iterator = typename pointer;
        using const_iterator = typename const_pointer;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        explicit vector(const Allocator& alloc) noexcept
            : item_data(this->get_stack_items())
            , item_cap(StackSize)
            , item_size(0)
        {
        }

        vector() noexcept
            : vector(Allocator())
        {
        }

        vector(size_type count, const T& value, const Allocator& alloc = Allocator())
            : vector(alloc)
        {
            this->assign(count, value);
        }

        explicit vector(size_type count, const Allocator& alloc = Allocator())
            : vector(alloc)
        {
            this->resize(count);
        }

        template<class InputIt, std::enable_if_t<ff::internal::is_iterator_t<InputIt>, int> = 0>
        vector(InputIt first, InputIt last, const Allocator& alloc = Allocator())
            : vector(alloc)
        {
            this->assign(first, last);
        }

        vector(const vector_type& other)
            : vector(other.get_allocator())
        {
            this->assign(other.cbegin(), other.cend());
        }

        vector(const vector_type& other, const Allocator& alloc)
            : vector(alloc)
        {
            this->assign(other.cbegin(), other.cend());
        }

        vector(vector_type&& other) noexcept
            : vector(other.get_allocator())
        {
            *this = std::move(other);
        }

        vector(vector_type&& other, const Allocator& alloc)
            : vector(alloc)
        {
            *this = std::move(other);
        }

        vector(std::initializer_list<T> init, const Allocator& alloc = Allocator())
            : vector(alloc)
        {
            this->assign(init);
        }

        ~vector()
        {
            this->clear();
            this->deallocate_item_data();
        }

        vector_type& operator=(const vector_type& other)
        {
            if (this != &other)
            {
                this->assign(other.cbegin(), other.cend());
            }

            return *this;
        }

        vector_type& operator=(vector_type&& other) noexcept(std::allocator_traits<Allocator>::propagate_on_container_move_assignment::value || std::allocator_traits<Allocator>::is_always_equal::value)
        {
            if (this != &other)
            {
                this->clear();
                this->deallocate_item_data();

                if (other.item_data == other.get_stack_items())
                {
                    ff::internal::vector_type_helper::shift_items(this->item_data, other.item_data, other.item_size);
                }
                else
                {
                    this->item_data = other.item_data;
                }

                this->item_cap = other.item_cap;
                this->item_size = other.item_size;

                other.item_data = other.get_stack_items();
                other.item_cap = StackSize;
                other.item_size = 0;
            }

            return *this;
        }

        vector_type& operator=(std::initializer_list<T> ilist)
        {
            this->assign(ilist);
            return *this;
        }

        void assign(size_type count, const T& value)
        {
            this->clear();
            this->insert(this->cbegin(), count, value);
        }

        template<class InputIt, std::enable_if_t<ff::internal::is_iterator_t<InputIt>, int> = 0>
        void assign(InputIt first, InputIt last)
        {
            this->clear();
            this->insert(this->cbegin(), first, last);
        }

        void assign(std::initializer_list<T> ilist)
        {
            this->clear();
            this->insert(this->cbegin(), ilist);
        }

        allocator_type get_allocator() const noexcept
        {
            return Allocator();
        }

        const_reference at(size_type pos) const
        {
            assert(pos < this->item_size);
            return this->item_data[pos];
        }

        reference at(size_type pos)
        {
            assert(pos < this->item_size);
            return this->item_data[pos];
        }

        reference operator[](size_type pos)
        {
            return this->at(pos);
        }

        const_reference operator[](size_type pos) const
        {
            return this->at(pos);
        }

        reference front()
        {
            assert(this->item_size);
            return this->item_data[0];
        }

        const_reference front() const
        {
            assert(this->item_size);
            return this->item_data[0];
        }

        reference back()
        {
            assert(this->item_size);
            return this->item_data[this->item_size - 1];
        }

        const_reference back() const
        {
            assert(this->item_size);
            return this->item_data[this->item_size - 1];
        }

        T* data() noexcept
        {
            return this->item_data;
        }

        const T* data() const noexcept
        {
            return this->item_data;
        }

        iterator begin() noexcept
        {
            return iterator(this->item_data);
        }

        const_iterator begin() const noexcept
        {
            return const_iterator(this->item_data);
        }

        const_iterator cbegin() const noexcept
        {
            return const_iterator(this->item_data);
        }

        iterator end() noexcept
        {
            return iterator(this->item_data + this->item_size);
        }

        const_iterator end() const noexcept
        {
            return const_iterator(this->item_data + this->item_size);
        }

        const_iterator cend() const noexcept
        {
            return const_iterator(this->item_data + this->item_size);
        }

        reverse_iterator rbegin() noexcept
        {
            return reverse_iterator(this->end());
        }

        const_reverse_iterator rbegin() const noexcept
        {
            return const_reverse_iterator(this->cend());
        }

        const_reverse_iterator crbegin() const noexcept
        {
            return const_reverse_iterator(this->cend());
        }

        reverse_iterator rend() noexcept
        {
            return reverse_iterator(this->begin());
        }

        const_reverse_iterator rend() const noexcept
        {
            return const_reverse_iterator(this->cbegin());
        }

        const_reverse_iterator crend() const noexcept
        {
            return const_reverse_iterator(this->cbegin());
        }

        bool empty() const noexcept
        {
            return this->item_size == 0;
        }

        size_type size() const noexcept
        {
            return this->item_size;
        }

        size_type max_size() const noexcept
        {
            return std::numeric_limits<size_type>::max();
        }

        void reserve(size_type new_cap)
        {
            this->reserve_item_data(this->cbegin(), 0, new_cap);
        }

        size_type capacity() const noexcept
        {
            return this->item_cap;
        }

        void shrink_to_fit()
        {
            if (this->item_size < this->item_cap && this->item_data != this->get_stack_items())
            {
                if (this->item_size == 0)
                {
                    this->deallocate_item_data();
                }
                else
                {
                    size_type new_size = this->item_size;
                    size_type new_cap = StackSize;
                    T* new_data = (new_size <= new_cap) ? this->get_stack_items() : this->allocate_item_data(this->item_size, new_cap);
                    ff::internal::vector_type_helper::shift_items(new_data, this->item_data, this->item_size);
                    this->deallocate_item_data();

                    this->item_data = new_data;
                    this->item_cap = new_cap;
                    this->item_size = new_size;
                }
            }
        }

        void clear() noexcept
        {
            this->erase(this->cbegin(), this->cend());
        }

        iterator insert(const_iterator pos, const T& value)
        {
            iterator iter = this->reserve_item_data(pos, 1);
            ff::internal::vector_type_helper::copy_construct_item(&*iter, value);
            return iter;
        }

        iterator insert(const_iterator pos, T&& value)
        {
            iterator iter = this->reserve_item_data(pos, 1);
            ff::internal::vector_type_helper::move_construct_item(&*iter, std::move(value));
            return iter;
        }

        iterator insert(const_iterator pos, size_type count, const T& value)
        {
            iterator iter = this->reserve_item_data(pos, count);
            for (size_type i = 0; i < count; i++)
            {
                ff::internal::vector_type_helper::copy_construct_item(&iter[i], value);
            }

            return iter;
        }

        template<class InputIt, std::enable_if_t<ff::internal::is_iterator_t<InputIt>, int> = 0>
        iterator insert(const_iterator pos, InputIt first, InputIt last)
        {
            return this->insert_range(pos, first, last, std::iterator_traits<InputIt>::iterator_category{});
        }

        iterator insert(const_iterator pos, std::initializer_list<T> ilist)
        {
            return this->insert(pos, ilist.begin(), ilist.end());
        }

        template<class... Args>
        iterator emplace(const_iterator pos, Args&&... args)
        {
            iterator iter = this->reserve_item_data(pos, 1);
            ff::internal::vector_type_helper::construct_item(&*iter, std::forward<Args>(args)...);
            return iter;
        }

        iterator erase(const_iterator pos)
        {
            return this->erase(pos, pos + 1);
        }

        iterator erase(const_iterator first, const_iterator last)
        {
            assert(first >= this->cbegin() && last >= first && last <= this->cend());

            size_type i = first - this->cbegin();
            size_type count = last - first;

            if (count < this->item_size && this->item_cap == 0)
            {
                // Copy read-only static data before erasing
                this->reserve_item_data(this->cbegin(), 0);
            }

            ff::internal::vector_type_helper::destruct_items(this->item_data + i, count);
            ff::internal::vector_type_helper::shift_items(this->item_data + i, this->item_data + i + count, this->item_size - i - count);
            this->item_size -= count;

            return iterator(this->item_data + i);
        }

        void push_back(const T& value)
        {
            this->insert(this->cend(), value);
        }

        void push_back(T&& value)
        {
            this->insert(this->cend(), std::move(value));
        }

        template<class... Args>
        reference emplace_back(Args&&... args)
        {
            return *this->emplace(this->cend(), std::forward<Args>(args)...);
        }

        void pop_back()
        {
            assert(this->item_size);
            this->erase(this->cend() - 1, this->cend());
        }

        void resize(size_type count)
        {
            if (count < this->item_size)
            {
                this->erase(this->cend() - count, this->cend());
            }
            else if (count > this->item_size)
            {
                size_type new_count = count - this->item_size;
                iterator i = this->reserve_item_data(this->cend(), new_count);
                ff::internal::vector_type_helper::default_construct_items(&*i, new_count);
            }
        }

        void resize(size_type count, const value_type& value)
        {
            if (count < this->item_size)
            {
                this->erase(this->cend() - count, this->cend());
            }
            else if (count > this->item_size)
            {
                this->insert(this->cend(), count, value);
            }
        }

        void swap(vector_type& other)
        {
            vector_type temp = std::move(other);
            other = std::move(*this);
            *this = std::move(temp);
        }

    private:
        template<class InputIt>
        iterator insert_range(const_iterator pos, InputIt first, InputIt last, std::input_iterator_tag)
        {
            vector_type temp;
            for (; first != last; ++first)
            {
                temp.push_back(*first);
            }

            iterator iter = this->reserve_item_data(pos, temp.size());
            for (size_type i = 0; i < temp.size(); i++)
            {
                ff::internal::vector_type_helper::move_construct_item(&iter[i], std::move(temp[i]));
            }

            return iter;
        }

        template<class InputIt>
        iterator insert_range(const_iterator pos, InputIt first, InputIt last, std::forward_iterator_tag)
        {
            iterator iter = this->reserve_item_data(pos, std::distance(first, last));
            for (; first != last; ++first, ++iter)
            {
                ff::internal::vector_type_helper::copy_construct_item(&*iter, *first);
            }

            return iter;
        }

        iterator reserve_item_data(const_iterator pos, size_type count, size_type new_cap = 0)
        {
            difference_type pos_index = pos - this->cbegin();
            assert(pos_index >= 0 && static_cast<size_type>(pos_index) <= this->item_size);

            size_type new_size = this->item_size + count;
            new_cap = std::max(new_cap, this->item_size + count);

            if (new_cap > this->item_cap)
            {
                T* new_data = this->allocate_item_data(new_cap, new_cap);
                ff::internal::vector_type_helper::shift_items(new_data, this->item_data, pos_index);
                ff::internal::vector_type_helper::shift_items(new_data + pos_index + count, this->item_data + pos_index, this->item_size - pos_index);
                this->deallocate_item_data();

                this->item_data = new_data;
                this->item_cap = new_cap;
            }
            else if (static_cast<size_type>(pos_index) < this->item_size)
            {
                ff::internal::vector_type_helper::shift_items(this->item_data + pos_index + count, this->item_data + pos_index, this->item_size - pos_index);
            }

            this->item_size = new_size;

            return iterator(this->item_data + pos_index);
        }

        T* allocate_item_data(size_type capacity_requested, size_type &capacity)
        {
            capacity = std::max<size_t>(ff::math::nearest_power_of_two(capacity_requested), 16);
            return Allocator::allocate(capacity);
        }

        void deallocate_item_data()
        {
            if (this->item_data != this->get_stack_items())
            {
                if (this->item_cap)
                {
                    Allocator::deallocate(this->item_data, this->item_cap);
                }

                this->item_data = this->get_stack_items();
            }

            this->item_cap = StackSize;
            this->item_size = 0;
        }

        T* item_data;
        size_type item_cap;
        size_type item_size;
    };
}

template<class T, size_t StackSize, class Allocator>
bool operator==(const ff::vector<T, StackSize, Allocator>& lhs, const ff::vector<T, StackSize, Allocator>& rhs)
{
    return lhs.size() == rhs.size() && std::equal(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend());
}

template<class T, size_t StackSize, class Allocator>
bool operator!=(const ff::vector<T, StackSize, Allocator>& lhs, const ff::vector<T, StackSize, Allocator>& rhs)
{
    return !(lhs == rhs);
}

template<class T, size_t StackSize, class Allocator>
bool operator<(const ff::vector<T, StackSize, Allocator>& lhs, const ff::vector<T, StackSize, Allocator>& rhs)
{
    return std::lexicographical_compare(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend());
}

template<class T, size_t StackSize, class Allocator>
bool operator<=(const ff::vector<T, StackSize, Allocator>& lhs, const ff::vector<T, StackSize, Allocator>& rhs)
{
    return !(rhs < lhs);
}

template<class T, size_t StackSize, class Allocator>
bool operator>(const ff::vector<T, StackSize, Allocator>& lhs, const ff::vector<T, StackSize, Allocator>& rhs)
{
    return rhs < lhs;
}

template<class T, size_t StackSize, class Allocator>
bool operator>=(const ff::vector<T, StackSize, Allocator>& lhs, const ff::vector<T, StackSize, Allocator>& rhs)
{
    return !(lhs < rhs);
}

namespace std
{
    template<class T, size_t StackSize, class Allocator>
    void swap(ff::vector<T, StackSize, Allocator>& lhs, ff::vector<T, StackSize, Allocator>& rhs) noexcept(noexcept(lhs.swap(rhs)))
    {
        lhs.swap(rhs);
    }
}
