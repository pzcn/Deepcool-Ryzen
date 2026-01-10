// TrafficMonitor plugin: Ryzen SDK telemetry (temperature, usage, power).
#include <windows.h>

#include <array>
#include <cwchar>
#include <string>

#include "PluginInterface.h"

struct RMMonitorContext;

extern "C" {
void rm_monitor_set_sdk_path(const wchar_t* path);
int rm_monitor_init(RMMonitorContext** out_ctx);
int rm_monitor_read(RMMonitorContext* ctx, double* temperatureC, double* powerW, double* usagePercent);
void rm_monitor_shutdown(RMMonitorContext* ctx);
int rm_ipc_publish(double temperatureC, double powerW, double usagePercent, int status);
int rm_ipc_read(double* temperatureC, double* powerW, double* usagePercent, int* status, unsigned int max_age_ms);
int rm_ipc_is_service_running();
int rm_ipc_owner_try_acquire();
void rm_ipc_owner_release();
}

namespace {

constexpr int kStatusOk = 0;
constexpr int kIpcOk = 0;
constexpr ULONGLONG kInitRetryMs = 10000;
constexpr unsigned int kIpcMaxAgeMs = 4000;
constexpr ULONGLONG kCacheGraceMs = 5000;
constexpr wchar_t kNotAvailableText[] = L"N/A";
constexpr wchar_t kUnavailableTooltip[] = L"Ryzen SDK unavailable";
constexpr wchar_t kWaitingForServiceTooltip[] = L"Waiting for service data";

enum class ItemIndex {
    Temp = 0,
    Usage = 1,
    Power = 2,
    Count = 3,
};

size_t ToIndex(ItemIndex index) {
    return static_cast<size_t>(index);
}

std::wstring JoinPath(const std::wstring& base, const wchar_t* suffix) {
    if (base.empty()) {
        return L"";
    }
    std::wstring path = base;
    if (path.back() != L'\\' && path.back() != L'/') {
        path.push_back(L'\\');
    }
    path.append(suffix);
    return path;
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool HasPlatformDll(const std::wstring& root) {
    return FileExists(JoinPath(root, L"Platform.dll")) ||
           FileExists(JoinPath(root, L"bin\\Platform.dll"));
}

std::wstring GetModuleDirectory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetModuleDirectory),
            &module)) {
        return L"";
    }

    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(module, path, static_cast<DWORD>(_countof(path)));
    if (len == 0 || len >= _countof(path)) {
        return L"";
    }

    std::wstring full(path, len);
    size_t pos = full.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L"";
    }
    return full.substr(0, pos);
}

std::wstring GetProcessDirectory() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(_countof(path)));
    if (len == 0 || len >= _countof(path)) {
        return L"";
    }

    std::wstring full(path, len);
    size_t pos = full.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L"";
    }
    return full.substr(0, pos);
}

std::wstring GetTempSdkRoot() {
    wchar_t temp_path[MAX_PATH] = {};
    DWORD len = GetTempPathW(static_cast<DWORD>(_countof(temp_path)), temp_path);
    if (len == 0 || len >= _countof(temp_path)) {
        return L"";
    }
    std::wstring root(temp_path, len);
    if (!root.empty() && root.back() != L'\\' && root.back() != L'/') {
        root.push_back(L'\\');
    }
    root.append(L"ryzenmaster-monitor");
    if (HasPlatformDll(root)) {
        return root;
    }
    return L"";
}

std::wstring ResolveSdkRoot() {
    const std::wstring module_dir = GetModuleDirectory();
    if (!module_dir.empty()) {
        std::wstring bundled = JoinPath(module_dir, L"RyzenSDK");
        if (HasPlatformDll(bundled)) {
            return bundled;
        }
        if (HasPlatformDll(module_dir)) {
            return module_dir;
        }
    }

    const std::wstring process_dir = GetProcessDirectory();
    if (!process_dir.empty()) {
        std::wstring bundled = JoinPath(process_dir, L"RyzenSDK");
        if (HasPlatformDll(bundled)) {
            return bundled;
        }
        if (HasPlatformDll(process_dir)) {
            return process_dir;
        }
    }

    std::wstring temp_root = GetTempSdkRoot();
    if (!temp_root.empty()) {
        return temp_root;
    }

    return L"";
}

