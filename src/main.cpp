/**
 * @file main.cpp
 * @author linome (francukigor549@gmail.com)
 * @brief Main entry point of backup service for Windows.
 * @version 1.0.0
 * @date 2026-10-29
 * 
 * @details
 * Program should work as a service on Windows (7 and later).
 * Program should perform backup of files from specified directory to archive in ZIP format.
 *
 * SCM --- Service Control Manager.
 * SCP --- Service Control Program.
 *
 * General requirements:
 * 1. The program must not be interactive, all settings must be provided via configuration files.
 * 2. The program must create archive with backup of files from specified directory.
 * 3. Files to be backed up are specified with a mask (supports * and ? wildcards) or by names.
 * 4. Backup archive must use the ZIP format.
 * 5. Archivation must not utilize any external software.
 * 6. New files that fit a mask and changes to existing files must be added to the archive.
 * 7. Installation, uninstallation, start, and stop of the service must be performed via command line.
 * 8. Correct start/stop of the service when requested by SCM
 * 9. File/directory names are specified in UNC format (\\localhost\C$\path\to\file.txt)
 *
 * Configuration file format requirements:
 * 1. Name of directory to be backed up.
 * 2. Name of archive file to be created.
 * 3. Mask/names of files to be backed up.
 */

#include "main.hpp"

#include "backup.hpp"
#include "config.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace backup_service {
namespace {

constexpr DWORD service_wait_hint_ms = 10'000U;
constexpr DWORD milliseconds_per_second = 1'000U;
constexpr std::size_t executable_path_capacity = 32'768U;

struct service_context {
    SERVICE_STATUS_HANDLE status_handle{};
    SERVICE_STATUS status{};
    HANDLE stop_event{};
    std::filesystem::path executable_path;
};

[[nodiscard]] service_context& service_state() {
    static service_context state;
    return state;
}

void log_message(WORD type, const std::wstring& message) {
    OutputDebugStringW((message + L"\n").c_str());
    HANDLE event_source = RegisterEventSourceW(nullptr, std::data(service_name));
    if (event_source != nullptr) {
        std::array<LPCWSTR, 1> strings{message.c_str()};
        ReportEventW(event_source, type, 0U, 0U, nullptr, 1U, 0U, strings.data(), nullptr);
        DeregisterEventSource(event_source);
    }
}

struct status_update {
    DWORD state{};
    DWORD exit_code{NO_ERROR};
    DWORD wait_hint{};
};

void report_status(status_update update) {
    auto& context = service_state();
    context.status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    context.status.dwCurrentState = update.state;
    context.status.dwWin32ExitCode = update.exit_code;
    context.status.dwServiceSpecificExitCode = 0U;
    context.status.dwWaitHint = update.wait_hint;
    context.status.dwCheckPoint = update.state == SERVICE_START_PENDING || update.state == SERVICE_STOP_PENDING ? 1U : 0U;
    context.status.dwControlsAccepted = update.state == SERVICE_RUNNING
                                            ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN
                                            : 0U;
    if (context.status_handle != nullptr) {
        SetServiceStatus(context.status_handle, &context.status);
    }
}

void WINAPI service_control_handler(DWORD control) {
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        report_status({SERVICE_STOP_PENDING, NO_ERROR, service_wait_hint_ms});
        if (service_state().stop_event != nullptr) {
            SetEvent(service_state().stop_event);
        }
        break;
    case SERVICE_CONTROL_INTERROGATE:
        if (service_state().status_handle != nullptr) {
            SetServiceStatus(service_state().status_handle, &service_state().status);
        }
        break;
    default:
        break;
    }
}

[[nodiscard]] std::filesystem::path configuration_path() {
    return service_state().executable_path.parent_path() / L"SMIT52.ini";
}

