/*
 * OH_Init.cpp - Loop principal e ponto de entrada do módulo Once Human
 *
 * OH_Loop() é a função que roda em uma thread separada após a injeção.
 * Ela inicializa o console, resolve os ponteiros Python do executável
 * e processa hotkeys até que DELETE seja pressionado.
 */

#include "OH.h"
#include <cstdio>

namespace oh {

/**
 * @brief Loop principal da sessão injetada no Once Human.
 *
 * Fluxo de execução:
 *   1. Aloca console e abre stdout
 *   2. Obtém ponteiros das funções Python via GetProcAddress
 *   3. Valida que os ponteiros foram encontrados
 *   4. Processa hotkeys em loop:
 *        DELETE → encerra o loop e desinjeta
 *        HOME   → executa C:\test.py
 *        END    → executa teste de tratamento de erro
 *   5. Libera o console ao sair
 */
void OH_Loop()
{
    // Aloca console para saída de debug
    AllocConsole();
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    printf("[*] OH: modulo injetado com sucesso no Once Human\n");

    // Resolve ponteiros das funções Python exportadas pelo executável
    HMODULE hExe = GetModuleHandleA(NULL);
    g_pyRunSimpleString = (FnPyRunSimpleString)GetProcAddress(hExe, "PyRun_SimpleString");
    g_pyGILEnsure       = (FnPyGILEnsure)      GetProcAddress(hExe, "PyGILState_Ensure");
    g_pyGILRelease      = (FnPyGILRelease)      GetProcAddress(hExe, "PyGILState_Release");

    if (!g_pyRunSimpleString || !g_pyGILEnsure || !g_pyGILRelease) {
        printf("[-] OH: falha ao obter funcoes Python — modulo encerrando\n");
        FreeConsole();
        if (f) fclose(f);
        return;
    }

    printf("[+] OH: PyRun_SimpleString  @ %p\n", (void*)g_pyRunSimpleString);
    printf("[+] OH: PyGILState_Ensure   @ %p\n", (void*)g_pyGILEnsure);
    printf("[+] OH: PyGILState_Release  @ %p\n", (void*)g_pyGILRelease);

    // Loop principal de hotkeys
    while (true) {
        // DELETE — encerra a sessão e desinjeta
        if (GetAsyncKeyState(VK_DELETE) & 0x8000)
            break;

        // HOME — executa o script externo C:\test.py
        if (GetAsyncKeyState(VK_HOME) & 0x8000) {
            printf("[>] OH: executando C:\\test.py\n");
            Py_Execute("exec(open('C:\\\\test.py').read())");
            Sleep(300); // debounce
        }

        // END — teste de tratamento de exceção Python
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            printf("[>] OH: teste de excecao Python\n");
            Py_Execute("print(10 / 0)");
            Sleep(300);
        }

        Sleep(30);
    }

    printf("[!] OH: encerrando sessao...\n");
    Sleep(2000);
    FreeConsole();
    if (f) fclose(f);
}

} // namespace oh

// ================================================================
// Ponto de entrada
// ================================================================

/**
 * @brief Ponto de entrada do módulo Once Human.
 *
 * Chamado pelo DllMain em DLL_PROCESS_ATTACH.
 * Cria uma thread separada para executar OH_Loop() sem bloquear o DllMain.
 */
void OH_Init(HMODULE hModule)
{
    DisableThreadLibraryCalls(hModule);
    HANDLE hThread = CreateThread(nullptr, 0,
        (LPTHREAD_START_ROUTINE)oh::OH_Loop, nullptr, 0, nullptr);
    if (hThread)
        CloseHandle(hThread);
}
