/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <filesystem>

namespace path_identity
{
// Use the filesystem identity when both paths exist. For pending outputs,
// resolve the existing parent components and compare their normalized paths.
inline bool SameFile(const std::filesystem::path& first, const std::filesystem::path& second)
{
    if (first.empty() || second.empty()) return false;
    if (first == second) return true;
    std::error_code error;
    if (std::filesystem::equivalent(first, second, error) && !error) return true;
    error.clear();
    const auto a = std::filesystem::weakly_canonical(first, error);
    if (error) return first.lexically_normal() == second.lexically_normal();
    const auto b = std::filesystem::weakly_canonical(second, error);
    if (error) return first.lexically_normal() == second.lexically_normal();
    if (a == b) return true;
    return a.filename() == b.filename() &&
        std::filesystem::equivalent(a.parent_path(), b.parent_path(), error) && !error;
}
}
