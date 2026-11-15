#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if !defined(_M_X64) && !defined(__x86_64__)
#error SoftVoice requires an x64 Windows compiler.
#endif

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace lmms {
namespace {
constexpr unsigned int LANGUAGE_ENGLISH = 1;
constexpr unsigned int FORMAT_22KHZ = 0x08000000;
constexpr unsigned int FORMAT_16BIT = 0x00800000;
constexpr unsigned int TRANSLATE_NATURAL = 1;
constexpr unsigned int WRITE_WAVFILE = 0x100;
constexpr unsigned int WAV_CREATE = 0;

using Session = void*;
using OpenSpeech = int (WINAPI*)(Session*, HWND, int, unsigned int);
using CloseSpeech = int (WINAPI*)(Session);
using Register = int (WINAPI*)(Session, const char*, const char*, unsigned int);
using SetParameter = int (WINAPI*)(Session, std::uint16_t);
using SetWavFile = int (WINAPI*)(Session, const char*, unsigned int);
using Tts = int (WINAPI*)(Session, const char*, char**, unsigned int*, HWND,
                          unsigned int, unsigned int);

// SoftVoice is a legacy ANSI API, so convert rather than handing it UTF-8 bytes.
std::string ansi(const std::wstring& s)
{
    if (s.empty()) { return {}; }
    const int length = WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, out.data(), length, nullptr, nullptr);
    out.pop_back();
    return out;
}

std::wstring environment(const wchar_t* name)
{
    const DWORD count = GetEnvironmentVariableW(name, nullptr, 0);
    if (!count) { return {}; }
    std::wstring value(count, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), count);
    value.resize(written);
    return value;
}

template<typename T> T entry(HMODULE library, const char* exportName)
{
    return reinterpret_cast<T>(GetProcAddress(library, exportName));
}

bool check(const char* operation, int result)
{
    if (result == 0) { return true; }
    std::cerr << operation << " failed with SoftVoice error " << result << '\n';
    return false;
}
} // namespace

