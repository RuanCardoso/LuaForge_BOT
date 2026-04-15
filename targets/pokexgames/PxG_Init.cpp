/*
 * PxG_Init.cpp - Ponto de entrada do módulo PokeXgAMES
 *
 * PxG_Init() é chamado pelo DllMain quando a DLL é injetada.
 * Cria uma thread separada para a inicialização porque operações
 * bloqueantes dentro do DllMain causam deadlocks no Windows.
 */

#include "PxG.h"
#include "../shared/Overlay.h"

/**
 * @brief Thread de inicialização do módulo PxG.
 *
 * Chama InstallHooks() e reporta o resultado no console.
 * Encerra ao terminar (o módulo permanece injetado).
 */
static DWORD WINAPI PxG_Thread(LPVOID)
{
    if (!pxg::InstallHooks()) {
        std::printf("[-] PxG: falha ao instalar hooks\n");
        return 0;
    }

    /* Inicializa o overlay após MH_Initialize() — passa callback de execução Lua */
    shared::Overlay_Init([](const char* code) {
        pxg::ExecuteString(code);
    });

    return 0;
}

/**
 * @brief Ponto de entrada do módulo PokeXgAMES.
 *
 * Chamado pelo DllMain em DLL_PROCESS_ATTACH.
 * @param module Handle da DLL injetada
 */
void PxG_Init(HMODULE module)
{
    DisableThreadLibraryCalls(module);
    HANDLE hThread = CreateThread(nullptr, 0, PxG_Thread, nullptr, 0, nullptr);
    if (hThread)
        CloseHandle(hThread);
}
