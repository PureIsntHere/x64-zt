#include <std_include.hpp>
#include "loader/loader.hpp"
#include "loader/component_loader.hpp"

#include <utils/string.hpp>
#include <utils/flags.hpp>
#include <utils/io.hpp>
#include <filesystem>
#include <vector>

#include "game/mode.hpp"
#include "component/h1/command.hpp"

#define H1_BINARY "h1_mp64_ship.exe"
#define H2_BINARY0 "MW2CR.exe"
#define H2_BINARY1 "h2_sp64_bnet_ship.exe"
#define S1_BINARY "s1_mp64_ship.exe"
#define IW6_BINARY "iw6mp64_ship.exe"
#define IW7_BINARY "iw7_ship.exe"
#define T7_BINARY "BlackOps3_UnrankedDedicatedServer.exe"

namespace {
    // Exit hook
    DECLSPEC_NORETURN void WINAPI exit_hook(int code) {
        component_loader::pre_destroy();
        exit(code);
    }

    // SystemParametersInfoA hook
    BOOL WINAPI system_parameters_info_a(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni) {
        component_loader::post_unpack();
        return SystemParametersInfoA(uiAction, uiParam, pvParam, fWinIni);
    }

    // Recursive/non-recursive batch dump
    void run_batch_dump(const std::string& directory, bool recursive) {
        namespace fs = std::filesystem;
        fs::path base(directory);
        if (!fs::exists(base) || !fs::is_directory(base)) {
            MessageBoxA(nullptr, ("[BatchDump] Invalid directory: " + directory).c_str(), "BatchDump", MB_ICONERROR);
            exit(1);
        }
        std::vector<std::string> zones;
        if (recursive) {
            for (auto& e : fs::recursive_directory_iterator(base)) {
                if (e.is_regular_file() && e.path().extension() == ".ff")
                    zones.push_back(e.path().string());
            }
        }
        else {
            for (auto& e : fs::directory_iterator(base)) {
                if (e.is_regular_file() && e.path().extension() == ".ff")
                    zones.push_back(e.path().string());
            }
        }
        // Execute dumpzone for each
        for (auto& z : zones) {
            h1::command::execute("dumpzone " + z, true);
        }
        exit(0);
    }

    // h1, h2, s1, iw6 namespaces unchanged from original source
    namespace t7
    {
        BOOL set_process_dpi_aware_stub()
        {
            component_loader::post_unpack();
            return SetProcessDPIAware();
        }

        FARPROC load_binary()
        {
            loader loader;
            utils::nt::library self;

            loader.set_import_resolver([self](const std::string& library, const std::string& function) -> void*
                {
                    if (function == "ExitProcess") return exit_hook;
                    if (function == "SetProcessDPIAware") return set_process_dpi_aware_stub;
                    return component_loader::load_import(library, function);
                });

            std::string binary = T7_BINARY;
            std::string data;
            if (!utils::io::read_file(binary, &data))
            {
                throw std::runtime_error(utils::string::va(
                    "Failed to read game binary (%s)!\nPlease copy the t7-zonetool.exe into your Call of Duty: Black Ops 3 UnrankedServer installation folder and run it from there.",
                    binary.data()));
            }

            return loader.load_library(binary);
        }
    }
    namespace iw7
    {
        DWORD_PTR WINAPI set_thread_affinity_mask(HANDLE hThread, DWORD_PTR dwThreadAffinityMask)
        {
            component_loader::post_unpack();
            return SetThreadAffinityMask(hThread, dwThreadAffinityMask);
        }

        FARPROC load_binary()
        {
            loader loader;
            utils::nt::library self;

            loader.set_import_resolver([self](const std::string& library, const std::string& function) -> void*
                {
                    if (library == "steam_api64.dll" && function != "SteamAPI_Shutdown")
                        return self.get_proc<FARPROC>(function);
                    if (function == "ExitProcess") return exit_hook;
                    if (function == "SetThreadAffinityMask") return set_thread_affinity_mask;
                    return component_loader::load_import(library, function);
                });

            std::string binary = IW7_BINARY;
            std::string data;
            if (!utils::io::read_file(binary, &data))
            {
                throw std::runtime_error(utils::string::va(
                    "Failed to read game binary (%s)!\nPlease copy the iw7-zonetool.exe into your Call of Duty: Infinite Warfare installation folder and run it from there.",
                    binary.data()));
            }

            return loader.load_library(binary);
        }

        void remove_crash_file()
        {
            utils::io::remove_file("__iw7_ship");
        }
    }
    namespace h1
    {
        void get_old_exe(std::string* binary, std::string* data)
        {
            const auto patched_binary = "h1_mp64_zt.exe"s;

            try
            {
                if (!utils::io::file_exists(patched_binary) && !utils::io::write_file(patched_binary, *data, false))
                {
                    throw std::runtime_error("Could not write file");
                }
            }
            catch (const std::exception& e)
            {
                throw std::runtime_error(
                    utils::string::va("Could not create game binary for %s! %s",
                        binary->data(), e.what())
                );
            }

            *binary = patched_binary;
        }

