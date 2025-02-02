#include "pch.h"
#include "base/assert.h"
#include "base/constants.h"
#include "data_persist/data.h"
#include "data_persist/file.h"
#include "data_persist/stream.h"

ff::data_static::data_static(const void* data, size_t size)
    : data_(reinterpret_cast<const uint8_t*>(data))
    , size_(size)
{}

ff::data_static::data_static(HINSTANCE instance, const wchar_t* rc_type, const wchar_t* rc_name)
{
    HRSRC res_found = ::FindResourceW(instance, rc_name, rc_type);
    HGLOBAL res = res_found ? ::LoadResource(instance, res_found) : nullptr;
    this->data_ = res ? reinterpret_cast<const uint8_t*>(::LockResource(res)) : nullptr;
    this->size_ = this->data_ ? ::SizeofResource(instance, res_found) : 0;
    assert(this->size_);
}

size_t ff::data_static::size() const
{
    return this->size_;
}

const uint8_t* ff::data_static::data() const
{
    return this->data_;
}

std::shared_ptr<ff::data_base> ff::data_static::subdata(size_t offset, size_t size) const
{
    assert(offset + size <= this->size_);
    return std::make_shared<ff::data_static>(this->data_ + offset, size);
}

ff::data_mem_mapped::data_mem_mapped(const std::shared_ptr<file_mem_mapped>& file)
    : data_mem_mapped(file, 0, file->size())
{}

ff::data_mem_mapped::data_mem_mapped(const std::shared_ptr<file_mem_mapped>& file, size_t offset, size_t size)
    : file_(file)
    , offset_(offset)
    , size_(size)
{
    assert(offset + size <= file->size());
}

ff::data_mem_mapped::data_mem_mapped(const std::filesystem::path& path)
    : data_mem_mapped(file_mem_mapped(path))
{}

ff::data_mem_mapped::data_mem_mapped(file_mem_mapped&& file) noexcept
    : data_mem_mapped(std::make_shared<file_mem_mapped>(std::move(file)))
{}

ff::data_mem_mapped::data_mem_mapped(file_mem_mapped&& file, size_t offset, size_t size) noexcept
    : data_mem_mapped(std::make_shared<file_mem_mapped>(std::move(file)), offset, size)
{}

bool ff::data_mem_mapped::valid() const
{
    return this->file_ && *this->file_ && (this->offset_ + this->size_) <= this->file_->size();
}

const std::shared_ptr<ff::file_mem_mapped>& ff::data_mem_mapped::file() const
{
    return this->file_;
}

size_t ff::data_mem_mapped::offset() const
{
    return this->offset_;
}

size_t ff::data_mem_mapped::size() const
{
    return this->size_;
}

const uint8_t* ff::data_mem_mapped::data() const
{
    return this->file_->data() + this->offset_;
}

std::shared_ptr<ff::data_base> ff::data_mem_mapped::subdata(size_t offset, size_t size) const
{
    assert(this->offset_ + offset + size <= this->file_->size());
    return std::make_shared<data_mem_mapped>(this->file_, this->offset_ + offset, size);
}

ff::data_vector::data_vector(const std::shared_ptr<const std::vector<uint8_t>>& vector)
    : data_vector(vector, 0, ff::constants::invalid_unsigned<size_t>())
{}

ff::data_vector::data_vector(const std::shared_ptr<const std::vector<uint8_t>>& vector, size_t offset, size_t size)
    : vector_(vector)
    , offset_(offset)
    , size_(size)
{}

ff::data_vector::data_vector(std::vector<uint8_t>&& vector) noexcept
    : data_vector(std::make_shared<const std::vector<uint8_t>>(std::move(vector)))
{}

ff::data_vector::data_vector(std::vector<uint8_t>&& vector, size_t offset, size_t size) noexcept
    : data_vector(std::make_shared<const std::vector<uint8_t>>(std::move(vector)), offset, size)
{}

const std::shared_ptr<const std::vector<uint8_t>>& ff::data_vector::vector() const
{
    return this->vector_;
}

size_t ff::data_vector::offset() const
{
    return this->offset_;
}

size_t ff::data_vector::size() const
{
    return (this->size_ == ff::constants::invalid_unsigned<size_t>())
        ? this->vector_->size() - this->offset()
        : this->size_;
}

const uint8_t* ff::data_vector::data() const
{
    return this->vector_->data() + this->offset();
}

std::shared_ptr<ff::data_base> ff::data_vector::subdata(size_t offset, size_t size) const
{
    assert(this->offset() + offset + size <= this->offset() + this->size());
    return std::make_shared<data_vector>(this->vector_, this->offset() + offset, size);
}
