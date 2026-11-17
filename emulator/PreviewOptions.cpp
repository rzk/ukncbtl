/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewOptions.h"

std::expected<preview_options::Options, std::string> preview_options::Parse(std::span<const std::string_view> args)
{
    Options options;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const auto arg = args[i];
        if (arg == "--help" || arg == "-h") options.help = true;
        else if (arg == "--smoke-test") options.smokeTest = true;
        else if (arg == "--run") options.run = true;
        else if (arg == "--paused") options.run = false;
        else if (arg == "--sound") options.sound = true;
        else if (arg == "--no-sound") options.sound = false;
        else if (arg == "--fullscreen") options.fullscreen = true;
        else if (arg == "--windowed") options.fullscreen = false;
        else if (arg == "--boot")
        {
            if (++i == args.size() || args[i].size() != 1 || args[i][0] < '1' || args[i][0] > '7')
                return std::unexpected("--boot requires a selection from 1 to 7");
            options.boot = args[i][0] - '0';
        }
        else if (arg == "--settings" || arg == "--rom" || arg == "--floppy" || arg == "--cartridge" || arg == "--hard-disk")
        {
            int slot = 0;
            if (arg != "--rom" && arg != "--settings")
            {
                if (++i == args.size() || args[i].size() != 1 || args[i][0] < (arg == "--floppy" ? '0' : '1') ||
                    args[i][0] > (arg == "--floppy" ? '3' : '2'))
                    return std::unexpected(std::string(arg) + " has an invalid slot");
                slot = args[i][0] - (arg == "--floppy" ? '0' : '1');
            }
            if (++i == args.size() || args[i].empty() || args[i].starts_with("--"))
                return std::unexpected(std::string(arg) + " requires a file path");
            const std::u8string utf8(reinterpret_cast<const char8_t*>(args[i].data()), args[i].size());
            auto path = std::filesystem::path(utf8);
            if (arg == "--settings") options.settingsFile = std::move(path);
            else if (arg == "--rom") options.rom = std::move(path);
            else if (arg == "--floppy") options.floppies[slot] = std::move(path);
            else if (arg == "--cartridge") options.cartridges[slot] = std::move(path);
            else options.hardDisks[slot] = std::move(path);
        }
        else return std::unexpected("Unknown option: " + std::string(arg));
    }
    if (options.boot && options.run == false) return std::unexpected("--boot cannot be combined with --paused");
    return options;
}