const wchar_t* ItemName(ItemIndex index) {
    switch (index) {
    case ItemIndex::Temp:
        return L"Ryzen Temperature";
    case ItemIndex::Usage:
        return L"Ryzen Usage";
    case ItemIndex::Power:
        return L"Ryzen Power";
    default:
        return L"Ryzen Item";
    }
}

const wchar_t* ItemId(ItemIndex index) {
    switch (index) {
    case ItemIndex::Temp:
        return L"RyzenTemp";
    case ItemIndex::Usage:
        return L"RyzenUsage";
    case ItemIndex::Power:
        return L"RyzenPower";
    default:
        return L"RyzenItem";
    }
}

const wchar_t* ItemLabel(ItemIndex index) {
    switch (index) {
    case ItemIndex::Temp:
        return L"Temp";
    case ItemIndex::Usage:
        return L"Usage";
    case ItemIndex::Power:
        return L"Power";
    default:
        return L"Value";
    }
}

const wchar_t* ItemSample(ItemIndex index) {
    switch (index) {
    case ItemIndex::Temp:
        return L"100 \u2103";
    case ItemIndex::Usage:
        return L"100 %";
    case ItemIndex::Power:
        return L"200 W";
    default:
        return L"0";
    }
}

} // namespace

class RyzenMonitorPlugin;

class RyzenItem final : public IPluginItem {
public:
    RyzenItem(RyzenMonitorPlugin& plugin, ItemIndex index)
        : plugin_(plugin), index_(index) {}

    const wchar_t* GetItemName() const override { return ItemName(index_); }
    const wchar_t* GetItemId() const override { return ItemId(index_); }
    const wchar_t* GetItemLableText() const override { return ItemLabel(index_); }
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override { return ItemSample(index_); }

private:
    RyzenMonitorPlugin& plugin_;
    ItemIndex index_;
};

class RyzenMonitorPlugin final : public ITMPlugin {
public:
    static RyzenMonitorPlugin& Instance() {
        static RyzenMonitorPlugin instance;
        return instance;
    }

    IPluginItem* GetItem(int index) override {
        if (index < 0 || index >= static_cast<int>(ItemIndex::Count)) {
            return nullptr;
        }
        return &items_[static_cast<size_t>(index)];
    }

    void DataRequired() override {
        double temp = 0.0;
        double power = 0.0;
        double usage = 0.0;
        if (rm_ipc_is_service_running() != 0) {
            if (owns_sdk_) {
                ReleaseSdkOwnership();
            }
            if (TryReadIpc(temp, power, usage)) {
                UpdateValues(temp, power, usage);
                tooltip_.clear();
                return;
            }
            if (UseCachedValuesIfFresh(kCacheGraceMs, kWaitingForServiceTooltip)) {
                return;
            }
            SetUnavailable(kUnavailableTooltip);
            return;
        }

        if (!owns_sdk_) {
            if (TryReadIpc(temp, power, usage)) {
                UpdateValues(temp, power, usage);
                tooltip_.clear();
                return;
            }
            if (rm_ipc_owner_try_acquire() == 0) {
                if (UseCachedValuesIfFresh(kCacheGraceMs, kUnavailableTooltip)) {
                    return;
                }
                SetUnavailable(kUnavailableTooltip);
                return;
            }
            owns_sdk_ = true;
        }

        if (!EnsureInitialized()) {
            ReleaseSdkOwnership();
            if (UseCachedValuesIfFresh(kCacheGraceMs, kUnavailableTooltip)) {
                return;
            }
            SetUnavailable(kUnavailableTooltip);
            return;
        }

        int status = rm_monitor_read(ctx_, &temp, &power, &usage);
        if (status != kStatusOk) {
            ReleaseSdkOwnership();
            if (UseCachedValuesIfFresh(kCacheGraceMs, kUnavailableTooltip)) {
                return;
            }
            SetUnavailable(kUnavailableTooltip);
            return;
        }

        rm_ipc_publish(temp, power, usage, status);
        UpdateValues(temp, power, usage);
        tooltip_.clear();
    }

    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
        case TMI_NAME:
            return L"Ryzen SDK Monitor";
        case TMI_DESCRIPTION:
            return L"Reads Ryzen temperature, usage, and power via Ryzen SDK.";
        case TMI_AUTHOR:
            return L"Deepcool";
        case TMI_COPYRIGHT:
            return L"Copyright (c) 2025";
        case TMI_VERSION:
            return L"1.0.0";
        case TMI_URL:
            return L"";
        default:
            return L"";
        }
    }

    const wchar_t* GetTooltipInfo() override { return tooltip_.c_str(); }

    const std::wstring& ValueText(ItemIndex index) const {
        return values_[ToIndex(index)];
    }

