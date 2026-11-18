/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewRoms.h"
#include "Snapshot.h"
#include "emubase/Emubase.h"
#include "util/WavPcmFile.h"
#include <SDL3/SDL_main.h>
#include <filesystem>
#include <string>
#include <string_view>

void DebugLog(const wchar_t*) {}

int main(int argc, char** argv)
{
    const bool wav = argc == 3 && std::string_view(argv[2]) == "--wav";
    if (argc != 2 && !wav) return 2;
    const std::u8string path(reinterpret_cast<const char8_t*>(argv[1]));
    if (wav)
    {
        auto file = WavPcmFile::Create(std::filesystem::path(path), 44100);
        return file && file->WriteOne(0) && file->WriteOne(0xff000000) && file->Close() ? 0 : 1;
    }
    CProcessor::Init();
    bool saved = false;
    {
        CMotherboard board;
        preview_roms::Roms roms(board);
        board.Reset();
        saved = snapshot::Save(std::filesystem::path(path), board, 123);
    }
    CProcessor::Done();
    return saved ? 0 : 1;
}
