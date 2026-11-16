/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "PrinterOutput.h"
#include <filesystem>

class CMotherboard;
namespace preview_printer
{
// The borrowed board must outlive this session. Calls occur on its thread.
class Session
{
public:
    explicit Session(CMotherboard& board) : board_(board) {}
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    [[nodiscard]] bool Start(const std::filesystem::path& path);
    [[nodiscard]] bool Stop();
    [[nodiscard]] bool Flush();
    [[nodiscard]] bool Check();
    // Restore the host device's online bit after a machine reset or state load.
    void Synchronize();
    [[nodiscard]] bool Active() const { return output_.IsOpen(); }
    [[nodiscard]] bool Conflicts(const std::filesystem::path& path) const;
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
private:
    static bool Write(void* context, std::uint8_t value);
    CMotherboard& board_;
    PrinterOutput output_;
    std::filesystem::path path_;
};
}
