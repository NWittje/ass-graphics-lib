#include "internal.hpp"
#include <charconv>
#include <fstream>
#include <sstream>
namespace ag
{
std::string utf8(const std::wstring &s)
{
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0, nullptr,
                                nullptr);
    require(n > 0, L"Invalid Unicode");
    std::string o(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), o.data(), n, nullptr,
                        nullptr);
    return o;
}
std::wstring wide(const std::string &s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    require(n > 0, L"Invalid UTF-8 settings");
    std::wstring o(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), o.data(), n);
    return o;
}
std::wstring trim(std::wstring s)
{
    auto a = s.find_first_not_of(L" \t\r");
    if (a == s.npos)
        return {};
    return s.substr(a, s.find_last_not_of(L" \t\r") - a + 1);
}
std::wstring escape(const std::wstring &s)
{
    std::wstring o = L"\"";
    for (auto c : s)
    {
        switch (c)
        {
        case L'\\':
            o += L"\\\\";
            break;
        case L'\"':
            o += L"\\\"";
            break;
        case L'\n':
            o += L"\\n";
            break;
        case L'\r':
            o += L"\\r";
            break;
        case L'\t':
            o += L"\\t";
            break;
        default:
            o += c;
        }
    }
    return o + L"\"";
}
std::wstring unescape(const std::wstring &s)
{
    require(s.size() >= 2 && s.front() == L'\"' && s.back() == L'\"', L"Settings values must be quoted");
    std::wstring o;
    for (size_t i = 1; i + 1 < s.size(); ++i)
    {
        auto c = s[i];
        if (c == L'\\')
        {
            require(++i + 1 < s.size(), L"Incomplete settings escape");
            switch (s[i])
            {
            case L'n':
                o += L'\n';
                break;
            case L'r':
                o += L'\r';
                break;
            case L't':
                o += L'\t';
                break;
            case L'\\':
                o += L'\\';
                break;
            case L'\"':
                o += L'\"';
                break;
            default:
                throw Failure{E_INVALIDARG, L"Unknown settings escape"};
            }
        }
        else
        {
            require(c != L'\"', L"Unexpected quote in settings");
            o += c;
        }
    }
    return o;
}
struct Settings final : Object
{
    std::filesystem::path path;
    std::map<std::wstring, std::map<std::wstring, std::wstring>> values;
    Settings(const wchar_t *p) : Object(Kind::Settings, nullptr)
    {
        if (p && *p)
            path = p;
        else
        {
            std::wstring exe(32768, 0);
            DWORD n = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
            require(n > 0 && n < exe.size(), L"Executable path failed");
            exe.resize(n);
            path = std::filesystem::path(exe).parent_path() / L"settings.cfg";
        }
        path = std::filesystem::absolute(path);
    }
    static void names(const wchar_t *section, const wchar_t *key)
    {
        require(section && key && *key, L"Missing settings section/key");
        require(*key != L'#' && *key != L';', L"Settings key cannot start with a comment marker");
        require(std::wstring(section).find_first_of(L"[]\r\n") == std::wstring::npos &&
                    trim(section) == section &&
                    std::wstring(key).find_first_of(L"=\r\n[]") == std::wstring::npos && trim(key) == key,
                L"Invalid settings section/key");
    }
    const std::wstring *find(const wchar_t *s, const wchar_t *k)
    {
        names(s, k);
        auto a = values.find(s);
        if (a == values.end())
            return nullptr;
        auto b = a->second.find(k);
        return b == a->second.end() ? nullptr : &b->second;
    }
    void load()
    {
        std::error_code ec;
        bool exists = std::filesystem::exists(path, ec);
        require(!ec, L"Cannot inspect settings file", E_FAIL);
        if (!exists)
            return;
        require(std::filesystem::file_size(path) <= 16 * 1024 * 1024, L"Settings exceed 16 MiB");
        std::ifstream f(path, std::ios::binary);
        require(bool(f), L"Cannot open settings", E_ACCESSDENIED);
        std::string bytes((std::istreambuf_iterator<char>(f)), {});
        require(!f.bad(), L"Cannot read settings", E_FAIL);
        if (bytes.starts_with("\xEF\xBB\xBF"))
            bytes.erase(0, 3);
        auto text = wide(bytes);
        require(text.find(L'\0') == text.npos, L"NUL in settings");
        std::wistringstream input(text);
        decltype(values) parsed;
        std::wstring line, section;
        uint32_t number = 0;
        while (std::getline(input, line))
        {
            ++number;
            try
            {
                line = trim(line);
                if (line.empty() || line[0] == L'#' || line[0] == L';')
                    continue;
                if (line[0] == L'[')
                {
                    require(line.back() == L']', L"Unclosed section");
                    section = line.substr(1, line.size() - 2);
                    names(section.c_str(), L"_");
                    continue;
                }
                auto pos = line.find(L'=');
                require(pos != line.npos, L"Expected key=value");
                auto key = trim(line.substr(0, pos));
                names(section.c_str(), key.c_str());
                auto value = unescape(trim(line.substr(pos + 1)));
                require(!parsed[section].contains(key), L"Duplicate key");
                parsed[section][key] = value;
            }
            catch (const Failure &e)
            {
                throw Failure{e.hr, L"Line " + std::to_wstring(number) + L": " + e.message};
            }
        }
        values.swap(parsed);
    }
    void save()
    {
        std::wstring content = L"# ass_graphics settings v1; UTF-8, quoted values\n";
        for (auto &[s, entries] : values)
        {
            if (!s.empty())
                content += L"\n[" + s + L"]\n";
            for (auto &[k, v] : entries)
                content += k + L"=" + escape(v) + L"\n";
        }
        auto bytes = utf8(content);
        require(bytes.size() <= 16 * 1024 * 1024, L"Settings exceed 16 MiB");
        auto temp = path;
        temp += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
        HANDLE f =
            CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        wincheck(f != INVALID_HANDLE_VALUE, L"Cannot create settings temporary file");
        try
        {
            DWORD written{};
            wincheck(WriteFile(f, bytes.data(), DWORD(bytes.size()), &written, nullptr) &&
                         written == bytes.size(),
                     L"Cannot write settings");
            wincheck(FlushFileBuffers(f), L"Cannot flush settings");
            CloseHandle(f);
            f = INVALID_HANDLE_VALUE;
            auto bak = path;
            bak += L".bak";
            BOOL ok = std::filesystem::exists(path)
                          ? ReplaceFileW(path.c_str(), temp.c_str(), bak.c_str(), 0, nullptr, nullptr)
                          : MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH);
            wincheck(ok, L"Cannot replace settings; original preserved");
        }
        catch (...)
        {
            if (f != INVALID_HANDLE_VALUE)
                CloseHandle(f);
            DeleteFileW(temp.c_str());
            throw;
        }
    }
};
} // namespace ag
using namespace ag;
ag_result ag_settings_create(const wchar_t *p, ag_handle *out)
{
    return api(
        [&]
        {
            output(out);
            *out = new Settings(p);
        });
}
ag_result ag_settings_load(ag_handle h)
{
    return api(
        [&]
        {
            auto p = get<Settings>(h, Kind::Settings);
            p->ui();
            p->load();
        });
}
ag_result ag_settings_save(ag_handle h)
{
    return api(
        [&]
        {
            auto p = get<Settings>(h, Kind::Settings);
            p->ui();
            p->save();
        });
}
ag_result ag_settings_set(ag_handle h, const wchar_t *s, const wchar_t *k, const wchar_t *v)
{
    return api(
        [&]
        {
            auto p = get<Settings>(h, Kind::Settings);
            p->ui();
            Settings::names(s, k);
            require(v, L"Null settings value");
            p->values[s][k] = v;
        });
}
ag_result ag_settings_get(ag_handle h, const wchar_t *s, const wchar_t *k, wchar_t *v, uint32_t n,
                          uint32_t *required)
{
    return api(
        [&]
        {
            auto p = get<Settings>(h, Kind::Settings);
            p->ui();
            auto text = p->find(s, k);
            require(text, L"Settings key not found", HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
            copy_text(*text, v, n, required);
        });
}
ag_result ag_settings_set_number(ag_handle h, const wchar_t *s, const wchar_t *k, double v)
{
    return api(
        [&]
        {
            require(std::isfinite(v), L"Nonfinite setting");
            char buf[64];
            auto [end, ec] = std::to_chars(buf, buf + 64, v);
            require(ec == std::errc(), L"Number formatting failed");
            std::wstring text(buf, end);
            check(ag_settings_set(h, s, k, text.c_str()), L"Cannot set number");
        });
}
ag_result ag_settings_get_number(ag_handle h, const wchar_t *s, const wchar_t *k, double fallback, double lo,
                                 double hi, double *v)
{
    return api(
        [&]
        {
            auto p = get<Settings>(h, Kind::Settings);
            p->ui();
            require(v && std::isfinite(lo) && std::isfinite(hi) && lo <= hi && fallback >= lo &&
                        fallback <= hi,
                    L"Invalid numeric default/range");
            auto text = p->find(s, k);
            *v = fallback;
            if (!text)
                return;
            auto bytes = utf8(*text);
            double n{};
            auto [end, ec] = std::from_chars(bytes.data(), bytes.data() + bytes.size(), n);
            require(ec == std::errc() && end == bytes.data() + bytes.size() && std::isfinite(n) && n >= lo &&
                        n <= hi,
                    L"Invalid numeric setting");
            *v = n;
        });
}
ag_result ag_settings_set_bool(ag_handle h, const wchar_t *s, const wchar_t *k, uint32_t v)
{
    return ag_settings_set(h, s, k, v ? L"true" : L"false");
}
ag_result ag_settings_get_bool(ag_handle h, const wchar_t *s, const wchar_t *k, uint32_t fallback,
                               uint32_t *v)
{
    return api(
        [&]
        {
            auto p = get<Settings>(h, Kind::Settings);
            p->ui();
            require(v, L"Null boolean output");
            auto text = p->find(s, k);
            *v = fallback != 0;
            if (!text)
                return;
            require(*text == L"true" || *text == L"false", L"Invalid boolean setting");
            *v = *text == L"true";
        });
}
