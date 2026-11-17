/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "StagedFile.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

std::expected<staged_file::File, staged_file::Error> staged_file::File::Create(const std::filesystem::path& destination)
{
    if (destination.empty() || !destination.has_filename()) return std::unexpected(Error::invalid_path);
    auto savedDestination = destination;
    static std::atomic<std::uint64_t> sequence{0};
    const auto parent = destination.has_parent_path() ? destination.parent_path() : std::filesystem::path{"."};
    for (int attempt = 0; attempt < 128; ++attempt)
    {
        const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
        auto directory = parent / (".ukncbtl-output-" + std::to_string(time) + "-" + std::to_string(sequence.fetch_add(1)));
        std::error_code error;
        if (std::filesystem::create_directory(directory, error)) return File(std::move(directory), std::move(savedDestination));
        if (error && error != std::errc::file_exists) return std::unexpected(Error::create_failed);
    }
    return std::unexpected(Error::create_failed);
}

staged_file::File::File(File&& other) noexcept
    : directory_(std::move(other.directory_)), destination_(std::move(other.destination_))
{
    other.directory_.clear();
}

staged_file::File::~File()
{
    if (directory_.empty()) return;
    std::error_code error;
    std::filesystem::remove(Path(), error);
    std::filesystem::remove(directory_, error);
}

std::expected<void, staged_file::Error> staged_file::File::Commit()
{
    if (directory_.empty()) return std::unexpected(Error::replace_failed);
    std::error_code error;
    std::filesystem::rename(Path(), destination_, error);
    if (error) return std::unexpected(Error::replace_failed);
    return {};
}
