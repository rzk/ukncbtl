/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>

// Binary parallel-port output. Opening truncates the destination.
class PrinterOutput
{
public:
    [[nodiscard]] bool Open(const std::filesystem::path& path);
    [[nodiscard]] bool Write(std::uint8_t value);
    [[nodiscard]] bool Flush();
    [[nodiscard]] bool Close();
    [[nodiscard]] bool IsOpen() const { return file_.is_open(); }

private:
    std::ofstream file_;
};