[[nodiscard]] std::wstring error_message(const char* message) {
    UINT code_page = CP_UTF8;
    int size = MultiByteToWideChar(code_page, 0, message, -1, nullptr, 0);
    if (size <= 1) {
        code_page = CP_ACP;
        size = MultiByteToWideChar(code_page, 0, message, -1, nullptr, 0);
        if (size <= 1) {
            return L"Service operation failed.";
        }
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(code_page, 0, message, -1, result.data(), size);
    result.pop_back();
    return result;
}

[[nodiscard]] const wchar_t* service_state_name(DWORD state) {
    switch (state) {
    case SERVICE_STOPPED: return L"STOPPED";
    case SERVICE_START_PENDING: return L"START_PENDING";
    case SERVICE_STOP_PENDING: return L"STOP_PENDING";
    case SERVICE_RUNNING: return L"RUNNING";
    case SERVICE_CONTINUE_PENDING: return L"CONTINUE_PENDING";
    case SERVICE_PAUSE_PENDING: return L"PAUSE_PENDING";
    case SERVICE_PAUSED: return L"PAUSED";
    default: return L"UNKNOWN";
    }
}

void WINAPI service_main(DWORD argument_count, LPWSTR* arguments) {
    (void)argument_count;
    (void)arguments;
    auto& context = service_state();
    context.status_handle = RegisterServiceCtrlHandlerW(std::data(service_name), service_control_handler);
    if (context.status_handle == nullptr) {
        return;
    }
    report_status({SERVICE_START_PENDING, NO_ERROR, service_wait_hint_ms});
    context.stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (context.stop_event == nullptr) {
        report_status({SERVICE_STOPPED, GetLastError()});
        return;
    }

    try {
        const auto config = load_configuration(configuration_path());
        report_status({SERVICE_RUNNING});
        bool stopping = false;
        while (!stopping) {
            try {
                create_backup(config, [&context] {
                    return WaitForSingleObject(context.stop_event, 0U) == WAIT_OBJECT_0;
                });
            } catch (const std::exception& error) {
                log_message(EVENTLOG_ERROR_TYPE, error_message(error.what()));
            }
            stopping = WaitForSingleObject(context.stop_event,
                                           config.interval_seconds * milliseconds_per_second) == WAIT_OBJECT_0;
        }
        log_message(EVENTLOG_INFORMATION_TYPE, L"Backup service stopped.");
        CloseHandle(context.stop_event);
        context.stop_event = nullptr;
        report_status({SERVICE_STOPPED});
    } catch (const std::exception& error) {
        log_message(EVENTLOG_ERROR_TYPE, error_message(error.what()));
        CloseHandle(context.stop_event);
        context.stop_event = nullptr;
        report_status({SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR});
    }
}

[[nodiscard]] SC_HANDLE open_service_manager(DWORD access) {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, access);
    if (manager == nullptr) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "OpenSCManager failed");
    }
    return manager;
}

[[nodiscard]] SC_HANDLE open_service(SC_HANDLE manager, DWORD access) {
    SC_HANDLE service = OpenServiceW(manager, std::data(service_name), access);
    if (service == nullptr) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "OpenService failed");
    }
    return service;
}

int install_service() {
    SC_HANDLE manager = open_service_manager(SC_MANAGER_CREATE_SERVICE);
    SC_HANDLE service = CreateServiceW(manager, std::data(service_name), L"SMIT52 Backup Service",
                                       SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_STOP | DELETE,
                                       SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
                                       SERVICE_ERROR_NORMAL, service_state().executable_path.c_str(), nullptr,
                                       nullptr, nullptr, nullptr, nullptr);
    if (service == nullptr) {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        throw std::system_error(static_cast<int>(error), std::system_category(), "CreateService failed");
    }
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    std::wcout << L"Service installed. Configuration: " << configuration_path() << L"\n";
    return 0;
}

