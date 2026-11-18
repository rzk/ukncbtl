/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <expected>
#include <filesystem>
#include <utility>

namespace staged_file
{
enum class Error { invalid_path, create_failed, replace_failed };

// A uniquely created sibling directory keeps the temporary file on the same filesystem.
// Destruction removes only the staging file and directory created by this object.
class File
{
public:
    [[nodiscard]] static std::expected<File, Error> Create(const std::filesystem::path& destination);
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& other) noexcept;
    File& operator=(File&&) = delete;
    ~File();
    [[nodiscard]] std::filesystem::path Path() const { return directory_ / "output"; }
    [[nodiscard]] std::expected<void, Error> Commit();
private:
    File(std::filesystem::path directory, std::filesystem::path destination)
        : directory_(std::move(directory)), destination_(std::move(destination)) {}
    std::filesystem::path directory_, destination_;
};
}
