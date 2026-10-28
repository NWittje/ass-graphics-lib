#include "internal.hpp"

namespace ag
{
namespace
{
using Path = std::filesystem::path;
constexpr uint32_t maximum_font_bytes = 16 * 1024 * 1024;

[[noreturn]] void font_failure(const Path &path, const wchar_t *reason, HRESULT hr)
{
    throw Failure{hr, L"B612 dependency [" + path.wstring() + L"]: " + reason};
}
void font_check(HRESULT hr, const Path &path, const wchar_t *reason)
{
    if (FAILED(hr))
        font_failure(path, reason, hr);
}
Path application_directory()
{
    // The consuming EXE defines deployment. Neither this DLL's directory nor
    // process CWD/PATH/system font registration participates in resolution.
    std::vector<wchar_t> path(512);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        wincheck(length != 0, L"Cannot locate application for B612 dependency");
        if (length < path.size())
            return Path(std::wstring(path.data(), length)).parent_path();
        require(path.size() < 32768, L"Application path exceeds B612 lookup limit", E_FAIL);
        path.resize(std::min<size_t>(path.size() * 2, 32768));
    }
}
bool read_candidate(const Path &path, std::vector<uint8_t> &bytes)
{
    struct File
    {
        HANDLE handle;
        ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    } file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (file.handle == INVALID_HANDLE_VALUE)
    {
        const DWORD open_error = GetLastError();
        if (open_error == ERROR_FILE_NOT_FOUND || open_error == ERROR_PATH_NOT_FOUND)
        {
            // A dangling explicit reparse point is broken, not an absent override.
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                font_failure(path, L"explicit font path cannot be resolved", DWRITE_E_FILEFORMAT);
            const DWORD attributes_error = GetLastError();
            if (attributes_error != ERROR_FILE_NOT_FOUND && attributes_error != ERROR_PATH_NOT_FOUND)
                font_failure(path, L"cannot inspect explicit font path", HRESULT_FROM_WIN32(attributes_error));
            return false;
        }
        font_failure(path, L"cannot open explicit font file", HRESULT_FROM_WIN32(open_error));
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.handle, &size))
        font_failure(path, L"cannot read font size", HRESULT_FROM_WIN32(GetLastError()));
    if (size.QuadPart < 12 || size.QuadPart > maximum_font_bytes)
        font_failure(path, L"invalid font length (12 bytes to 16 MiB required)", DWRITE_E_FILEFORMAT);
    bytes.resize(size_t(size.QuadPart));
    DWORD read{};
    if (!ReadFile(file.handle, bytes.data(), DWORD(bytes.size()), &read, nullptr))
        font_failure(path, L"cannot read explicit font file", HRESULT_FROM_WIN32(GetLastError()));
    if (read != bytes.size())
        font_failure(path, L"incomplete explicit font file", DWRITE_E_FILEFORMAT);
    return true;
}
void validate_directory(const std::vector<uint8_t> &bytes, const Path &path)
{
    const auto be = [&](size_t at) -> uint32_t {
        return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at+1]) << 16) |
               (uint32_t(bytes[at+2]) << 8) | bytes[at+3];
    };
    const size_t count = (size_t(bytes[4]) << 8) | bytes[5];
    if (be(0) != 0x00010000 || count == 0 || count > (bytes.size()-12)/16)
        font_failure(path, L"invalid TrueType directory", DWRITE_E_FILEFORMAT);
    for (size_t i = 0; i < count; ++i)
    {
        const size_t at = 12+i*16, offset = be(at+8), length = be(at+12);
        if (offset % 4 || offset < 12+count*16 || offset > bytes.size() ||
            length > bytes.size()-offset)
            font_failure(path, L"invalid TrueType table range", DWRITE_E_FILEFORMAT);
    }
    // No file digest, checksum or revision pin: independently updated upstream
    // B612 files remain acceptable when their structure/family/style are valid.
}
void validate_variant(IDWriteFactory5 *write, IDWriteFontFile *file, const Path &path, bool bold)
{
    BOOL supported{};
    DWRITE_FONT_FILE_TYPE file_type{};
    DWRITE_FONT_FACE_TYPE face_type{};
    UINT32 faces{};
    font_check(file->Analyze(&supported, &file_type, &face_type, &faces), path, L"font analysis failed");
    if (!supported || faces != 1 || file_type != DWRITE_FONT_FILE_TYPE_TRUETYPE)
        font_failure(path, L"one TrueType B612 face is required", DWRITE_E_FILEFORMAT);
    ComPtr<IDWriteFontFace> face;
    font_check(write->CreateFontFace(face_type, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &face),
               path, L"font face is invalid");
    ComPtr<IDWriteFontFace3> details;
    font_check(face.As(&details), path, L"font metadata is unavailable");
    ComPtr<IDWriteLocalizedStrings> names;
    font_check(details->GetFamilyNames(&names), path, L"font family is unavailable");
    bool b612 = false;
    for (UINT32 i = 0; i < names->GetCount(); ++i)
    {
        UINT32 length{};
        font_check(names->GetStringLength(i, &length), path, L"font family length is invalid");
        if (length != 4)
            continue;
        wchar_t family[5]{};
        font_check(names->GetString(i, family, 5), path, L"font family name is invalid");
        b612 |= _wcsicmp(family, L"B612") == 0;
    }
    const auto weight = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    if (!b612 || details->GetWeight() != weight || details->GetStyle() != DWRITE_FONT_STYLE_NORMAL ||
        details->GetStretch() != DWRITE_FONT_STRETCH_NORMAL)
        font_failure(path, bold ? L"B612 Bold upright is required" : L"B612 Regular upright is required",
                     DWRITE_E_FILEFORMAT);
}
} // namespace

