/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

class CMotherboard;
class FrameWorker;
namespace preview_sprite
{
struct Selection
{
    std::uint16_t address;
    int width;
    int mode;
    bool operator==(const Selection&) const = default;
};

// Returns opaque ARGB pixels in top-down order, read through the CPU debugger view.
[[nodiscard]] std::optional<std::vector<std::uint32_t>> Capture(
    CMotherboard& board, std::uint16_t address, int width, int mode);

// Capture through the board owner and decode on one lazily started worker.
// An outstanding job owns its input and output until the next Update collects it.
class Image
{
public:
    explicit Image(bool enableWorker = true, std::function<void()> notify = {});
    ~Image();
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    [[nodiscard]] bool Update(CMotherboard& board, Selection selection);
    struct View
    {
        std::span<const std::uint32_t> pixels;
        std::uint64_t revision;
    };
    // The owner may read the last published pixels for this selection until
    // the next Update. The worker only writes to its own buffer.
    [[nodiscard]] std::optional<View> Get(Selection selection) const;
private:
    void Publish();
    std::optional<Selection> captured_, published_;
    std::vector<std::uint16_t> scratch_, words_;
    std::vector<std::uint32_t> decoding_, pixels_;
    std::uint64_t revision_ = 0;
    bool enableWorker_, pending_ = false;
    std::function<void()> notify_;
    std::unique_ptr<FrameWorker> worker_;
};
}
