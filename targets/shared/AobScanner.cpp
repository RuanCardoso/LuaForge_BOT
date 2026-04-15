/*
 * AobScanner.cpp - Implementação do scanner de padrões AOB
 *
 * Obtém a região de código do módulo principal via cabeçalhos PE,
 * sem dependências externas além de Windows.h.
 */

#include "AobScanner.h"
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace shared {

uintptr_t AobScan(const char* pattern)
{
    if (!pattern || !*pattern) return 0;

    /* Obtém base e tamanho do módulo principal via cabeçalhos PE */
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const uintptr_t imageSize = nt->OptionalHeader.SizeOfImage;

    /* Converte a string de padrão em bytes + máscara de wildcards */
    struct Token { uint8_t byte; bool wildcard; };
    std::vector<Token> tokens;
    tokens.reserve(64);

    const char* p = pattern;
    while (*p) {
        /* Pula espaços */
        while (*p == ' ') ++p;
        if (!*p) break;

        if (*p == '?') {
            tokens.push_back({ 0, true });
            ++p;
            if (*p == '?') ++p; // aceita "??" além de "?"
        }
        else {
            tokens.push_back({ static_cast<uint8_t>(strtoul(p, nullptr, 16)), false });
            p += 2;
        }
    }

    if (tokens.empty()) return 0;

    const size_t      patLen = tokens.size();
    const uintptr_t   end    = base + imageSize - patLen;

    for (uintptr_t addr = base; addr < end; ++addr) {
        bool match = true;
        for (size_t i = 0; i < patLen && match; ++i) {
            if (!tokens[i].wildcard &&
                *reinterpret_cast<const uint8_t*>(addr + i) != tokens[i].byte)
            {
                match = false;
            }
        }
        if (match) {
            std::printf("[AOB] Padrão encontrado em 0x%llX\n",
                        static_cast<unsigned long long>(addr));
            return addr;
        }
    }

    std::printf("[-] AOB: padrão nao encontrado: %.40s...\n", pattern);
    return 0;
}

} // namespace shared
