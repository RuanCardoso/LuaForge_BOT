/*
 * OH_Python.cpp - Integração com o interpretador Python do Once Human
 *
 * O Once Human embute o Python em seu executável e exporta as funções
 * do runtime como símbolos públicos. Este arquivo:
 *   - Executa strings de código Python via PyRun_SimpleString
 *   - Redireciona stdout/stderr para o console de debug
 *   - Intercepta PyMarshal_ReadObjectFromString para dump de bytecode
 */

#include "OH.h"
#include <cstdio>

namespace oh {

// ================================================================
// Variáveis globais (definições)
// ================================================================

FnPyRunSimpleString g_pyRunSimpleString = nullptr;
FnPyGILEnsure       g_pyGILEnsure       = nullptr;
FnPyGILRelease      g_pyGILRelease      = nullptr;
LPVOID              g_origPyMarshal     = nullptr;
int                 g_dumpCount         = 0;

// ================================================================
// Execução de código Python
// ================================================================

/**
 * @brief Executa código Python com redirecionamento de saída e tratamento de erros.
 *
 * O script é envolvido automaticamente em um bloco try/except Python.
 * stdout e stderr são redirecionados para o console do Windows (CONOUT$).
 * O GIL é adquirido antes da execução e liberado ao terminar.
 *
 * @param code Código Python a executar (pode conter múltiplas linhas com \n)
 */
void Py_Execute(const char* code)
{
    if (!g_pyRunSimpleString || !g_pyGILEnsure || !g_pyGILRelease) return;

    // Adquire o GIL antes de qualquer operação Python em thread externa
    void* gilState = g_pyGILEnsure();

    // Monta o script com redirecionamento de saída e tratamento de erros
    std::string script =
        "import sys\nimport traceback\n"
        "class _Console:\n"
        "    def write(self, m):\n"
        "        try:\n"
        "            with open('CONOUT$', 'w') as f: f.write(m)\n"
        "        except: pass\n"
        "    def flush(self): pass\n"
        "sys.stdout = sys.stderr = _Console()\n"
        "try:\n";

    // Indenta cada linha do código do usuário com 4 espaços
    std::string indentedCode = "    ";
    indentedCode += code;
    size_t pos = 0;
    while ((pos = indentedCode.find('\n', pos)) != std::string::npos) {
        indentedCode.replace(pos, 1, "\n    ");
        pos += 5;
    }

    script += indentedCode;
    script +=
        "\nexcept Exception:\n"
        "    with open('CONOUT$', 'w') as f: f.write(traceback.format_exc())\n";

    g_pyRunSimpleString(script.c_str());

    // Libera o GIL após a execução
    g_pyGILRelease(gilState);
}

// ================================================================
// Hook de desserialização de bytecode
// ================================================================

/**
 * @brief Hook de PyMarshal_ReadObjectFromString.
 *
 * Intercepta a desserialização de módulos Python compilados (.pyc).
 * Salva cada buffer interceptado em C:\dump\N.pyc para análise offline.
 * Após salvar, delega para a função original.
 *
 * @param string Buffer de bytecode Python
 * @param len    Tamanho do buffer em bytes
 */
void* Py_HookMarshal(const char* string, int len)
{
    printf("[*] OH: PyMarshal interceptado (%d bytes)\n", len);

    // Cria o diretório de dump se necessário (silencioso em caso de erro)
    CreateDirectoryA("C:\\dump", nullptr);

    char path[256];
    sprintf_s(path, "C:\\dump\\%d.pyc", g_dumpCount++);

    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(string, 1, len, f);
        fclose(f);
        printf("[+] OH: dump salvo em %s\n", path);
    }

    // Chama a função original para não interromper o fluxo do jogo
    auto original = reinterpret_cast<FnPyMarshal>(g_origPyMarshal);
    return original(string, len);
}

} // namespace oh
