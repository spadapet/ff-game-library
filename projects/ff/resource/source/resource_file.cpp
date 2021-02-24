#include "pch.h"
#include "resource_file.h"
#include "resource_load_context.h"

static bool default_compress(const std::filesystem::path& path)
{
    std::string extension = ff::filesystem::to_string(ff::filesystem::to_lower(path.extension()));

    if (extension == ".mp3" ||
        extension == ".png" ||
        extension == ".jpg")
    {
        return false;
    }

    return true;
}

#if !UWP_APP

ff::resource_file::resource_file(std::string_view file_extension, HINSTANCE instance, const wchar_t* rc_type, const wchar_t* rc_name)
    : file_extension_(file_extension)
    , compress(::default_compress(file_extension))
{
    std::shared_ptr<ff::data_base> data = std::make_shared<ff::data_static>(instance, rc_type, rc_name);
    this->saved_data_ = std::make_shared<ff::saved_data_static>(data, data->size(), ff::saved_data_type::none);
}

#endif

ff::resource_file::resource_file(const std::filesystem::path& path)
    : file_extension_(ff::filesystem::to_string(ff::filesystem::to_lower(path.extension())))
    , compress(::default_compress(path))
{
    std::error_code ec;
    uintmax_t size = std::filesystem::file_size(path, ec);

    if (size == static_cast<std::uintmax_t>(-1))
    {
        assert(false);
        size = 0;
    }

    this->saved_data_ = std::make_shared<ff::saved_data_file>(path, 0, static_cast<size_t>(size), static_cast<size_t>(size), ff::saved_data_type::none);
}

ff::resource_file::resource_file(std::shared_ptr<ff::saved_data_base> saved_data, std::string_view file_extension, bool compress)
    : saved_data_(saved_data)
    , file_extension_(file_extension)
    , compress(compress)
{
    assert(this->saved_data_);
}

std::shared_ptr<ff::data_base> ff::resource_file::loaded_data() const
{
    return this->saved_data_ ? this->saved_data_->loaded_data() : nullptr;
}

const std::shared_ptr<ff::saved_data_base>& ff::resource_file::saved_data() const
{
    return this->saved_data_;
}

const std::string& ff::resource_file::file_extension() const
{
    return this->file_extension_;
}

bool ff::resource_file::resource_save_to_file(const std::filesystem::path& directory_path, std::string_view name) const
{
    std::string temp_name(name);
    temp_name += this->file_extension_;

    std::filesystem::path path = directory_path / ff::filesystem::to_path(temp_name);
    size_t size = this->saved_data_->loaded_size();
    size_t copied_size = ff::stream_copy(ff::file_writer(path), *this->saved_data_->loaded_reader(), size);
    return copied_size == size;
}

bool ff::resource_file::save_to_cache(ff::dict& dict, bool& allow_compress) const
{
    allow_compress = this->compress;

    dict.set<ff::saved_data_base>("data", this->saved_data_);
    dict.set<std::string>("extension", this->file_extension_);
    dict.set<bool>("compress", this->compress);

    return true;
}

std::shared_ptr<ff::resource_object_base> ff::internal::resource_file_factory::load_from_source(const ff::dict& dict, resource_load_context& context) const
{
    std::filesystem::path path = dict.get<std::string>("file");

    if (path.empty())
    {
        context.add_error("Missing 'file' value");
        return nullptr;
    }

    std::error_code ec;
    uintmax_t max_size = std::filesystem::file_size(path, ec);
    if (max_size == static_cast<std::uintmax_t>(-1))
    {
        std::ostringstream str;
        str << "Failed to get size of file: " << path;
        context.add_error(str.str());
        return nullptr;
    }

    size_t size = static_cast<size_t>(max_size);
    auto saved_data = std::make_shared<ff::saved_data_file>(path, 0, size, size, ff::saved_data_type::none);

    std::string file_extension = ff::filesystem::to_string(ff::filesystem::to_lower(path.extension()));
    bool compress = dict.get<bool>("compress", ::default_compress(path));
    return std::make_shared<resource_file>(saved_data, file_extension, compress);
}

std::shared_ptr<ff::resource_object_base> ff::internal::resource_file_factory::load_from_cache(const ff::dict& dict) const
{
    auto saved_data = dict.get<ff::saved_data_base>("data");
    std::string file_extension = dict.get<std::string>("extension");
    bool compress = dict.get<bool>("compress", true);

    return saved_data ? std::make_shared<resource_file>(saved_data, file_extension, compress) : nullptr;
}