int runHost(int argc, wchar_t** argv)
{
    // Arguments: <dll-directory> <output.wav> <personality> <style> <mode> <rate-wpm> <pitch-hz> <text>
    if (argc != 9)
    {
        std::cerr << "Usage: softvoice-host.exe <engine-dir> <output.wav> <personality> <style> <mode> <rate> <pitch> <text>\n";
        return 2;
    }
    const auto engineDir = std::filesystem::absolute(argv[1]);
    const auto dllPath = engineDir / L"svctl64.dll";
    if (!std::filesystem::is_regular_file(dllPath) ||
        !std::filesystem::is_regular_file(engineDir / L"sveng64.dll"))
    {
        std::cerr << "The engine directory must contain svctl64.dll and sveng64.dll\n";
        return 3;
    }

    const int personality = _wtoi(argv[3]);
    const int style = _wtoi(argv[4]);
    const int mode = _wtoi(argv[5]);
    const int rate = _wtoi(argv[6]);
    const int pitch = _wtoi(argv[7]);
    if (personality < 0 || personality > 19 || (style != 0 && style != 2 && style != 3 && style != 4)
        || mode < 0 || mode > 2 || rate < 20 || rate > 500 || pitch < 10 || pitch > 2000)
    {
        std::cerr << "Speech parameters outside the supported SoftVoice ranges\n";
        return 4;
    }

    // Keep the synthesis DLL in the helper process.
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
    {
        std::cerr << "Cannot configure DLL search paths (Win32 error " << GetLastError() << ")\n";
        return 5;
    }
    const auto cookie = AddDllDirectory(engineDir.c_str());
    if (!cookie)
    {
        std::cerr << "Cannot add the SoftVoice engine directory (Win32 error " << GetLastError() << ")\n";
        return 5;
    }
    HMODULE library = LoadLibraryExW(dllPath.c_str(), nullptr,
                                     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library)
    {
        std::cerr << "Cannot load the 64-bit svctl64.dll (Win32 error " << GetLastError() << ")\n";
        RemoveDllDirectory(cookie);
        return 5;
    }

    auto open = entry<OpenSpeech>(library, "SVOpenSpeech");
    auto close = entry<CloseSpeech>(library, "SVCloseSpeech");
    auto registerEngine = entry<Register>(library, "SVRegister");
    auto personalityFn = entry<SetParameter>(library, "SVSetPersonality");
    auto styleFn = entry<SetParameter>(library, "SVSetF0Style");
    auto modeFn = entry<SetParameter>(library, "SVSetVoicingMode");
    auto rateFn = entry<SetParameter>(library, "SVSetRate");
    auto pitchFn = entry<SetParameter>(library, "SVSetPitch");
    auto outputFn = entry<SetWavFile>(library, "SVSetWAVFile");
    auto synth = entry<Tts>(library, "SVTTS");
    const bool complete = open && close && registerEngine && personalityFn && styleFn && modeFn
        && rateFn && pitchFn && outputFn && synth;
    if (!complete)
    {
        std::cerr << "svctl64.dll is missing one or more expected engine exports\n";
        FreeLibrary(library);
        RemoveDllDirectory(cookie);
        return 6;
    }

    // A top-level window and a message pump are necessary for some historical engine versions.
    HWND window = CreateWindowExW(0, L"STATIC", L"SoftVoice LMMS host", WS_OVERLAPPED,
                                  0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window)
    {
        std::cerr << "Cannot create the SoftVoice message window (Win32 error " << GetLastError() << ")\n";
        FreeLibrary(library);
        RemoveDllDirectory(cookie);
        return 7;
    }
    Session session = nullptr;
    const int result = open(&session, window, -1, LANGUAGE_ENGLISH | FORMAT_22KHZ | FORMAT_16BIT);
    bool ok = check("SVOpenSpeech", result);

    if (ok)
    {
        const auto company = environment(L"SOFTVOICE_REG_COMPANY");
        const auto product = environment(L"SOFTVOICE_REG_PRODUCT");
        const auto idString = environment(L"SOFTVOICE_REG_ID");
        // Without explicit registration details, use the engine's default mode.
        if (!company.empty() || !product.empty() || !idString.empty())
        {
            if (company.empty() || product.empty() || idString.empty())
            {
                std::cerr << "Set all three SoftVoice registration variables or clear all three.\n";
                ok = false;
            }
            else
            {
                wchar_t* end = nullptr;
                errno = 0;
                const auto id = std::wcstoul(idString.c_str(), &end, 0);
                if (end == idString.c_str() || *end != L'\0' || errno == ERANGE)
                {
                    std::cerr << "SOFTVOICE_REG_ID must be a valid numeric registration ID.\n";
                    ok = false;
                }
                else
                {
                    const auto companyAnsi = ansi(company);
                    const auto productAnsi = ansi(product);
                    ok = check("SVRegister", registerEngine(session, companyAnsi.c_str(), productAnsi.c_str(), id));
                }
            }
        }
    }

    const std::string wavPath = ansi(argv[2]);
    const std::string utterance = ansi(argv[8]);
    if (ok) { ok = check("SVSetPersonality", personalityFn(session, static_cast<std::uint16_t>(personality))); }
    if (ok) { ok = check("SVSetF0Style", styleFn(session, static_cast<std::uint16_t>(style))); }
    if (ok) { ok = check("SVSetVoicingMode", modeFn(session, static_cast<std::uint16_t>(mode))); }
    if (ok) { ok = check("SVSetRate", rateFn(session, static_cast<std::uint16_t>(rate))); }
    if (ok) { ok = check("SVSetPitch", pitchFn(session, static_cast<std::uint16_t>(pitch))); }
    if (ok) { ok = check("SVSetWAVFile", outputFn(session, wavPath.c_str(), WAV_CREATE)); }
    if (ok)
    {
        ok = check("SVTTS", synth(session, utterance.c_str(), nullptr, nullptr,
                                   window, TRANSLATE_NATURAL, WRITE_WAVFILE));
    }
    if (ok)
    {
        // Let asynchronous file writing and engine window messages settle.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        std::uintmax_t previousSize = 0;
        int stable = 0;
        while (std::chrono::steady_clock::now() < deadline)
        {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            std::error_code ec;
            auto bytes = std::filesystem::file_size(argv[2], ec);
            if (!ec && bytes > 44 && bytes == previousSize) { ++stable; }
            else { stable = 0; }
            if (stable > 15) { break; }
            previousSize = ec ? 0 : bytes;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }
    if (result == 0 && session) { ok = check("SVCloseSpeech", close(session)) && ok; }
    DestroyWindow(window);
    FreeLibrary(library);
    RemoveDllDirectory(cookie);
    if (!ok) { return 7; }

    std::error_code fileError;
    const auto bytes = std::filesystem::file_size(argv[2], fileError);
    if (fileError || bytes <= 44)
    {
        std::cerr << "No valid WAV file was generated\n";
        return 7;
    }
    std::cout << "Generated WAV\n";
    return 0;
}

} // namespace lmms

int wmain(int argc, wchar_t** argv)
{
    return lmms::runHost(argc, argv);
}