private:
    RyzenMonitorPlugin()
        : items_{ {
              RyzenItem(*this, ItemIndex::Temp),
              RyzenItem(*this, ItemIndex::Usage),
              RyzenItem(*this, ItemIndex::Power),
          } } {
        SetUnavailable(L"");
    }

    ~RyzenMonitorPlugin() { ReleaseSdkOwnership(); }

    bool EnsureInitialized() {
        if (ctx_) {
            return true;
        }
        ULONGLONG now = GetTickCount64();
        if (now - last_init_attempt_ < kInitRetryMs) {
            return false;
        }
        last_init_attempt_ = now;

        std::wstring sdk_root = ResolveSdkRoot();
        if (!sdk_root.empty()) {
            rm_monitor_set_sdk_path(sdk_root.c_str());
        }

        RMMonitorContext* ctx = nullptr;
        int status = rm_monitor_init(&ctx);
        last_status_ = status;
        if (status != kStatusOk) {
            if (ctx) {
                rm_monitor_shutdown(ctx);
            }
            return false;
        }

        ctx_ = ctx;
        return true;
    }

    bool TryReadIpc(double& temp, double& power, double& usage) {
        int status = kStatusOk;
        int result = rm_ipc_read(&temp, &power, &usage, &status, kIpcMaxAgeMs);
        if (result != kIpcOk || status != kStatusOk) {
            return false;
        }
        return true;
    }

    bool UseCachedValuesIfFresh(ULONGLONG max_age_ms, const wchar_t* tooltip) {
        if (!has_cache_) {
            return false;
        }
        ULONGLONG now = GetTickCount64();
        if (now < last_update_ms_ || now - last_update_ms_ > max_age_ms) {
            return false;
        }
        if (tooltip) {
            tooltip_.assign(tooltip);
        } else {
            tooltip_.clear();
        }
        return true;
    }

    void ShutdownContext() {
        if (ctx_) {
            rm_monitor_shutdown(ctx_);
            ctx_ = nullptr;
        }
    }

    void ReleaseSdkOwnership() {
        if (owns_sdk_) {
            ShutdownContext();
            rm_ipc_owner_release();
            owns_sdk_ = false;
        }
    }

    void SetUnavailable(const wchar_t* tooltip) {
        for (auto& value : values_) {
            value.assign(kNotAvailableText);
        }
        if (tooltip) {
            tooltip_.assign(tooltip);
        } else {
            tooltip_.clear();
        }
    }

    void UpdateValues(double temp, double power, double usage) {
        std::array<wchar_t, 32> buffer{};
        swprintf_s(buffer.data(), buffer.size(), L"%.0f \u2103", temp);
        values_[ToIndex(ItemIndex::Temp)] = buffer.data();

        swprintf_s(buffer.data(), buffer.size(), L"%.0f %%", usage);
        values_[ToIndex(ItemIndex::Usage)] = buffer.data();

        swprintf_s(buffer.data(), buffer.size(), L"%.0f W", power);
        values_[ToIndex(ItemIndex::Power)] = buffer.data();

        has_cache_ = true;
        last_update_ms_ = GetTickCount64();
    }

    std::array<RyzenItem, static_cast<size_t>(ItemIndex::Count)> items_;
    std::array<std::wstring, static_cast<size_t>(ItemIndex::Count)> values_{};
    std::wstring tooltip_;
    RMMonitorContext* ctx_ = nullptr;
    ULONGLONG last_init_attempt_ = 0;
    int last_status_ = kStatusOk;
    bool owns_sdk_ = false;
    bool has_cache_ = false;
    ULONGLONG last_update_ms_ = 0;
};

const wchar_t* RyzenItem::GetItemValueText() const {
    return plugin_.ValueText(index_).c_str();
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance() {
    return &RyzenMonitorPlugin::Instance();
}
