#include "acer_wmi.hpp"

#include "com_util.hpp"
#include "text.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <utility>

namespace AcerFan
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr std::uint64_t STATUS_MASK = 0xFF;

        // SetGamingFanBehavior and GetGamingFanBehavior: fan select bits (CPU = bit 0, GPU = bit 3), set-mode fields in
        // bits 17:16 (CPU) and 23:22 (GPU), read-back modes in bits 9:8 (CPU) and 15:14 (GPU).
        constexpr std::uint32_t BOTH_FANS = 0x9;
        constexpr int CPU_MODE_SET_SHIFT = 16;
        constexpr int GPU_MODE_SET_SHIFT = 22;
        constexpr int CPU_MODE_GET_SHIFT = 8;
        constexpr int GPU_MODE_GET_SHIFT = 14;
        constexpr std::uint64_t MODE_FIELD_MASK = 0x3;
        constexpr std::uint32_t MODE_AUTO = 1;
        constexpr std::uint32_t MODE_CUSTOM = 3;

        // SetGamingFanSpeed: the fan id in bits 7:0, the percent in bits 15:8.
        constexpr std::uint32_t CPU_FAN_ID = 0x1;
        constexpr std::uint32_t GPU_FAN_ID = 0x4;
        constexpr std::array<std::uint32_t, 2> FAN_IDS{
            CPU_FAN_ID,
            GPU_FAN_ID,
        };
        constexpr int PERCENT_SHIFT = 8;
        constexpr int MAX_PERCENT = 100;

        // GetGamingSysInfo: the reading in bits 23:8.
        constexpr int READING_SHIFT = 8;
        constexpr std::uint64_t READING_MASK = 0xFFFF;

        std::uint32_t mode_value(FanMode mode) noexcept
        {
            return mode == FanMode::Custom ? MODE_CUSTOM : MODE_AUTO;
        }

        Result<std::uint64_t> to_u64(const VARIANT &value)
        {
            switch (value.vt)
            {
            case VT_BSTR:
            {
                // WMI carries uint64 as a decimal string.
                const std::optional<std::uint64_t> parsed = parse_u64(to_utf8(value.bstrVal));
                if (!parsed)
                {
                    return fail("gmOutput is not a decimal number", WBEM_E_TYPE_MISMATCH);
                }
                return *parsed;
            }
            case VT_UI8:
                return value.ullVal;
            case VT_I8:
                return static_cast<std::uint64_t>(value.llVal);
            case VT_I4:
            case VT_UI4:
                return static_cast<std::uint32_t>(value.lVal);
            default:
                return fail(std::format("gmOutput has the unexpected VARIANT type {}", value.vt), WBEM_E_TYPE_MISMATCH);
            }
        }
    } // namespace

    Result<AcerWmi> AcerWmi::connect()
    {
        ComPtr<IWbemLocator> locator;
        ACERFAN_TRY_VOID(check_hr(
            CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator)),
            "create WbemLocator"
        ));

        AcerWmi wmi;
        const Bstr name_space{L"ROOT\\WMI"};
        ACERFAN_TRY_VOID(check_hr(
            locator->ConnectServer(name_space.get(), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &wmi.m_services),
            "connect to root\\WMI"
        ));
        ACERFAN_TRY_VOID(check_hr(
            CoSetProxyBlanket(
                wmi.m_services.Get(),
                RPC_C_AUTHN_WINNT,
                RPC_C_AUTHZ_NONE,
                nullptr,
                RPC_C_AUTHN_LEVEL_CALL,
                RPC_C_IMP_LEVEL_IMPERSONATE,
                nullptr,
                EOAC_NONE
            ),
            "set the WMI proxy security"
        ));

        const Bstr class_name{L"AcerGamingFunction"};
        ACERFAN_TRY_VOID(check_hr(
            wmi.m_services->GetObject(class_name.get(), 0, nullptr, &wmi.m_class, nullptr),
            "find the AcerGamingFunction class (no Acer laptop with this firmware interface?)"
        ));

        const Bstr language{L"WQL"};
        const Bstr query{L"SELECT * FROM AcerGamingFunction"};
        ComPtr<IEnumWbemClassObject> instances;
        ACERFAN_TRY_VOID(check_hr(
            wmi.m_services->ExecQuery(
                language.get(),
                query.get(),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                nullptr,
                &instances
            ),
            "query AcerGamingFunction"
        ));

        ComPtr<IWbemClassObject> instance;
        ULONG returned = 0;
        ACERFAN_TRY_VOID(
            check_hr(instances->Next(WBEM_INFINITE, 1, &instance, &returned), "read the AcerGamingFunction instance")
        );
        if (returned == 0)
        {
            return fail("no AcerGamingFunction instance (run AcerFan as administrator)", WBEM_E_NOT_FOUND);
        }

        Variant path;
        ACERFAN_TRY_VOID(check_hr(instance->Get(L"__PATH", 0, path.put(), nullptr, nullptr), "read the instance path"));
        if (path.get().vt != VT_BSTR)
        {
            return fail("the instance path is not a string", WBEM_E_TYPE_MISMATCH);
        }
        wmi.m_instance_path = path.get().bstrVal;
        return wmi;
    }

    Result<IWbemClassObject *> AcerWmi::in_signature(std::wstring_view method)
    {
        if (const auto found = m_signatures.find(method); found != m_signatures.end())
        {
            return found->second.Get();
        }
        std::wstring name{method};
        ComPtr<IWbemClassObject> signature;
        ACERFAN_TRY_VOID(
            check_hr(m_class->GetMethod(name.c_str(), 0, &signature, nullptr), "find method " + to_utf8(name))
        );
        return m_signatures.emplace(std::move(name), std::move(signature)).first->second.Get();
    }

    Result<std::uint64_t> AcerWmi::call(std::wstring_view method, std::uint32_t input)
    {
        const std::string label = std::format("{}(0x{:X})", to_utf8(method), input);
        ACERFAN_TRY(signature, in_signature(method));

        ComPtr<IWbemClassObject> params;
        ACERFAN_TRY_VOID(check_hr(signature->SpawnInstance(0, &params), "create the parameters of " + label));

        VARIANT value;
        VariantInit(&value);
        // WMI carries uint32 as VT_I4. A VT_I4 holds no resource, so it needs no VariantClear.
        value.vt = VT_I4;
        value.lVal = static_cast<LONG>(input);
        ACERFAN_TRY_VOID(check_hr(params->Put(L"gmInput", 0, &value, 0), "set gmInput of " + label));

        const Bstr path{m_instance_path};
        const Bstr name{method};
        ComPtr<IWbemClassObject> out;
        ACERFAN_TRY_VOID(
            check_hr(m_services->ExecMethod(path.get(), name.get(), 0, nullptr, params.Get(), &out, nullptr), label)
        );

        Variant result;
        ACERFAN_TRY_VOID(
            check_hr(out->Get(L"gmOutput", 0, result.put(), nullptr, nullptr), "read gmOutput of " + label)
        );
        ACERFAN_TRY(output, to_u64(result.get()));
        if (const std::uint64_t status = output & STATUS_MASK; status != 0)
        {
            return fail(std::format("{} returned the firmware status {}", label, status), E_FAIL);
        }
        return output;
    }

    Result<int> AcerWmi::read(Sensor sensor)
    {
        ACERFAN_TRY(output, call(L"GetGamingSysInfo", static_cast<std::uint32_t>(sensor)));
        return static_cast<int>((output >> READING_SHIFT) & READING_MASK);
    }

    Result<Temperatures> AcerWmi::read_temperatures()
    {
        ACERFAN_TRY(cpu_c, read(Sensor::CpuTemp));
        ACERFAN_TRY(gpu_c, read(Sensor::GpuTemp));
        return Temperatures{cpu_c, gpu_c};
    }

    Result<void> AcerWmi::set_mode(FanMode mode)
    {
        const std::uint32_t value = mode_value(mode);
        const std::uint32_t input = BOTH_FANS | (value << CPU_MODE_SET_SHIFT) | (value << GPU_MODE_SET_SHIFT);
        ACERFAN_TRY_VOID(call(L"SetGamingFanBehavior", input));
        return {};
    }

    Result<void> AcerWmi::set_percent(int percent)
    {
        const auto clamped = static_cast<std::uint32_t>(std::clamp(percent, 0, MAX_PERCENT));
        for (const std::uint32_t fan : FAN_IDS)
        {
            ACERFAN_TRY_VOID(call(L"SetGamingFanSpeed", fan | (clamped << PERCENT_SHIFT)));
        }
        return {};
    }

    Result<void> AcerWmi::apply(FanCommand command)
    {
        ACERFAN_TRY_VOID(set_mode(command.mode));
        if (command.mode == FanMode::Custom)
        {
            ACERFAN_TRY_VOID(set_percent(command.percent));
        }
        return {};
    }

    Result<bool> AcerWmi::is_mode(FanMode mode)
    {
        ACERFAN_TRY(output, call(L"GetGamingFanBehavior", BOTH_FANS));
        const std::uint64_t wanted = mode_value(mode);
        const bool cpu_in_mode = ((output >> CPU_MODE_GET_SHIFT) & MODE_FIELD_MASK) == wanted;
        const bool gpu_in_mode = ((output >> GPU_MODE_GET_SHIFT) & MODE_FIELD_MASK) == wanted;
        return cpu_in_mode && gpu_in_mode;
    }
} // namespace AcerFan
