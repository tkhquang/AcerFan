#include "acer_wmi.hpp"

#include "com_util.hpp"

#include <algorithm>
#include <cwchar>
#include <format>

using Microsoft::WRL::ComPtr;

namespace AcerFan
{
    namespace
    {
        constexpr std::uint64_t status_mask = 0xFF;

        // SetGamingFanBehavior / GetGamingFanBehavior: fan select bits (CPU = bit 0, GPU = bit 3), set-mode fields
        // in bits 17:16 (CPU) and 23:22 (GPU), read-back modes in bits 9:8 (CPU) and 15:14 (GPU).
        constexpr std::uint32_t both_fans = 0x9;
        constexpr int cpu_mode_set_shift = 16;
        constexpr int gpu_mode_set_shift = 22;
        constexpr int cpu_mode_get_shift = 8;
        constexpr int gpu_mode_get_shift = 14;
        constexpr std::uint64_t mode_field_mask = 0x3;
        constexpr std::uint32_t mode_auto = 1;
        constexpr std::uint32_t mode_custom = 3;

        // SetGamingFanSpeed: fan id in bits 7:0, percent in bits 15:8.
        constexpr std::uint32_t cpu_fan_id = 0x1;
        constexpr std::uint32_t gpu_fan_id = 0x4;
        constexpr int percent_shift = 8;

        // GetGamingSysInfo: reading in bits 23:8.
        constexpr int reading_shift = 8;
        constexpr std::uint64_t reading_mask = 0xFFFF;

        std::uint32_t mode_value(FanMode mode) noexcept
        {
            return mode == FanMode::custom ? mode_custom : mode_auto;
        }

        std::uint64_t to_u64(const VARIANT &value)
        {
            switch (value.vt)
            {
            case VT_BSTR: // WMI carries uint64 as a decimal string.
                return std::wcstoull(value.bstrVal, nullptr, 10);
            case VT_UI8:
                return value.ullVal;
            case VT_I8:
                return static_cast<std::uint64_t>(value.llVal);
            case VT_I4:
            case VT_UI4:
                return static_cast<std::uint32_t>(value.lVal);
            default:
                throw ComError(std::format("gmOutput has unexpected VARIANT type {}", value.vt), WBEM_E_TYPE_MISMATCH);
            }
        }
    } // namespace

    AcerWmi::AcerWmi()
    {
        ComPtr<IWbemLocator> locator;
        check_hr(
            CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator)),
            "create WbemLocator"
        );

        const Bstr name_space{L"ROOT\\WMI"};
        check_hr(
            locator->ConnectServer(name_space.get(), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &m_services),
            "connect to root\\WMI"
        );
        check_hr(
            CoSetProxyBlanket(
                m_services.Get(),
                RPC_C_AUTHN_WINNT,
                RPC_C_AUTHZ_NONE,
                nullptr,
                RPC_C_AUTHN_LEVEL_CALL,
                RPC_C_IMP_LEVEL_IMPERSONATE,
                nullptr,
                EOAC_NONE
            ),
            "set WMI proxy security"
        );

        const Bstr class_name{L"AcerGamingFunction"};
        check_hr(
            m_services->GetObject(class_name.get(), 0, nullptr, &m_class, nullptr),
            "find the AcerGamingFunction class (not an Acer laptop with this firmware interface?)"
        );

        const Bstr language{L"WQL"};
        const Bstr query{L"SELECT * FROM AcerGamingFunction"};
        ComPtr<IEnumWbemClassObject> instances;
        check_hr(
            m_services->ExecQuery(
                language.get(),
                query.get(),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                nullptr,
                &instances
            ),
            "query AcerGamingFunction"
        );

        ComPtr<IWbemClassObject> instance;
        ULONG returned = 0;
        check_hr(instances->Next(WBEM_INFINITE, 1, &instance, &returned), "read the AcerGamingFunction instance");
        if (returned == 0)
        {
            throw ComError("no AcerGamingFunction instance (run AcerFan as administrator)", WBEM_E_NOT_FOUND);
        }

        Variant path;
        check_hr(instance->Get(L"__PATH", 0, path.put(), nullptr, nullptr), "read the instance path");
        if (path.get().vt != VT_BSTR)
        {
            throw ComError("the instance path is not a string", WBEM_E_TYPE_MISMATCH);
        }
        m_instance_path = path.get().bstrVal;
    }

    IWbemClassObject *AcerWmi::in_signature(const std::wstring &method)
    {
        if (const auto found = m_signatures.find(method); found != m_signatures.end())
        {
            return found->second.Get();
        }
        ComPtr<IWbemClassObject> signature;
        check_hr(m_class->GetMethod(method.c_str(), 0, &signature, nullptr), "find method " + to_utf8(method));
        return m_signatures.emplace(method, std::move(signature)).first->second.Get();
    }

    std::uint64_t AcerWmi::call(const std::wstring &method, std::uint32_t input)
    {
        const std::string label = std::format("{}(0x{:X})", to_utf8(method), input);

        ComPtr<IWbemClassObject> params;
        check_hr(in_signature(method)->SpawnInstance(0, &params), "create parameters for " + label);

        VARIANT value;
        VariantInit(&value);
        value.vt = VT_I4; // WMI carries uint32 as VT_I4.
        value.lVal = static_cast<LONG>(input);
        check_hr(params->Put(L"gmInput", 0, &value, 0), "set gmInput for " + label);

        const Bstr path{m_instance_path};
        const Bstr name{method};
        ComPtr<IWbemClassObject> out;
        check_hr(m_services->ExecMethod(path.get(), name.get(), 0, nullptr, params.Get(), &out, nullptr), label);

        Variant result;
        check_hr(out->Get(L"gmOutput", 0, result.put(), nullptr, nullptr), "read gmOutput of " + label);
        const std::uint64_t output = to_u64(result.get());
        if (const std::uint64_t status = output & status_mask; status != 0)
        {
            throw ComError(std::format("{} returned firmware status {}", label, status), E_FAIL);
        }
        return output;
    }

    int AcerWmi::read(Sensor sensor)
    {
        const std::uint64_t output = call(L"GetGamingSysInfo", static_cast<std::uint32_t>(sensor));
        return static_cast<int>((output >> reading_shift) & reading_mask);
    }

    void AcerWmi::set_mode(FanMode mode)
    {
        const std::uint32_t value = mode_value(mode);
        (void)call(
            L"SetGamingFanBehavior",
            both_fans | (value << cpu_mode_set_shift) | (value << gpu_mode_set_shift)
        );
    }

    void AcerWmi::set_percent(int percent)
    {
        const auto clamped = static_cast<std::uint32_t>(std::clamp(percent, 0, 100));
        for (const std::uint32_t fan : {cpu_fan_id, gpu_fan_id})
        {
            (void)call(L"SetGamingFanSpeed", fan | (clamped << percent_shift));
        }
    }

    bool AcerWmi::is_mode(FanMode mode)
    {
        const std::uint64_t output = call(L"GetGamingFanBehavior", both_fans);
        const std::uint64_t want = mode_value(mode);
        return ((output >> cpu_mode_get_shift) & mode_field_mask) == want &&
               ((output >> gpu_mode_get_shift) & mode_field_mask) == want;
    }
} // namespace AcerFan
