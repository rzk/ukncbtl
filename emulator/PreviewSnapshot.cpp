/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewSnapshot.h"
#include "Snapshot.h"
#include "emubase/Board.h"

std::filesystem::path preview_snapshot::WithDefaultExtension(std::filesystem::path path)
{
    const auto name = path.filename().native();
    const auto hiddenUknc = name.size() == 5 && name[0] == '.' &&
        (name[1] == 'u' || name[1] == 'U') && (name[2] == 'k' || name[2] == 'K') &&
        (name[3] == 'n' || name[3] == 'N') && (name[4] == 'c' || name[4] == 'C');
    if (!path.empty() && path.has_filename() && !path.has_extension() &&
        !hiddenUknc)
        path += ".uknc";
    return path;
}

preview_snapshot::Session::Session(CMotherboard& board) : board_(board), timing_(FRAMERATE)
{
    timing_.Start(frame_timing::Clock::now());
}

void preview_snapshot::Session::FrameCompleted()
{
    (void)timing_.OnFrame(frame_timing::Clock::now());
}

void preview_snapshot::Session::ResetTracking()
{
    timing_.ResetUptime();
    timing_.Start(frame_timing::Clock::now());
    history_.Clear();
}

bool preview_snapshot::Session::Save(const std::filesystem::path& path)
{
    return snapshot::Save(path, board_, timing_.Uptime());
}

bool preview_snapshot::Session::Load(const std::filesystem::path& path, const std::function<void()>& stop)
{
    auto uptime = timing_.Uptime();
    if (!snapshot::Load(path, board_, uptime, history_, stop)) return false;
    timing_.ResetUptime();
    timing_.SetUptime(uptime);
    timing_.Start(frame_timing::Clock::now());
    return true;
}
