/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "DisplayListDump.h"
#include <format>
#include <fstream>
#include <ostream>

std::string display_list_dump::FormatLine(int line, const video::DisplayTag& tag)
{
    auto result = std::format("{:4d}  {:06o}  ", line, tag.address);
    if (tag.kind != video::TagKind::Short) result += std::format("{:06o} {:06o} ", tag.tag1, tag.tag2);
    else result += "------ ------ ";
    result += std::format("{:06o} {:06o}  ", tag.pixels, tag.link);
    if (tag.kind == video::TagKind::Palette) result += "palette; ";
    else if (tag.kind == video::TagKind::Parameters) result += std::format("scale {}; ", tag.Scale());
    if (tag.NextKind() != video::TagKind::Short) result += std::format("next: 4-word at {:06o}", tag.NextAddress());
    result += '\n';
    return result;
}

bool display_list_dump::Write(std::ostream& output, const CMotherboard& board)
{
    output << "line address  tag 1  tag 2  addr   next   \n";
    if (!output) return false;
    video::DisplayListCursor cursor;
    for (int line = 0; line < video::TotalLines; ++line)
    {
        output << FormatLine(line, cursor.Next(board));
        if (!output) return false;
    }
    return true;
}

std::expected<void, display_list_dump::FileError> display_list_dump::Save(
    const std::filesystem::path& path, const CMotherboard& board)
{
    if (path.empty()) return std::unexpected(FileError::open_failed);
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file) return std::unexpected(FileError::open_failed);
    const bool written = Write(file, board);
    file.flush();
    const bool flushed = static_cast<bool>(file);
    file.close();
    if (!written || !flushed) return std::unexpected(FileError::write_failed);
    if (!file) return std::unexpected(FileError::close_failed);
    return {};
}
