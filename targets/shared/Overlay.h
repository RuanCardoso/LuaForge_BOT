#pragma once
/*
 * Overlay.h - Interface do overlay ImGui com janela Win32 dedicada
 *
 * Cria uma janela independente (WS_POPUP + WS_EX_TOPMOST) com device
 * D3D11 próprio. Não hookeia nada do jogo — funciona com qualquer
 * versão de DirectX/Vulkan/OpenGL.
 */

#include <Windows.h>
#include <functional>

namespace shared {

/* Callback chamado quando o usuário clica em "Executar" */
using FnExecuteLua = std::function<void(const char* code)>;

/**
 * @brief Inicia a thread do overlay ImGui.
 *
 * Cria uma janela Win32 dedicada (500 × 370) no canto superior esquerdo,
 * inicializa D3D11 e ImGui nessa janela e entra no loop de renderização.
 *
 * Deve ser chamado após InstallHooks() (que chama MH_Initialize).
 * @param executeFn Callback que enfileira o código Lua para execução
 */
void Overlay_Init(FnExecuteLua executeFn);

} // namespace shared
