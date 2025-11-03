// keepalive.cpp
#include <initguid.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <mmsystem.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

DEFINE_GUID(CLSID_MMDeviceEnumerator,
0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e);

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "user32.lib")

#include "wav_data_msvc.h"

namespace fs = std::filesystem;

// ============ 日志系统 ============
static bool g_verbose = false;
static std::wofstream g_log;
static std::wstring g_log_path;

void write_log(const std::wstring& msg) {
    if (!g_verbose || !g_log.is_open()) return;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t ts[64];
    swprintf_s(ts, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    g_log << ts << msg << std::endl;
    g_log.flush();

    // 自动压缩日志（超过1MB）
    try {
        auto size = fs::file_size(g_log_path);
        if (size > 1024 * 1024) {
            g_log.close();
            std::wstring zip = g_log_path + L".zip";
            std::wstring cmd = L"powershell -Command \"Compress-Archive -Force -Path '" +
                g_log_path + L"' -DestinationPath '" + zip + L"'\"";
            _wsystem(cmd.c_str());
            DeleteFileW(g_log_path.c_str());
            g_log.open(g_log_path, std::ios::app);
            write_log(L"[INFO] Log rotated and compressed.");
        }
    } catch (...) {}
}

void init_log() {
    if (!g_verbose) return;
    try {
        fs::create_directory(L"logs");
        g_log_path = L"logs\\keepalive_log.txt";
        g_log.open(g_log_path, std::ios::app);
        if (g_log.is_open()) write_log(L"==== KeepAlive started ====");
    } catch (...) {}
}

// UTF-8 std::string -> std::wstring
static std::wstring utf8_to_wstring(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

// 读取 blocked devices
std::vector<std::wstring> read_blocked_devices(const char* filename) {
    std::vector<std::wstring> list;
    std::ifstream fin(filename, std::ios::binary);
    if (!fin.is_open()) return list;

    std::ostringstream ss; ss << fin.rdbuf();
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

// 判断是否是 blocked device（只匹配设备名称部分）
bool is_blocked_device(const WCHAR* name, const std::vector<std::wstring>& blocked) {
    for (const auto& b : blocked) {
        if (wcsstr(name, b.c_str())) return true;
    }
    return false;
}

// 获取默认播放设备名
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

    PROPVARIANT varName; PropVariantInit(&varName);
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

// 主程序
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR lpCmdLine, int) {
    std::wstring args = lpCmdLine ? lpCmdLine : L"";
    if (args.find(L"--verbose") != std::wstring::npos || args.find(L"-v") != std::wstring::npos)
        g_verbose = true;
    init_log();

    WCHAR last_device[256] = L"";
    WCHAR cur_device[256] = L"";
    bool is_playing = false;

    auto blocked = read_blocked_devices("blocked_devices.txt");
    write_log(L"[INFO] Loaded blocked device list.");

    if (!PlaySoundA((LPCSTR)wav_data, NULL,
        SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT)) {
        MessageBoxA(NULL, "PlaySound failed!", "Error", MB_OK);
        write_log(L"[ERROR] PlaySound failed.");
        return 1;
    }
    is_playing = true;

    while (true) {
        Sleep(500);
        if (get_default_audio_device_name(cur_device, 256)) {
            if (wcscmp(cur_device, last_device) != 0) {
                wcscpy_s(last_device, cur_device);
                std::wstring msg = L"[INFO] Device changed -> " + std::wstring(cur_device);
                write_log(msg);

                if (is_blocked_device(cur_device, blocked)) {
                    write_log(L"[INFO] Blocked device detected. Stop playback.");
                    PlaySound(NULL, NULL, 0);
                    is_playing = false;
                } else if (!is_playing) {
                    write_log(L"[INFO] Switching to non-blocked device. Resume playback.");
                    PlaySoundA((LPCSTR)wav_data, NULL,
                        SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
                    is_playing = true;
                }
            }
        }
    }
}
