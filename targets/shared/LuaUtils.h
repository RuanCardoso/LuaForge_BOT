#pragma once

/*
 * LuaUtils.h - Utilitários Lua compartilhados entre targets
 *
 * Contém funções que são idênticas em todos os targets baseados em Lua
 * (PokeXgAMES e OTPokemon), eliminando duplicação de código.
 *
 * Funções independentes de target:
 *   GetGameWindow()      — localiza e cacheia o HWND do processo atual
 *   Lua_GetWindowSize()  — wsize(): largura e altura da área cliente
 *   Lua_GetWindowPos()   — wpos(): posição da janela na tela
 *   Lua_GetCursorPos()   — cpos(): posição atual do cursor do sistema
 */

#include <Windows.h>

struct lua_State;

namespace shared {

/**
 * @brief Obtém o HWND da janela principal do processo atual.
 *
 * Varre todas as janelas visíveis pertencentes ao processo via EnumWindows.
 * O resultado é cacheado em uma variável estática — a busca ocorre
 * apenas na primeira chamada.
 */
HWND GetGameWindow();

/**
 * @brief wsize() — Retorna largura e altura da área cliente da janela.
 *
 * Empurra dois valores no stack Lua: largura, altura (em pixels).
 * Manipula o stack diretamente sem depender de ponteiros resolvidos.
 * @return 2 (número de valores retornados ao Lua)
 */
int Lua_GetWindowSize(lua_State* L);

/**
 * @brief wpos() — Retorna a posição da janela do jogo na tela.
 *
 * Empurra dois valores no stack Lua: x, y (canto superior esquerdo).
 * @return 2 (número de valores retornados ao Lua)
 */
int Lua_GetWindowPos(lua_State* L);

/**
 * @brief cpos() — Retorna a posição atual do cursor em coordenadas de tela.
 *
 * Empurra dois valores no stack Lua: x, y.
 * @return 2 (número de valores retornados ao Lua)
 */
int Lua_GetCursorPos(lua_State* L);

} // namespace shared