        FARPROC load_binary()
        {
            loader loader;
            utils::nt::library self;

            loader.set_import_resolver([self](const std::string& library, const std::string& function) -> void*
                {
                    if (library == "steam_api64.dll")
                    {
                        return self.get_proc<FARPROC>(function);
                    }
                    else if (function == "ExitProcess")
                    {
                        return exit_hook;
                    }
                    else if (function == "SystemParametersInfoA")
                    {
                        return system_parameters_info_a;
                    }

                    return component_loader::load_import(library, function);
                });

            std::string binary = H1_BINARY;

            std::string data;
            if (!utils::io::read_file(binary, &data))
            {
                throw std::runtime_error(utils::string::va(
                    "Failed to read game binary (%s)!\nPlease copy the h1-zonetool.exe into your Call of Duty: Modern Warfare Remastered installation folder and run it from there.",
                    binary.data()));
            }

            data = utils::nt::load_resource(GAME_BINARY_EXE);
            if (data.empty())
            {
                throw std::runtime_error("Something went terribly wrong while loading the game binary...");
            }

            get_old_exe(&binary, &data);
            return loader.load_library(binary);
        }

        void remove_crash_file()
        {
            utils::io::remove_file("__h1Exe");
        }
    }
    namespace h2
    {
        FARPROC WINAPI get_proc_address(const HMODULE hModule, const LPCSTR lpProcName)
        {
            if (lpProcName == "InitializeCriticalSectionEx"s)
            {
                component_loader::post_unpack();
            }

            return GetProcAddress(hModule, lpProcName);
        }

        void apply_aslr_patch(std::string* data)
        {
            // sp binary
            if (data->size() != 0xE1E0C8)
            {
                throw std::runtime_error("File size mismatch, bad game files");
            }

            auto* dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(&data->at(0));
            auto* nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(&data->at(dos_header->e_lfanew));
            auto* optional_header = &nt_headers->OptionalHeader;

            if (optional_header->DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)
            {
                optional_header->DllCharacteristics &= ~(IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE);
            }
        }

        void get_aslr_patched_binary(std::string* binary, std::string* data)
        {
            const auto patched_binary = "h2_sp_patched.exe"s;

            try
            {
                apply_aslr_patch(data);
                if (!utils::io::file_exists(patched_binary) && !utils::io::write_file(patched_binary, *data, false))
                {
                    throw std::runtime_error("Could not write file");
                }
            }
            catch (const std::exception& e)
            {
                throw std::runtime_error(
                    utils::string::va("Could not create aslr patched binary for %s! %s",
                        binary->data(), e.what())
                );
            }

            *binary = patched_binary;
        }

        FARPROC load_binary()
        {
            loader loader;
            utils::nt::library self;

            loader.set_import_resolver([self](const std::string& library, const std::string& function) -> void*
                {
                    if (function == "ExitProcess")
                    {
                        return exit_hook;
                    }
                    else if (function == "SystemParametersInfoA")
                    {
                        return system_parameters_info_a;
                    }
                    else if (function == "GetProcAddress")
                    {
                        return get_proc_address;
                    }

                    return component_loader::load_import(library, function);
                });

            std::string binary = H2_BINARY0;
            if (!utils::io::file_exists(binary))
            {
                binary = H2_BINARY1;
            }

            std::string data;
            if (!utils::io::read_file(binary, &data))
            {
                throw std::runtime_error(utils::string::va(
                    "Failed to read game binary (%s)!\nPlease copy the h2-zonetool.exe into your Call of Duty: Modern Warfare 2 Campaign Remastered installation folder and run it from there.",
                    binary.data()));
            }

            get_aslr_patched_binary(&binary, &data);
            return loader.load_library(binary);
        }

        void remove_crash_file()
        {
            utils::io::remove_file("__h2Exe");
        }
    }
    namespace s1
    {
        FARPROC WINAPI get_proc_address(const HMODULE hModule, const LPCSTR lpProcName)
        {
            if (lpProcName == "GlobalMemoryStatusEx"s)
            {
                component_loader::post_unpack();
            }

            return GetProcAddress(hModule, lpProcName);
        }

