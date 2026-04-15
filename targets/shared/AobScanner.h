#pragma once
/*
 * AobScanner.h - Scanner de padrões AOB em memória
 *
 * Usado para localizar funções do jogo em runtime, independente de
 * ASLR ou ofuscação que altere os RVAs entre execuções.
 */

#include <Windows.h>

namespace shared {

/**
 * @brief Escaneia a memória do módulo principal em busca de um padrão AOB.
 *
 * Percorre toda a região de código do executável comparando bytes.
 * O caractere '?' funciona como wildcard e casa com qualquer byte.
 *
 * @param pattern Padrão no formato "AA BB ? CC DD" ou "AA BB ?? CC DD"
 * @return Endereço absoluto da primeira ocorrência, ou 0 se não encontrado
 */
uintptr_t AobScan(const char* pattern);

} // namespace shared
