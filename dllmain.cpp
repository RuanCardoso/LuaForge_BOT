#include <Windows.h>

// Entry points de cada target
void PxG_Init(HMODULE module);
void OTP_Init(HMODULE module);
void OH_Init(HMODULE hModule);

// ================================================================
// Seletor de Target - descomente APENAS UMA das linhas abaixo:
// ================================================================
//#define TARGET_POKEXGAMES
#define TARGET_OTPOKEMON
//#define TARGET_ONCEHUMAN
// ================================================================

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
#if defined(TARGET_POKEXGAMES)
        PxG_Init(module);
#elif defined(TARGET_OTPOKEMON)
        OTP_Init(module);
#elif defined(TARGET_ONCEHUMAN)
        OH_Init(module);
#else
#error "Nenhum target selecionado. Descomente uma das opcoes acima."
#endif
    }
    return TRUE;
}
