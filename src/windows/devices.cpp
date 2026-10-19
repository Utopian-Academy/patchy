#include "common.hpp"
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <endpointvolume.h>
#include <array>
#include <cstring>

namespace patchy::win {
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (!n) return "<text conversion failed>";
    std::string result(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), n, nullptr, nullptr);
    return result;
}
std::string describe(const Format& f) {
    std::ostringstream out;
    out << f.rate << " Hz, " << f.channels << " channels, ";
    out << (f.encoding == Encoding::float32 ? "float" : f.encoding == Encoding::pcm ? "PCM" : "unknown");
    out << f.bits << ", valid bits " << f.valid_bits << ", block " << f.block_align
        << ", speaker mask 0x" << std::hex << f.channel_mask;
    return out.str();
}
std::string state_name(DWORD state) {
    std::string s;
    if (state & DEVICE_STATE_ACTIVE) s += "active ";
    if (state & DEVICE_STATE_DISABLED) s += "disabled ";
    if (state & DEVICE_STATE_NOTPRESENT) s += "not-present ";
    if (state & DEVICE_STATE_UNPLUGGED) s += "unplugged ";
    return s.empty() ? "unknown" : s;
}
std::vector<Device> enumerate_devices() {
    auto e = enumerator();
    ComPtr<IMMDeviceCollection> collection;
    check(e->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, &collection), "EnumAudioEndpoints");
    UINT count{};
    check(collection->GetCount(&count), "GetCount");
    std::array<std::array<std::wstring, 3>, 2> defaults;
    for (int flow = 0; flow < 2; ++flow) {
        for (int role = 0; role < 3; ++role) {
            ComPtr<IMMDevice> d;
            if (SUCCEEDED(e->GetDefaultAudioEndpoint(static_cast<EDataFlow>(flow),
                                                     static_cast<ERole>(role), &d)))
                defaults[flow][role] = device_id(d.Get());
        }
    }
    std::vector<Device> devices;
    devices.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        Device info;
        try {
            ComPtr<IMMDevice> d;
            check(collection->Item(i, &d), "endpoint collection Item");
            info.id = device_id(d.Get());
            check(d->GetState(&info.state), "GetState");
            ComPtr<IMMEndpoint> endpoint;
            check(d.As(&endpoint), "IMMEndpoint query");
            EDataFlow flow{};
            check(endpoint->GetDataFlow(&flow), "GetDataFlow");
            info.render = flow == eRender;
            if (flow == eRender || flow == eCapture)
                for (unsigned r = 0; r < 3; ++r)
                    if (info.id == defaults[flow][r]) info.default_roles |= 1u << r;
            ComPtr<IPropertyStore> properties;
            const auto property_hr = d->OpenPropertyStore(STGM_READ, &properties);
            if (FAILED(property_hr)) info.error = "property store: " + error_text(property_hr);
            if (SUCCEEDED(property_hr)) {
                PROPVARIANT name;
                PropVariantInit(&name);
                const auto hr = properties->GetValue(PKEY_Device_FriendlyName, &name);
                if (SUCCEEDED(hr) && name.vt == VT_LPWSTR && name.pwszVal) info.name = name.pwszVal;
                PropVariantClear(&name);
                if (FAILED(hr)) info.error = "friendly name: " + error_text(hr);
            }
            if (info.state & DEVICE_STATE_ACTIVE) {
                ComPtr<IAudioEndpointVolume> volume;
                if (SUCCEEDED(d->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                         reinterpret_cast<void**>(volume.GetAddressOf())))) {
                    float level{}; BOOL muted{};
                    if (SUCCEEDED(volume->GetMasterVolumeLevelScalar(&level))) info.volume = level;
                    if (SUCCEEDED(volume->GetMute(&muted))) info.muted = muted != FALSE;
                }
                auto client = activate(d.Get());
                MixFormat mix(client.Get());
                info.format = parse_format(mix.value);
                REFERENCE_TIME normal{}, minimum{};
                check(client->GetDevicePeriod(&normal, &minimum), "GetDevicePeriod");
                info.default_period = normal;
                info.minimum_period = minimum;
            } else {
                info.error += " inactive endpoint: live format/period not queried";
            }
        } catch (const AudioError& error) {
            info.error += std::string(error.what()) + ": " + error_text(error.code);
        }
        devices.push_back(std::move(info));
    }
    return devices;
}
namespace {
std::wstring resolve_id(const std::wstring& selector, EDataFlow expected) {
    auto e = enumerator();
    ComPtr<IMMDevice> d;
    if (selector == L"default")
        check(e->GetDefaultAudioEndpoint(expected, eConsole, &d), "resolve default endpoint");
    else
        check(e->GetDevice(selector.c_str(), &d), "resolve endpoint ID");
    ComPtr<IMMEndpoint> endpoint;
    check(d.As(&endpoint), "IMMEndpoint query");
    EDataFlow flow{};
    check(endpoint->GetDataFlow(&flow), "GetDataFlow");
    if (flow != expected) throw std::invalid_argument(expected == eCapture
        ? "monitor source must be an input endpoint" : "selected endpoint must be an output endpoint");
    return device_id(d.Get());
}
}
std::wstring resolve_render_id(const std::wstring& selector) { return resolve_id(selector, eRender); }
std::wstring resolve_capture_id(const std::wstring& selector) { return resolve_id(selector, eCapture); }
class Notification final : public IMMNotificationClient {
public:
    std::atomic<std::uint64_t> generation{0};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto result = --references_;
        if (!result) delete this;
        return result;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return changed(); }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return changed(); }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return changed(); }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return changed(); }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return changed(); }
private:
    std::atomic<ULONG> references_{1};
    HRESULT changed() noexcept { generation.fetch_add(1, std::memory_order_relaxed); return S_OK; }
};
struct DeviceWatcher::Impl {
    ComPtr<IMMDeviceEnumerator> e;
    ComPtr<Notification> callback;
    Impl() : e(enumerator()) {
        callback.Attach(new Notification);
        check(e->RegisterEndpointNotificationCallback(callback.Get()), "register endpoint notifications");
    }
    ~Impl() { e->UnregisterEndpointNotificationCallback(callback.Get()); }
};
DeviceWatcher::DeviceWatcher() : impl_(std::make_unique<Impl>()) {}
DeviceWatcher::~DeviceWatcher() = default;
std::uint64_t DeviceWatcher::generation() const noexcept {
    return impl_->callback->generation.load(std::memory_order_relaxed);
}
}
