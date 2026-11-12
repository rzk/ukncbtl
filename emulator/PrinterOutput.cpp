/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PrinterOutput.h"

bool PrinterOutput::Open(const std::filesystem::path& path)
{
    if (path.empty() || file_.is_open()) return false;
    file_.clear();
    file_.open(path, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(file_);
}

bool PrinterOutput::Write(std::uint8_t value)
{
    if (!file_.is_open()) return false;
    file_.put(static_cast<char>(value));
    return static_cast<bool>(file_);
}

bool PrinterOutput::Flush()
{
    if (!file_.is_open()) return false;
    file_.flush();
    return static_cast<bool>(file_);
}

bool PrinterOutput::Close()
{
    if (!file_.is_open()) return true;
    file_.close();
    const bool ok = static_cast<bool>(file_);
    file_.clear();
    return ok;
}
