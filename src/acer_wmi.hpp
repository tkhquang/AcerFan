/**
 * @file acer_wmi.hpp
 * @brief Fan and sensor access through the AcerGamingFunction WMI class of the firmware, in root\WMI.
 * @details Acer's PredatorSense, NitroSense and ConceptD Palette use the same interface. Every method takes a 32-bit
 *          `gmInput` and returns a 64-bit `gmOutput` whose low byte is a status, 0 for success. The encodings below
 *          match the ones that the Linux acer-wmi driver documents. A ConceptD 9 (CN917-71) verified them. Only an
 *          elevated process can call the methods.
 */
#ifndef ACERFAN_ACER_WMI_HPP
#define ACERFAN_ACER_WMI_HPP

#include "error.hpp"
#include "fan_controller.hpp"

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace AcerFan
{
    /**
     * @brief The GetGamingSysInfo selectors: command 1 (read a sensor) in bits 7:0 and the sensor id in bits 15:8.
     */
    enum class Sensor : std::uint32_t
    {
        /// CPU temperature in degrees Celsius.
        CpuTemp = 0x0101,
        /// CPU fan speed in rpm.
        CpuFanRpm = 0x0201,
        /// GPU fan speed in rpm.
        GpuFanRpm = 0x0601,
        /// GPU temperature in degrees Celsius.
        GpuTemp = 0x0A01,
    };

    /**
     * @brief A connection to the AcerGamingFunction instance of the firmware.
     * @details Use one connection from one thread. It caches the method signatures.
     */
    class AcerWmi
    {
    public:
        /**
         * @brief Connects to root\WMI and finds the AcerGamingFunction instance.
         * @return The connection, or an Error when the class is missing (no Acer laptop with this firmware interface)
         *         or the process is not elevated.
         */
        [[nodiscard]] static Result<AcerWmi> connect();

        /** @brief Reads a temperature in degrees Celsius or a fan speed in rpm. */
        [[nodiscard]] Result<int> read(Sensor sensor);

        /** @brief Reads the CPU and GPU temperatures. */
        [[nodiscard]] Result<Temperatures> read_temperatures();

        /** @brief Puts both fans into `command.mode`, at `command.percent` for FanMode::Custom. */
        [[nodiscard]] Result<void> apply(FanCommand command);

        /** @brief Sets the custom speed of both fans in percent, clamped to 0..100. Only FanMode::Custom uses it. */
        [[nodiscard]] Result<void> set_percent(int percent);

        /** @brief Returns true when both fans are in `mode`. */
        [[nodiscard]] Result<bool> is_mode(FanMode mode);

    private:
        AcerWmi() = default;

        [[nodiscard]] Result<std::uint64_t> call(std::wstring_view method, std::uint32_t input);
        [[nodiscard]] Result<IWbemClassObject *> in_signature(std::wstring_view method);
        [[nodiscard]] Result<void> set_mode(FanMode mode);

        Microsoft::WRL::ComPtr<IWbemServices> m_services;
        Microsoft::WRL::ComPtr<IWbemClassObject> m_class;
        std::wstring m_instance_path;
        std::map<std::wstring, Microsoft::WRL::ComPtr<IWbemClassObject>, std::less<>> m_signatures;
    };
} // namespace AcerFan

#endif // ACERFAN_ACER_WMI_HPP
