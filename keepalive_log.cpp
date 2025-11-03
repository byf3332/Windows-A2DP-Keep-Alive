// keepalive_log.cpp
#include <io.h>
#include <fcntl.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <mmsystem.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

DEFINE_GUID(CLSID_MMDeviceEnumerator,
0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e);

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "user32.lib")

#include "wav_data_msvc.h"

// ===== 日志模式 =====
enum LogMode {
    LOG_NONE = 0,
    LOG_VERBOSE = 1,
    LOG_CONSOLE = 2,
    LOG_BOTH = 3
};
static LogMode g_mode = LOG_NONE;
static unsigned long g_log_counter = 0;
static std::wstring g_log_filename;

// ===== 时间戳（日志内） =====
std::wstring current_time() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"[%04d/%02d/%02d - %02d:%02d:%02d] ",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ===== 生成启动日志文件名 =====
std::wstring generate_log_filename() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"keepalive_%04d%02d%02d_%02d%02d%02d.txt",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ===== 写日志 =====
void write_log(const std::wstring& msg) {
    if (g_mode == LOG_NONE) return;

    std::wstring line = current_time() + msg;

    // 控制台输出
    if (g_mode & LOG_CONSOLE) {
        int len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string buf(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, &buf[0], len, nullptr, nullptr);
        printf("%s\n", buf.c_str());
        fflush(stdout);
    }

    // 文件写入（写后立即释放，不占内存）
    if ((g_mode & LOG_VERBOSE) && !g_log_filename.empty()) {
        HANDLE hFile = CreateFileW(g_log_filename.c_str(),
                                   FILE_APPEND_DATA, FILE_SHARE_READ,
                                   NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            int len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, NULL, 0, NULL, NULL);
            std::string utf8line(len, 0);
            WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, &utf8line[0], len, NULL, NULL);
            WriteFile(hFile, utf8line.c_str(), (DWORD)utf8line.size() - 1, &written, NULL);
            WriteFile(hFile, "\r\n", 2, &written, NULL);
            CloseHandle(hFile);
        }
    }
}

// ===== UTF-8 → Wide =====
std::wstring utf8_to_wstring(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

// ===== 读取阻止设备列表 =====
std::vector<std::wstring> read_blocked_devices(const char* filename) {
    std::vector<std::wstring> list;
    std::ifstream fin(filename, std::ios::binary);
    if (!fin.is_open()) return list;

    std::ostringstream ss;
    ss << fin.rdbuf();
    std::string all = ss.str();
    fin.close();

    if (all.size() >= 3 && (unsigned char)all[0] == 0xEF &&
        (unsigned char)all[1] == 0xBB && (unsigned char)all[2] == 0xBF)
        all = all.substr(3);

    std::istringstream lines(all);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t s = line.find_first_not_of(" \t");
        size_t e = line.find_last_not_of(" \t");
        if (s != std::string::npos)
            list.push_back(utf8_to_wstring(line.substr(s, e - s + 1)));
    }
    return list;
}

// ===== 判断设备是否被阻止 =====
bool is_blocked_device(const WCHAR* name, const std::vector<std::wstring>& blocked) {
    for (const auto& b : blocked) {
        if (wcsstr(name, b.c_str())) return true;
    }
    return false;
}

// ===== 获取默认播放设备 =====
bool get_default_audio_device_name(WCHAR* name, int max_len) {
    HRESULT hr;
    bool result = false;
    CoInitialize(NULL);

    IMMDeviceEnumerator* pEnum = nullptr;
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          IID_PPV_ARGS(&pEnum));
    if (FAILED(hr)) goto cleanup;

    IMMDevice* pDevice = nullptr;
    hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr)) goto cleanup_enum;

    IPropertyStore* pProps = nullptr;
    hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
    if (FAILED(hr)) goto cleanup_device;

    PROPVARIANT varName;
    PropVariantInit(&varName);
    hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
    if (SUCCEEDED(hr)) {
        wcsncpy_s(name, max_len, varName.pwszVal, _TRUNCATE);
        result = true;
    }
    PropVariantClear(&varName);
    pProps->Release();
cleanup_device:
    pDevice->Release();
cleanup_enum:
    pEnum->Release();
cleanup:
    CoUninitialize();
    return result;
}

// ===== 主入口 =====
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR lpCmdLine, int) {
    std::wstring args = lpCmdLine ? lpCmdLine : L"";
    bool has_console = args.find(L"--console") != std::wstring::npos || args.find(L"-c") != std::wstring::npos;
    bool has_verbose = args.find(L"--verbose") != std::wstring::npos || args.find(L"-v") != std::wstring::npos;

    if (has_console && has_verbose)
        g_mode = LOG_BOTH;
    else if (has_console)
        g_mode = LOG_CONSOLE;
    else if (has_verbose)
        g_mode = LOG_VERBOSE;
    else
        g_mode = LOG_NONE;

    // 文件模式生成日志名
    if (g_mode & LOG_VERBOSE)
        g_log_filename = generate_log_filename();

    // 控制台初始化
    if (g_mode & LOG_CONSOLE) {
        AllocConsole();
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        _setmode(_fileno(stdout), _O_TEXT);
        _setmode(_fileno(stderr), _O_TEXT);
    }

    write_log(L"KeepAlive started.");

    WCHAR last_device[256] = L"";
    WCHAR cur_device[256] = L"";
    bool is_playing = false;

    auto blocked = read_blocked_devices("blocked_devices.txt");
    write_log(L"Loaded blocked device list.");

    if (!PlaySoundA((LPCSTR)wav_data, NULL,
        SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT)) {
        write_log(L"PlaySound failed!");
        return 1;
    }
    is_playing = true;

    while (true) {
        Sleep(500);
        if (get_default_audio_device_name(cur_device, 256)) {
            if (wcscmp(cur_device, last_device) != 0) {
                wcscpy_s(last_device, cur_device);
                write_log(L"Device changed -> " + std::wstring(cur_device));

                if (is_blocked_device(cur_device, blocked)) {
                    write_log(L"Blocked device detected. Stop playback.");
                    PlaySound(NULL, NULL, 0);
                    is_playing = false;
                } else if (!is_playing) {
                    write_log(L"Non-blocked device. Resume playback.");
                    PlaySoundA((LPCSTR)wav_data, NULL,
                        SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
                    is_playing = true;
                }
            }
        }
    }
}