int remove_service() {
    SC_HANDLE manager = open_service_manager(SC_MANAGER_CONNECT);
    try {
        SC_HANDLE service = open_service(manager, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
        SERVICE_STATUS current_status{};
        if (ControlService(service, SERVICE_CONTROL_STOP, &current_status) == FALSE &&
            GetLastError() != ERROR_SERVICE_NOT_ACTIVE) {
            const DWORD error = GetLastError();
            CloseServiceHandle(service);
            throw std::system_error(static_cast<int>(error), std::system_category(), "Could not stop service");
        }
        if (DeleteService(service) == FALSE) {
            const DWORD error = GetLastError();
            CloseServiceHandle(service);
            throw std::system_error(static_cast<int>(error), std::system_category(), "DeleteService failed");
        }
        CloseServiceHandle(service);
    } catch (...) {
        CloseServiceHandle(manager);
        throw;
    }
    CloseServiceHandle(manager);
    std::wcout << L"Service marked for removal.\n";
    return 0;
}

int control_service(DWORD control) {
    SC_HANDLE manager = open_service_manager(SC_MANAGER_CONNECT);
    try {
        SC_HANDLE service = open_service(manager, control == SERVICE_CONTROL_STOP ? SERVICE_STOP : SERVICE_START);
        if (control == SERVICE_CONTROL_STOP) {
            SERVICE_STATUS current_status{};
            if (ControlService(service, control, &current_status) == FALSE && GetLastError() != ERROR_SERVICE_NOT_ACTIVE) {
                const DWORD error = GetLastError();
                CloseServiceHandle(service);
                throw std::system_error(static_cast<int>(error), std::system_category(), "ControlService failed");
            }
        } else if (StartServiceW(service, 0U, nullptr) == FALSE && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
            const DWORD error = GetLastError();
            CloseServiceHandle(service);
            throw std::system_error(static_cast<int>(error), std::system_category(), "StartService failed");
        }
        CloseServiceHandle(service);
    } catch (...) {
        CloseServiceHandle(manager);
        throw;
    }
    CloseServiceHandle(manager);
    std::wcout << (control == SERVICE_CONTROL_STOP ? L"Stop requested.\n" : L"Start requested.\n");
    return 0;
}

int show_status() {
    SC_HANDLE manager = open_service_manager(SC_MANAGER_CONNECT);
    SC_HANDLE service = OpenServiceW(manager, std::data(service_name), SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            std::wcout << L"Service state: NOT_INSTALLED\n";
            return 0;
        }
        throw std::system_error(static_cast<int>(error), std::system_category(), "OpenService failed");
    }

    try {
        SERVICE_STATUS current_status{};
        if (QueryServiceStatus(service, &current_status) == FALSE) {
            const DWORD error = GetLastError();
            CloseServiceHandle(service);
            throw std::system_error(static_cast<int>(error), std::system_category(), "QueryServiceStatus failed");
        }
        std::wcout << L"Service state: " << service_state_name(current_status.dwCurrentState) << L"\n";
        CloseServiceHandle(service);
    } catch (...) {
        CloseServiceHandle(manager);
        throw;
    }
    CloseServiceHandle(manager);
    return 0;
}

void print_usage() {
    std::wcout << L"Usage: SMIT52.exe [install|remove|start|stop|status]\n"
                  L"Without a command, the executable starts in SCM service mode.\n";
}

int run_command(std::wstring_view command) {
    try {
        if (command == L"install") return install_service();
        if (command == L"remove") return remove_service();
        if (command == L"start") return control_service(SERVICE_CONTROL_CONTINUE);
        if (command == L"stop") return control_service(SERVICE_CONTROL_STOP);
        if (command == L"status") return show_status();
        print_usage();
        return 2;
    } catch (const std::exception& error) {
        std::wcerr << L"Service command failed: " << error_message(error.what()) << L"\n";
        return 1;
    }
}

} // namespace
} // namespace backup_service

int wmain(int argc, wchar_t** argv) {
    using namespace backup_service;
    std::array<wchar_t, executable_path_capacity> executable_buffer{};
    const DWORD path_length = GetModuleFileNameW(nullptr, executable_buffer.data(),
                                                  static_cast<DWORD>(executable_buffer.size()));
    if (path_length == 0U || path_length >= executable_buffer.size()) {
        std::wcerr << L"Cannot determine executable path.\n";
        return 1;
    }
    service_state().executable_path = std::filesystem::path(std::wstring(executable_buffer.data(), path_length));

    if (argc > 1) {
        const auto arguments = std::span(argv, static_cast<std::size_t>(argc));
        return run_command(arguments.back());
    }

    std::array<wchar_t, std::size(service_name)> mutable_service_name{};
    std::ranges::copy(service_name, mutable_service_name.begin());
    std::array<SERVICE_TABLE_ENTRYW, 2> service_table{{
        {mutable_service_name.data(), service_main},
        {nullptr, nullptr},
    }};
    if (StartServiceCtrlDispatcherW(service_table.data()) == FALSE) {
        std::wcerr << L"StartServiceCtrlDispatcher failed: " << GetLastError()
                   << L". Run with an SCM command or start this executable through Windows Services.\n";
        return 1;
    }
    return 0;
}
