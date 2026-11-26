/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewSprite.h"
#include "FrameWorker.h"
#include "SpriteDecoder.h"
#include "emubase/Board.h"
#include "emubase/Processor.h"
#include <algorithm>
#include <system_error>

namespace
{
bool CaptureWords(CMotherboard& board, preview_sprite::Selection selection, std::vector<std::uint16_t>& words)
{
    const int bytes = sprite::PageBytes(selection.width, selection.mode);
    if (!bytes) return false;
    const auto* cpu = board.GetCPU();
    const auto* memory = cpu->GetMemoryControllerConst();
    const auto start = static_cast<std::uint16_t>(selection.address & 0xfffe);
    const bool haltMode = cpu->IsHaltMode();
    const int firstByte = selection.address & 1;
    const int endByte = firstByte + bytes;
    words.resize((endByte + 1) / 2);
    for (std::size_t word = 0; word < words.size(); ++word)
        words[word] = memory->GetWordView(static_cast<std::uint16_t>(start + 2 * word), haltMode, false).value;
    // Ignore padding bytes from aligned reads when comparing the selected page.
    if (firstByte != 0) words.front() &= 0xff00;
    if (endByte % 2 != 0) words.back() &= 0x00ff;
    return true;
}

bool Decode(preview_sprite::Selection selection, std::span<const std::uint16_t> words,
    std::span<std::uint32_t> pixels)
{
    const auto start = static_cast<std::uint16_t>(selection.address & 0xfffe);
    if (!sprite::Decode(pixels, selection.address, selection.width, selection.mode, [&](std::uint16_t address) {
        return words[static_cast<std::uint16_t>(address - start) / 2];
    })) return false;
    for (int row = 0; row < sprite::Height / 2; ++row)
        std::swap_ranges(pixels.begin() + row * sprite::Width, pixels.begin() + (row + 1) * sprite::Width,
            pixels.begin() + (sprite::Height - 1 - row) * sprite::Width);
    for (auto& pixel : pixels) pixel |= 0xff000000u;
    return true;
}
}

std::optional<std::vector<std::uint32_t>> preview_sprite::Capture(
    CMotherboard& board, std::uint16_t address, int width, int mode)
{
    const Selection selection{address, width, mode};
    std::vector<std::uint16_t> words;
    if (!CaptureWords(board, selection, words)) return std::nullopt;
    std::vector<std::uint32_t> pixels(sprite::Width * sprite::Height);
    if (!Decode(selection, words, pixels)) return std::nullopt;
    return pixels;
}

preview_sprite::Image::Image(bool enableWorker, std::function<void()> notify)
    : enableWorker_(enableWorker), notify_(std::move(notify)) {}
preview_sprite::Image::~Image() = default;

bool preview_sprite::Image::Update(CMotherboard& board, Selection selection)
{
    if (pending_)
    {
        if (!worker_->WaitFor(std::chrono::milliseconds(0))) return true;
        pending_ = false;
        if (worker_->WaitOutcome() != FrameWorker::Outcome::completed)
        {
            captured_.reset();
            return false;
        }
        Publish();
    }
    if (!CaptureWords(board, selection, scratch_)) return false;
    if (captured_ == selection && words_ == scratch_) return true;

    words_.swap(scratch_);
    captured_ = selection;
    decoding_.resize(sprite::Width * sprite::Height);
    if (enableWorker_ && !worker_)
    {
        try { worker_ = std::make_unique<FrameWorker>([this] { return Decode(*captured_, words_, decoding_); }, notify_); }
        catch (const std::system_error&) { enableWorker_ = false; }
    }
    if (worker_ && worker_->Start())
    {
        pending_ = true;
        return true;
    }
    if (!Decode(selection, words_, decoding_))
    {
        captured_.reset();
        return false;
    }
    Publish();
    return true;
}

void preview_sprite::Image::Publish()
{
    pixels_.swap(decoding_);
    published_ = captured_;
    ++revision_;
}

std::optional<preview_sprite::Image::View> preview_sprite::Image::Get(Selection selection) const
{
    if (published_ != selection) return std::nullopt;
    return View{pixels_, revision_};
}
