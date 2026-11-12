/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PrinterOutput.h"
#include <utility>

bool PrinterOutput::Open(const std::filesystem::path& path)
{
    if (path.empty() || file_.is_open()) return false;
    file_.clear();
    file_.open(path, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(file_);
}

bool PrinterOutput::Write(std::uint8_t value)
{
    if (!file_.is_open()) { error_ = true; return false; }
    file_.put(static_cast<char>(value));
    const bool ok = static_cast<bool>(file_);
    error_ |= !ok;
    return ok;
}

bool PrinterOutput::Flush()
{
    if (!file_.is_open()) { error_ = true; return false; }
    file_.flush();
    const bool ok = static_cast<bool>(file_);
    error_ |= !ok;
    return ok;
}

bool PrinterOutput::Close()
{
    if (!file_.is_open()) return true;
    file_.close();
    const bool ok = static_cast<bool>(file_);
    error_ |= !ok;
    file_.clear();
    return ok;
}

bool PrinterOutput::TakeError()
{
    return std::exchange(error_, false);
}
