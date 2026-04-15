#pragma once
/*
 * OH.h - Tipos e contexto global do alvo Once Human
 *
 * O Once Human embute um interpretador Python no seu executável.
 * Este módulo injeta código Python diretamente via PyRun_SimpleString,
 * gerenciando o GIL (Global Interpreter Lock) para thread-safety.
 *
 * Hotkeys durante a sessão injetada:
 *   HOME   — executa C:\test.py
 *   END    — teste de tratamento de erros Python
 *   DELETE — desinjeta o módulo
 */

#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <iostream>
#include <string>

namespace oh {

// ================================================================
// Tipos de ponteiros de função Python
// ================================================================

/* Executa uma string de código Python no interpretador embutido */
using FnPyRunSimpleString = int(__cdecl*)(const char* code);

/* Adquire e libera o GIL (Global Interpreter Lock) para thread-safety */
using FnPyGILEnsure  = void*(__cdecl*)();
using FnPyGILRelease = void(__cdecl*)(void*);

/* Desserializa bytecode Python (.pyc) — hookado para captura de dumps */
using FnPyMarshal = void*(__cdecl*)(const char* string, int len);

// ================================================================
// Contexto global do módulo
// ================================================================

/**
 * @brief Ponteiros para as funções Python obtidos via GetProcAddress.
 * Populados em OH_Loop() na inicialização.
 */
extern FnPyRunSimpleString g_pyRunSimpleString;
extern FnPyGILEnsure       g_pyGILEnsure;
extern FnPyGILRelease      g_pyGILRelease;

/* Ponteiro original de PyMarshal_ReadObjectFromString (antes do hook) */
extern LPVOID g_origPyMarshal;

/* Contador de dumps de bytecode salvos em C:\dump\ */
extern int g_dumpCount;

// ================================================================
// Funções Python expostas (implementadas em OH_Python.cpp)
// ================================================================

/**
 * @brief Executa código Python com tratamento de erros e redirecionamento de saída.
 * @param code Código Python como string C
 */
void Py_Execute(const char* code);

/**
 * @brief Hook de PyMarshal_ReadObjectFromString — salva bytecode interceptado.
 * @param string Buffer de bytecode
 * @param len    Tamanho do buffer
 */
void* Py_HookMarshal(const char* string, int len);

// ================================================================
// Loop principal e inicialização (implementados em OH_Init.cpp)
// ================================================================

/**
 * @brief Loop principal da sessão injetada.
 *
 * Inicializa o console, obtém os ponteiros Python e processa hotkeys
 * até que DELETE seja pressionado.
 */
void OH_Loop();

} // namespace oh

/* Ponto de entrada chamado pelo DllMain em DLL_PROCESS_ATTACH */
void OH_Init(HMODULE hModule);
