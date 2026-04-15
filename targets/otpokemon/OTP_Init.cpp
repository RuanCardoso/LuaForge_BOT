/*
 * OTP_Init.cpp - Ponto de entrada do módulo OT Pokemon
 */

#include "OTP.h"
#include "../shared/Overlay.h"

/**
 * @brief Thread de inicialização do módulo OTP.
 */
static DWORD WINAPI OTP_Thread(LPVOID)
{
    if (!otp::InstallHooks()) {
        std::printf("[-] OTP: falha ao instalar hooks\n");
        return 0;
    }

    /* Inicializa o overlay após MH_Initialize() — passa callback de execução Lua */
    shared::Overlay_Init([](const char* code) {
        otp::ExecuteString(code);
    });

    return 0;
}

/**
 * @brief Ponto de entrada do módulo OT Pokemon.
 * Chamado pelo DllMain em DLL_PROCESS_ATTACH.
 */
void OTP_Init(HMODULE module)
{
    DisableThreadLibraryCalls(module);
    HANDLE hThread = CreateThread(nullptr, 0, OTP_Thread, nullptr, 0, nullptr);
    if (hThread)
        CloseHandle(hThread);
}
