/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "DebugLogFile.h"
#include "IniEncoding.h"
#include <utility>

DebugLogFile::DebugLogFile(std::filesystem::path path) : path_(std::move(path)) {}

bool DebugLogFile::Open()
{
    if (file_.is_open()) return true;
    if (path_.empty()) return false;
    file_.clear();
    file_.open(path_, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(file_);
}

bool DebugLogFile::Clear()
{
    Close();
    return Open();
}

bool DebugLogFile::Append(std::string_view bytes)
{
    if (!Open()) return false;
    file_.clear();
    file_.seekp(0, std::ios::end);
    if (!bytes.empty()) file_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file_.flush();
    return static_cast<bool>(file_);
}

bool DebugLogFile::AppendText(std::wstring_view text)
{
    const auto encoded = ini_encoding::Encode(text, ini_encoding::Encoding::utf8);
    if (!encoded) return false;
    if (encoded->empty()) return Append({});
    return Append(std::string_view(reinterpret_cast<const char*>(encoded->data()), encoded->size()));
}

void DebugLogFile::Close()
{
    if (file_.is_open()) file_.close();
    file_.clear();
}