        FARPROC load_binary()
        {
            loader loader;
            utils::nt::library self;

            loader.set_import_resolver([self](const std::string& library, const std::string& function) -> void*
                {
                    if (library == "steam_api64.dll")
                    {
                        return self.get_proc<FARPROC>(function);
                    }
                    else if (function == "ExitProcess")
                    {
                        return exit_hook;
                    }
                    else if (function == "SystemParametersInfoA")
                    {
                        return system_parameters_info_a;
                    }
                    else if (function == "GetProcAddress")
                    {
                        return get_proc_address;
                    }

                    return component_loader::load_import(library, function);
                });

            std::string binary = S1_BINARY;

            std::string data;
            if (!utils::io::read_file(binary, &data))
            {
                throw std::runtime_error(utils::string::va(
                    "Failed to read game binary (%s)!\n",
                    binary.data()));
            }

            return loader.load_library(binary);
        }

        void remove_crash_file()
        {
            utils::io::remove_file("__s1Exe");
        }
    }
    namespace iw6
    {
        FARPROC load_binary()
        {
            loader loader;
            utils::nt::library self;

            loader.set_import_resolver([self](const std::string& library, const std::string& function) -> void*
                {
                    if (library == "steam_api64.dll")
                    {
                        return self.get_proc<FARPROC>(function);
                    }
                    else if (function == "ExitProcess")
                    {
                        return exit_hook;
                    }
                    else if (function == "SystemParametersInfoA")
                    {
                        return system_parameters_info_a;
                    }

                    return component_loader::load_import(library, function);
                });

            std::string binary = IW6_BINARY;
            std::string data;
            if (!utils::io::read_file(binary, &data))
            {
                throw std::runtime_error(utils::string::va("Failed to read game binary! (%s)", binary.data()));
            }

            return loader.load_library(binary);
        }

        void remove_crash_file()
        {
            utils::io::remove_file("__iw6mp64_ship");
        }

        void verify_ghost_version()
        {
            const auto value = *reinterpret_cast<DWORD*>(0x140001337);
            if (value != 0xDB0A33E7)
            {
                throw std::runtime_error("Unsupported Call of Duty: Ghosts version");
            }
        }
    }

    void remove_crash_file() {
        switch (game::get_mode()) {
        case game::iw6: return iw6::remove_crash_file();
        case game::s1: return s1::remove_crash_file();
        case game::h1: return h1::remove_crash_file();
        case game::h2: return h2::remove_crash_file();
        case game::iw7: return iw7::remove_crash_file();
        }
    }
    FARPROC load_binary() {
        switch (game::get_mode()) {
        case game::iw6: return iw6::load_binary();
        case game::s1: return s1::load_binary();
        case game::h1: return h1::load_binary();
        case game::h2: return h2::load_binary();
        case game::iw7: return iw7::load_binary();
        case game::t7: return t7::load_binary();
        }
        return nullptr;
    }
    void verify_version() {
        if (game::get_mode() == game::iw6) iw6::verify_ghost_version();
    }

    int main_internal(const std::vector<std::string>& args) {
        // Early batch check
        if (!args.empty() && (args[0] == "--batchdump" || args[0] == "--batchdumpwalk")) {
            bool walk = args[0] == "--batchdumpwalk";
            if (args.size() >= 2) run_batch_dump(args[1], walk);
            MessageBoxA(nullptr, "Usage: zonetool.exe --batchdump[walk] <folder>", "BatchDump", MB_ICONERROR);
            return 1;
        }
        // Normal init
        srand((uint32_t)time(nullptr));
        remove_crash_file();
        component_loader::sort();
        auto premature = true;
        const auto _ = gsl::finally([&] { if (premature) component_loader::pre_destroy(); });
        FARPROC ep = nullptr;
        try {
            if (!component_loader::post_start()) return 0;
            ep = load_binary(); if (!ep) throw std::runtime_error("Unable to load binary");
            verify_version(); if (!component_loader::post_load()) return 0;
        }
        catch (std::exception& e) {
            MessageBoxA(nullptr, e.what(), "ERROR", MB_ICONERROR);
            return 1;
        }
        premature = false;
        return (int)ep();
    }
}

int main() {
    std::vector<std::string> args;
    for (int i = 1; i < __argc; ++i) args.emplace_back(__argv[i]);
    if (utils::io::file_exists(H1_BINARY)) game::set_mode(game::game_mode::h1);
    else if (utils::io::file_exists(H2_BINARY0) || utils::io::file_exists(H2_BINARY1)) game::set_mode(game::game_mode::h2);
    else if (utils::io::file_exists(S1_BINARY)) game::set_mode(game::game_mode::s1);
    else if (utils::io::file_exists(IW6_BINARY)) game::set_mode(game::game_mode::iw6);
    else if (utils::io::file_exists(IW7_BINARY)) game::set_mode(game::game_mode::iw7);
    else if (utils::io::file_exists(T7_BINARY)) game::set_mode(game::game_mode::t7);
    else { MessageBoxA(nullptr, "No game binary found", "ERROR", MB_ICONERROR); return 1; }
    return main_internal(args);
}
int __stdcall WinMain(HINSTANCE, HINSTANCE, PSTR, int) { return main(); }
