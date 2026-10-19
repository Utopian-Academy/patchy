#pragma once
#include "patchy/windows.hpp"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <ks.h>
#include <ksmedia.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace patchy::win {
using Microsoft::WRL::ComPtr;
class ComApartment {
public:
    ComApartment() {
        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) throw std::runtime_error("CoInitializeEx failed");
    }
    ~ComApartment() { CoUninitialize(); }
    ComApartment(const ComApartment&) = delete;
};
class Handle {
public:
    explicit Handle(bool manual = false) : value_(CreateEventW(nullptr, manual, FALSE, nullptr)) {
        if (!value_) throw std::runtime_error("CreateEvent failed");
    }
    ~Handle() { if (value_) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_;
};
struct AudioError : std::runtime_error {
    HRESULT code;
    AudioError(HRESULT hr, const char* operation)
        : std::runtime_error(operation), code(hr) {}
};
inline void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw AudioError(hr, operation);
}
inline std::string error_text(HRESULT hr) {
    std::ostringstream out;
    out << "HRESULT 0x" << std::hex << std::uppercase << std::setw(8)
        << std::setfill('0') << static_cast<std::uint32_t>(hr);
    switch (hr) {
    case AUDCLNT_E_DEVICE_INVALIDATED: out << ": endpoint removed, disabled or reconfigured"; break;
    case AUDCLNT_E_RESOURCES_INVALIDATED: out << ": stream resources invalidated"; break;
    case AUDCLNT_E_SERVICE_NOT_RUNNING: out << ": Windows Audio service unavailable"; break;
    case AUDCLNT_E_DEVICE_IN_USE: out << ": endpoint is in exclusive use"; break;
    case AUDCLNT_E_UNSUPPORTED_FORMAT: out << ": stream format unsupported"; break;
    case E_ACCESSDENIED: out << ": access denied; inspect device policy and Windows microphone privacy settings for desktop apps"; break;
    case HRESULT_FROM_WIN32(ERROR_NOT_FOUND): out << ": selected endpoint is unavailable"; break;
    case HRESULT_FROM_WIN32(ERROR_TIMEOUT): out << ": audio stream watchdog expired"; break;
    default: break;
    }
    return out.str();
}
inline ComPtr<IMMDeviceEnumerator> enumerator() {
    ComPtr<IMMDeviceEnumerator> result;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           IID_PPV_ARGS(result.GetAddressOf())), "create MMDevice enumerator");
    return result;
}
inline ComPtr<IAudioClient> activate(IMMDevice* device) {
    ComPtr<IAudioClient> client;
    check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(client.GetAddressOf())), "activate endpoint");
    return client;
}
struct MixFormat {
    WAVEFORMATEX* value{};
    explicit MixFormat(IAudioClient* client) { check(client->GetMixFormat(&value), "GetMixFormat"); }
    ~MixFormat() { CoTaskMemFree(value); }
    MixFormat(const MixFormat&) = delete;
};
inline Format parse_format(const WAVEFORMATEX* w) {
    Format f{w->nSamplesPerSec, w->nChannels, w->wBitsPerSample, w->wBitsPerSample,
             w->nBlockAlign, 0, Encoding::unsupported};
    if (w->wFormatTag == WAVE_FORMAT_PCM) f.encoding = Encoding::pcm;
    else if (w->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) f.encoding = Encoding::float32;
    else if (w->wFormatTag == WAVE_FORMAT_EXTENSIBLE && w->cbSize >= 22) {
        const auto* e = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(w);
        f.channel_mask = e->dwChannelMask;
        f.valid_bits = e->Samples.wValidBitsPerSample;
        if (IsEqualGUID(e->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) f.encoding = Encoding::pcm;
        if (IsEqualGUID(e->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) f.encoding = Encoding::float32;
    }
    return f;
}
inline std::wstring device_id(IMMDevice* device) {
    LPWSTR raw{};
    check(device->GetId(&raw), "GetId");
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(raw, CoTaskMemFree);
    return raw;
}
}
