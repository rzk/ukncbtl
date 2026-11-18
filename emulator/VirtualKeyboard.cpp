/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "VirtualKeyboard.h"
#include <iterator>
#include <string_view>
#include <utility>

namespace
{
using virtual_keyboard::Key;
constexpr Key keys[]{
    {"K1",0010,1,0},{"K2",0011,1,0},{"K3",0012,1,0},{"K4",0014,1,0},{"K5",0015,1,0},
    {"POM",0172,1.3f,0},{"UST",0152,1.3f,0},{"ISP",0151,1.3f,0},{"RESET",0171,1.5f,0},{"STOP",0004,1.3f,0},
    {"AR2",0006,1.2f,1},{";",0007,1,1},{"1",0030,1,1},{"2",0031,1,1},{"3",0032,1,1},{"4",0013,1,1},
    {"5",0034,1,1},{"6",0035,1,1},{"7",0016,1,1},{"8",0017,1,1},{"9",0177,1,1},{"0",0176,1,1},
    {"-",0175,1,1},{"/",0173,1,1},{"Back",0132,1.6f,1},{"KP+",0131,1.2f,1},{"KP-",0025,1.2f,1},{"KP,",0005,1.2f,1},
    {"Tab",0026,1.5f,2},{"J",0027,1,2},{"C",0050,1,2},{"U",0051,1,2},{"K",0052,1,2},{"E",0033,1,2},
    {"N",0054,1,2},{"G",0055,1,2},{"[",0036,1,2},{"]",0037,1,2},{"Z",0157,1,2},{"H",0156,1,2},
    {"Hard",0155,1.3f,2},{":",0174,1,2},{"Enter",0153,1.8f,2},{"7",0125,1,2},{"8",0145,1,2},{"9",0165,1,2},
    {"Ctrl",0046,1.6f,3,true},{"F",0047,1,3},{"Y",0070,1,3},{"W",0071,1,3},{"A",0072,1,3},{"P",0053,1,3},
    {"R",0074,1,3},{"O",0075,1,3},{"L",0056,1,3},{"D",0057,1,3},{"V",0137,1,3},{"\\",0136,1,3},
    {".",0135,1,3},{"4",0130,1,3},{"5",0150,1,3},{"6",0170,1,3},
    {"ALF",0106,1.3f,4},{"GRAF",0066,1.4f,4},{"Q",0067,1,4},{"^",0110,1,4},{"S",0111,1,4},{"M",0112,1,4},
    {"I",0073,1,4},{"T",0114,1,4},{"X",0115,1,4},{"B",0076,1,4},{"@",0077,1,4},{",",0117,1,4},
    {"Left",0116,1.3f,4},{"Up",0154,1.2f,4},{"Right",0133,1.5f,4},{"1",0127,1,4},{"2",0147,1,4},{"3",0167,1,4},
    {"Shift",0105,1.8f,5,true},{"FIKS",0107,1.4f,5},{"Space",0113,6,5},{"Shift",0105,1.8f,5,true},
    {"Down",0134,1.5f,5},{"0",0126,1,5},{".",0146,1,5},{"KP Enter",0166,2,5}};
}

std::span<const virtual_keyboard::Key> virtual_keyboard::Keys() { return keys; }

std::string virtual_keyboard::Label(const Key& key, Legend legend)
{
    constexpr std::pair<std::uint8_t, std::u8string_view> cyrillic[]{
        {0047, u8"\u0424"}, {0073, u8"\u0418"}, {0111, u8"\u0421"}, {0071, u8"\u0412"},
        {0051, u8"\u0423"}, {0072, u8"\u0410"}, {0053, u8"\u041f"}, {0074, u8"\u0420"},
        {0036, u8"\u0428"}, {0075, u8"\u041e"}, {0056, u8"\u041b"}, {0057, u8"\u0414"},
        {0115, u8"\u042c"}, {0114, u8"\u0422"}, {0037, u8"\u0429"}, {0157, u8"\u0417"},
        {0027, u8"\u0419"}, {0052, u8"\u041a"}, {0070, u8"\u042b"}, {0033, u8"\u0415"},
        {0055, u8"\u0413"}, {0112, u8"\u041c"}, {0050, u8"\u0426"}, {0110, u8"\u0427"},
        {0054, u8"\u041d"}, {0067, u8"\u042f"}, {0156, u8"\u0425"}, {0155, u8"\u042a"},
        {0137, u8"\u0416"}, {0136, u8"\u042d"}, {0076, u8"\u0411"}, {0077, u8"\u042e"}};
    if (legend == Legend::latin) return key.label;
    for (const auto& [scan, text] : cyrillic)
        if (scan == key.scan)
        {
            const std::string translated(reinterpret_cast<const char*>(text.data()), text.size());
            return legend == Legend::both ? std::string(key.label) + "\n" + translated : translated;
        }
    return key.label;
}

std::optional<virtual_keyboard::Event> virtual_keyboard::State::Press(const Key& key)
{
    if (!key.scan || key.scan >= sticky_.size()) return std::nullopt;
    if (key.sticky)
    {
        sticky_[key.scan] = !sticky_[key.scan];
        return Event{key.scan, sticky_[key.scan]};
    }
    if (pointer_) return std::nullopt;
    pointer_ = key.scan;
    return Event{key.scan, true};
}

std::optional<virtual_keyboard::Event> virtual_keyboard::State::ReleasePointer()
{
    if (!pointer_) return std::nullopt;
    const auto scan = pointer_;
    pointer_ = 0;
    return Event{scan, false};
}

void virtual_keyboard::State::Reset() { pointer_ = 0; sticky_.fill(false); }
bool virtual_keyboard::State::Down(std::uint8_t scan) const
{ return scan && scan < sticky_.size() && (sticky_[scan] || pointer_ == scan); }