void Context::load_fonts()
{
    const Path directory = application_directory();
    constexpr const wchar_t *filenames[] = {L"B612-Regular.ttf", L"B612-Bold.ttf"};
    check(write->CreateInMemoryFontFileLoader(&loader), L"Font loader failed");
    check(write->RegisterFontFileLoader(loader.Get()), L"Font loader registration failed");
    ComPtr<IDWriteFontSetBuilder1> builder;
    check(write->CreateFontSetBuilder(&builder), L"Font set builder failed");
    for (size_t i = 0; i < font_storage.size(); ++i)
    {
        Path selected = directory / filenames[i];
        if (!read_candidate(selected, font_storage[i]))
        {
            selected = directory.parent_path() / filenames[i];
            if (!read_candidate(selected, font_storage[i]))
                throw Failure{HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
                              L"Required B612 file missing: " + (directory / filenames[i]).wstring() +
                              L" ; shared parent: " + selected.wstring()};
        }
        validate_directory(font_storage[i], selected);
        // Selection is final before parsing. Invalid/unreadable adjacent files
        // must never be replaced silently by the parent's or an installed font.
        font_bytes[i] = font_storage[i].data();
        font_sizes[i] = uint32_t(font_storage[i].size());
        ComPtr<IDWriteFontFile> file;
        font_check(loader->CreateInMemoryFontFileReference(write.Get(), font_bytes[i], font_sizes[i],
                                                           nullptr, &file),
                   selected, L"font reference failed");
        validate_variant(write.Get(), file.Get(), selected, i == 1);
        font_check(builder->AddFontFile(file.Get()), selected, L"font set rejected the file");
        DWORD count{};
        gdi_fonts[i] = AddFontMemResourceEx(font_storage[i].data(), font_sizes[i], nullptr, &count);
        if (!gdi_fonts[i] || count != 1)
            font_failure(selected, L"private native-control font registration failed", DWRITE_E_FILEFORMAT);
    }
    ComPtr<IDWriteFontSet> set;
    check(builder->CreateFontSet(&set), L"B612 font set failed");
    check(write->CreateFontCollectionFromFontSet(set.Get(), &collection), L"B612 collection failed");
}
} // namespace ag