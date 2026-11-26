/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncRoms.h"
#include <utility>

bool preview_roms::AsyncLoad::Start(std::filesystem::path path, int slot)
{
    if (Pending() || path.empty() || slot < -1 || slot > 1) return false;
    try
    {
        path_ = std::move(path);
        result_.Start([path = path_, slot]() -> std::expected<Image, Error> {
            try
            {
                if (slot == -1)
                {
                    auto image = ReadSystem(path);
                    if (!image) return std::unexpected(image.error());
                    return Image(std::in_place_type<SystemImage>, std::move(*image));
                }
                auto image = ReadCartridge(path);
                if (!image) return std::unexpected(image.error());
                return Image(std::in_place_type<CartridgeImage>, std::move(*image));
            }
            catch (...) { return std::unexpected(Error::read_failed); }
        });
        slot_ = slot;
        cancelled_ = false;
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

std::optional<preview_roms::AsyncLoad::Result> preview_roms::AsyncLoad::Poll()
{
    if (!result_.Ready())
        return std::nullopt;
    return Finish();
}

std::optional<preview_roms::AsyncLoad::Result> preview_roms::AsyncLoad::Finish()
{
    if (!Pending()) return std::nullopt;
    auto image = result_.Take();
    auto path = std::move(path_);
    path_.clear();
    const int slot = std::exchange(slot_, -1);
    const bool cancelled = std::exchange(cancelled_, false);
    return Result{std::move(path), slot, cancelled, std::move(image)};
}
