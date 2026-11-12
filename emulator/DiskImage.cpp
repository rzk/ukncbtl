/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "stdafx.h"
#include "DiskImage.h"
#include <vector>
std::expected<void, disk_image::Error> disk_image::Create(const wchar_t* path, int tracks, const Backend& backend)
{
    if (!path || !*path || !Size(tracks) || !backend.open || !backend.write || !backend.close || !backend.lastError)
        return std::unexpected(Error{ErrorKind::InvalidArgument, ERROR_INVALID_PARAMETER});
    std::vector<uint8_t> image(Size(tracks));
    Build(tracks, image);
    const auto file = backend.open(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::unexpected(Error{ErrorKind::Open, backend.lastError()});
    DWORD written = 0;
    const bool ok = backend.write(file, image.data(), static_cast<DWORD>(image.size()), &written, nullptr) != FALSE;
    const auto writeError = !ok ? backend.lastError() : written != image.size() ? ERROR_WRITE_FAULT : ERROR_SUCCESS;
    const bool closed = backend.close(file) != FALSE;
    if (!ok || written != image.size()) return std::unexpected(Error{ErrorKind::Write, writeError});
    if (!closed) return std::unexpected(Error{ErrorKind::Close, backend.lastError()});
    return {};
}
