#include "autostart.hpp"

#include "com_util.hpp"

#include <windows.h>
#include <taskschd.h>
#define SECURITY_WIN32
#include <security.h>
#include <wrl/client.h>

#include <array>
#include <cwctype>
#include <format>
#include <string>

using Microsoft::WRL::ComPtr;

namespace AcerFan::autostart
{
    namespace
    {
        constexpr std::wstring_view task_name = L"AcerFan";

        ComPtr<ITaskFolder> root_folder()
        {
            ComPtr<ITaskService> service;
            check_hr(
                CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service)),
                "create the Task Scheduler service"
            );
            VARIANT empty;
            VariantInit(&empty);
            check_hr(service->Connect(empty, empty, empty, empty), "connect to Task Scheduler");
            ComPtr<ITaskFolder> folder;
            const Bstr root{L"\\"};
            check_hr(service->GetFolder(root.get(), &folder), "open the root task folder");
            return folder;
        }

        std::wstring current_user()
        {
            std::array<wchar_t, 512> name{};
            ULONG size = static_cast<ULONG>(name.size());
            if (GetUserNameExW(NameSamCompatible, name.data(), &size) == FALSE)
            {
                throw ComError("read the current user name", HRESULT_FROM_WIN32(GetLastError()));
            }
            return name.data();
        }

        std::wstring xml_escape(std::wstring_view text)
        {
            std::wstring out;
            for (const wchar_t c : text)
            {
                switch (c)
                {
                case L'&':
                    out += L"&amp;";
                    break;
                case L'<':
                    out += L"&lt;";
                    break;
                case L'>':
                    out += L"&gt;";
                    break;
                case L'"':
                    out += L"&quot;";
                    break;
                default:
                    out += c;
                }
            }
            return out;
        }

        std::wstring lowercase(std::wstring text)
        {
            for (wchar_t &c : text)
            {
                c = static_cast<wchar_t>(std::towlower(c));
            }
            return text;
        }

        std::wstring task_xml(const std::filesystem::path &exe, const std::wstring &user)
        {
            const std::wstring u = xml_escape(user);
            return std::format(
                LR"(<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Description>AcerFan: temperature-aware fan control. Starts elevated at logon without a UAC prompt.</Description>
  </RegistrationInfo>
  <Triggers>
    <LogonTrigger>
      <Enabled>true</Enabled>
      <UserId>{0}</UserId>
    </LogonTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>{0}</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>HighestAvailable</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <AllowHardTerminate>true</AllowHardTerminate>
    <StartWhenAvailable>true</StartWhenAvailable>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <Priority>4</Priority>
    <RestartOnFailure>
      <Interval>PT1M</Interval>
      <Count>3</Count>
    </RestartOnFailure>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>{1}</Command>
      <WorkingDirectory>{2}</WorkingDirectory>
    </Exec>
  </Actions>
</Task>
)",
                u,
                xml_escape(exe.native()),
                xml_escape(exe.parent_path().native())
            );
        }
    } // namespace

    bool is_enabled(const std::filesystem::path &exe)
    {
        try
        {
            const ComPtr<ITaskFolder> folder = root_folder();
            const Bstr name{task_name};
            ComPtr<IRegisteredTask> task;
            if (FAILED(folder->GetTask(name.get(), &task)))
            {
                return false;
            }
            BSTR xml = nullptr;
            if (FAILED(task->get_Xml(&xml)) || xml == nullptr)
            {
                return false;
            }
            const std::wstring text = lowercase(xml);
            SysFreeString(xml);
            return text.find(lowercase(xml_escape(exe.native()))) != std::wstring::npos;
        }
        catch (const ComError &)
        {
            return false;
        }
    }

    void enable(const std::filesystem::path &exe)
    {
        const ComPtr<ITaskFolder> folder = root_folder();
        const Bstr name{task_name};
        const Bstr xml{task_xml(exe, current_user())};
        VARIANT empty;
        VariantInit(&empty);
        ComPtr<IRegisteredTask> task;
        check_hr(
            folder->RegisterTask(
                name.get(),
                xml.get(),
                TASK_CREATE_OR_UPDATE,
                empty,
                empty,
                TASK_LOGON_INTERACTIVE_TOKEN,
                empty,
                &task
            ),
            "register the AcerFan logon task"
        );
    }

    void disable()
    {
        const ComPtr<ITaskFolder> folder = root_folder();
        const Bstr name{task_name};
        const HRESULT hr = folder->DeleteTask(name.get(), 0);
        if (hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        {
            check_hr(hr, "delete the AcerFan logon task");
        }
    }
} // namespace AcerFan::autostart
