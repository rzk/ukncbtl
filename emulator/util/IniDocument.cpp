/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "IniDocument.h"
#include <algorithm>
#include <utility>

namespace
{
std::wstring_view Trim(std::wstring_view text)
{
    const auto start = text.find_first_not_of(L" \t");
    if (start == text.npos) return {};
    return text.substr(start, text.find_last_not_of(L" \t") - start + 1);
}

wchar_t Fold(wchar_t c)
{
    return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c;
}

bool Equal(std::wstring_view a, std::wstring_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](wchar_t x, wchar_t y) { return Fold(x) == Fold(y); });
}

std::optional<std::wstring_view> Section(std::wstring_view line)
{
    line = Trim(line);
    if (line.size() < 2 || line.front() != L'[' || line.back() != L']') return std::nullopt;
    return Trim(line.substr(1, line.size() - 2));
}

std::optional<std::pair<std::wstring_view, std::wstring_view>> Entry(std::wstring_view line)
{
    line = Trim(line);
    if (line.empty() || line.front() == L';' || line.front() == L'#') return std::nullopt;
    const auto delimiter = line.find(L'=');
    if (delimiter == line.npos) return std::nullopt;
    const auto name = Trim(line.substr(0, delimiter));
    if (name.empty()) return std::nullopt;
    return std::pair{name, Trim(line.substr(delimiter + 1))};
}

bool ValidName(std::wstring_view name, std::wstring_view forbidden)
{
    return !name.empty() && name == Trim(name) && name.find(L'\0') == name.npos &&
        name.find_first_of(forbidden) == name.npos;
}
}

IniDocument::IniDocument(std::wstring_view text)
{
    bool foundEnding = false;
    while (!text.empty())
    {
        const auto end = text.find_first_of(L"\r\n");
        if (end == text.npos) { lines_.push_back({std::wstring(text), {}}); break; }
        auto length = std::size_t{1};
        if (text[end] == L'\r' && end + 1 < text.size() && text[end + 1] == L'\n') ++length;
        auto ending = std::wstring(text.substr(end, length));
        if (!foundEnding) { defaultEnding_ = ending; foundEnding = true; }
        lines_.push_back({std::wstring(text.substr(0, end)), std::move(ending)});
        text.remove_prefix(end + length);
    }
}

std::optional<std::wstring> IniDocument::Get(std::wstring_view section, std::wstring_view key) const
{
    bool active = false;
    for (const auto& line : lines_)
    {
        if (const auto name = Section(line.text)) { active = Equal(*name, section); continue; }
        if (!active) continue;
        if (const auto entry = Entry(line.text); entry && Equal(entry->first, key))
            return std::wstring(entry->second);
    }
    return std::nullopt;
}

bool IniDocument::Set(std::wstring_view section, std::wstring_view key, std::wstring_view value)
{
    if (!ValidName(section, L"[]\r\n") || !ValidName(key, L"=\r\n") ||
        key.front() == L';' || key.front() == L'#' || value != Trim(value) ||
        value.find(L'\0') != value.npos || value.find_first_of(L"\r\n") != value.npos)
        return false;
    std::size_t insert = lines_.size();
    bool active = false, found = false;
    for (std::size_t i = 0; i < lines_.size(); ++i)
    {
        if (const auto name = Section(lines_[i].text))
        {
            if (active) { insert = i; break; }
            active = Equal(*name, section);
            found |= active;
            continue;
        }
        if (active)
        {
            if (const auto entry = Entry(lines_[i].text); entry && Equal(entry->first, key))
            {
                lines_[i].text = std::wstring(key) + L"=" + std::wstring(value);
                return true;
            }
        }
    }
    if (insert == lines_.size() && !lines_.empty() && lines_.back().ending.empty())
        lines_.back().ending = defaultEnding_;
    if (!found)
    {
        lines_.push_back({L"[" + std::wstring(section) + L"]", defaultEnding_});
        insert = lines_.size();
    }
    lines_.insert(lines_.begin() + insert, {std::wstring(key) + L"=" + std::wstring(value), defaultEnding_});
    return true;
}

std::wstring IniDocument::Serialize() const
{
    std::wstring result;
    for (const auto& line : lines_) { result += line.text; result += line.ending; }
    return result;
}
