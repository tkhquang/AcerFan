#include "autostart.hpp"

#include "com_util.hpp"

#include <windows.h>
#include <taskschd.h>
#define SECURITY_WIN32
#include <security.h>
#include <wrl/client.h>

#include <array>
#include <format>
#include <string>
#include <string_view>

namespace AcerFan::autostart
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr std::wstring_view TASK_NAME = L"AcerFan";
        constexpr std::size_t MAX_USER_NAME_CHARS = 512;

        Result<ComPtr<ITaskFolder>> root_folder()
        {
            ComPtr<ITaskService> service;
            ACERFAN_TRY_VOID(check_hr(
                CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service)),
                "create the Task Scheduler service"
            ));
            const Variant empty;
            ACERFAN_TRY_VOID(check_hr(
                service->Connect(empty.get(), empty.get(), empty.get(), empty.get()),
                "connect to Task Scheduler"
            ));
            const Bstr root{L"\\"};
            ComPtr<ITaskFolder> folder;
            ACERFAN_TRY_VOID(check_hr(service->GetFolder(root.get(), &folder), "open the root task folder"));
            return folder;
        }

        Result<std::wstring> current_user()
        {
            std::array<wchar_t, MAX_USER_NAME_CHARS> name{};
            ULONG size = static_cast<ULONG>(name.size());
            if (GetUserNameExW(NameSamCompatible, name.data(), &size) == FALSE)
            {
                return fail_last_error("read the current user name");
            }
            return std::wstring{name.data()};
        }

        std::wstring xml_escape(std::wstring_view text)
        {
            std::wstring out;
            for (const wchar_t character : text)
            {
                switch (character)
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
                    out += character;
                }
            }
            return out;
        }

        std::wstring task_xml(const std::filesystem::path &exe, std::wstring_view user)
        {
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
                xml_escape(user),
                xml_escape(exe.native()),
                xml_escape(exe.parent_path().native())
            );
        }

        // True when the action at the 1-based `index` runs `exe`. Windows paths compare without case.
        bool action_runs(IActionCollection &actions, LONG index, const std::filesystem::path &exe)
        {
            ComPtr<IAction> action;
            ComPtr<IExecAction> exec;
            Bstr path;
            if (FAILED(actions.get_Item(index, &action)) || FAILED(action.As(&exec)) ||
                FAILED(exec->get_Path(path.put())))
            {
                return false;
            }
            return path.get() != nullptr && CompareStringOrdinal(path.get(), -1, exe.c_str(), -1, TRUE) == CSTR_EQUAL;
        }
    } // namespace

    bool is_enabled(const std::filesystem::path &exe)
    {
        const Result<ComPtr<ITaskFolder>> folder = root_folder();
        if (!folder)
        {
            return false;
        }
        const Bstr name{TASK_NAME};
        ComPtr<IRegisteredTask> task;
        ComPtr<ITaskDefinition> definition;
        ComPtr<IActionCollection> actions;
        LONG count = 0;
        if (FAILED((*folder)->GetTask(name.get(), &task)) || FAILED(task->get_Definition(&definition)) ||
            FAILED(definition->get_Actions(&actions)) || FAILED(actions->get_Count(&count)))
        {
            return false;
        }
        for (LONG i = 1; i <= count; ++i)
        {
            if (action_runs(*actions.Get(), i, exe))
            {
                return true;
            }
        }
        return false;
    }

    Result<void> enable(const std::filesystem::path &exe)
    {
        ACERFAN_TRY(folder, root_folder());
        ACERFAN_TRY(user, current_user());
        const Bstr name{TASK_NAME};
        const Bstr xml{task_xml(exe, user)};
        const Variant empty;
        ComPtr<IRegisteredTask> task;
        return check_hr(
            folder->RegisterTask(
                name.get(),
                xml.get(),
                TASK_CREATE_OR_UPDATE,
                empty.get(),
                empty.get(),
                TASK_LOGON_INTERACTIVE_TOKEN,
                empty.get(),
                &task
            ),
            "register the AcerFan logon task"
        );
    }

    Result<void> disable()
    {
        ACERFAN_TRY(folder, root_folder());
        const Bstr name{TASK_NAME};
        const HRESULT hr = folder->DeleteTask(name.get(), 0);
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        {
            return {};
        }
        return check_hr(hr, "delete the AcerFan logon task");
    }
} // namespace AcerFan::autostart
