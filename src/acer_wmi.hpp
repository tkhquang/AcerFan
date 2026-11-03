/**
 * @file acer_wmi.hpp
 * @brief Fan and sensor access through the firmware's AcerGamingFunction WMI class (root\WMI).
 * @details The same interface Acer's PredatorSense, NitroSense and ConceptD Palette use. Every method takes a 32-bit
 *          `gmInput` and returns a 64-bit `gmOutput` whose low byte is a status (0 = success). The encodings below
 *          match the ones documented by the Linux acer-wmi driver and were verified on a ConceptD 9 (CN917-71).
 *          The methods are only callable from an elevated process.
 */
#ifndef ACERFAN_ACER_WMI_HPP
#define ACERFAN_ACER_WMI_HPP

#include "fan_controller.hpp"

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace AcerFan
{
    /// GetGamingSysInfo selectors: command 1 (read sensor) in bits 7:0, sensor id in bits 15:8.
    enum class Sensor : std::uint32_t
    {
        cpu_temp = 0x0101,
        cpu_fan_rpm = 0x0201,
        gpu_fan_rpm = 0x0601,
        gpu_temp = 0x0A01,
    };

    class AcerWmi
    {
    public:
        /**
         * @brief Connects to root\WMI and finds the AcerGamingFunction instance.
         * @throws ComError when the class is missing (not an Acer laptop with this firmware interface) or access is
         *         denied (process not elevated).
         */
        AcerWmi();

        /// Reads a temperature (degrees Celsius) or fan speed (rpm).
        [[nodiscard]] int read(Sensor sensor);

        /// Puts both fans into `mode` (firmware Auto or custom speed).
        void set_mode(FanMode mode);

        /// Sets the custom speed of both fans, 0..100. Takes effect in FanMode::custom.
        void set_percent(int percent);

        /// True when both fans are currently in `mode`.
        [[nodiscard]] bool is_mode(FanMode mode);

    private:
        [[nodiscard]] std::uint64_t call(const std::wstring &method, std::uint32_t input);
        [[nodiscard]] IWbemClassObject *in_signature(const std::wstring &method);

        Microsoft::WRL::ComPtr<IWbemServices> m_services;
        Microsoft::WRL::ComPtr<IWbemClassObject> m_class;
        std::wstring m_instance_path;
        std::map<std::wstring, Microsoft::WRL::ComPtr<IWbemClassObject>, std::less<>> m_signatures;
    };
} // namespace AcerFan

#endif // ACERFAN_ACER_WMI_HPP
