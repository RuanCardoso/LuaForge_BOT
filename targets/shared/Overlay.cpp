/*
 * Overlay.cpp - Overlay ImGui com janela Win32 dedicada
 *
 * Abordagem: janela independente, sem nenhum hook no jogo.
 *
 * Fluxo:
 *   Overlay_Init()
 *     → cria thread dedicada (OverlayThread)
 *       → registra classe Win32 "LuaForge_Overlay"
 *       → cria janela POPUP + WS_EX_TOPMOST (500 × 370, canto superior esquerdo)
 *       → cria device D3D11 + swap chain próprios para essa janela
 *       → inicializa ImGui (Win32 + DX11)
 *       → loop: PeekMessage → NewFrame → RenderOverlay → Present
 *
 * Vantagens sobre hook de Present:
 *   - Funciona independente da versão de DirectX do jogo (DX9/11/12/Vulkan)
 *   - Zero manutenção ao trocar de target
 *   - Sem risco de conflito com anti-cheat de render
 */

#include "Overlay.h"
#include "LuaUtils.h"

#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <string>
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace shared {

// ================================================================
// Dimensões e estado global da overlay
// ================================================================

static constexpr int OVERLAY_W = 500;
static constexpr int OVERLAY_H = 370;

static FnExecuteLua            g_executeFn;
static bool                    g_running      = false;

/* Recursos D3D11 da janela dedicada */
static ID3D11Device*           g_device       = nullptr;
static ID3D11DeviceContext*    g_context      = nullptr;
static IDXGISwapChain*         g_swapChain    = nullptr;
static ID3D11RenderTargetView* g_mainRTV      = nullptr;

/* Estado da UI */
static char        g_scriptBuffer[8192] = "";
static std::string g_statusMsg          = "";
static bool        g_statusError        = false;

// ================================================================
// WndProc da janela dedicada
// ================================================================

static LRESULT __stdcall OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_DESTROY:
        g_running = false;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ================================================================
// Criação e destruição do RenderTargetView
// ================================================================

static void CriarRTV()
{
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_mainRTV);
        backBuffer->Release();
    }
}

static void LiberarRTV()
{
    if (g_mainRTV) { g_mainRTV->Release(); g_mainRTV = nullptr; }
}

// ================================================================
// Estilo visual — tema escuro com acento roxo
// ================================================================

static void AplicarEstilo()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 8.0f;
    s.ChildRounding     = 6.0f;
    s.FrameRounding     = 5.0f;
    s.PopupRounding     = 5.0f;
    s.ScrollbarRounding = 5.0f;
    s.GrabRounding      = 4.0f;
    s.TabRounding       = 4.0f;
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.ItemSpacing       = ImVec2(8.0f, 6.0f);
    s.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    s.WindowPadding     = ImVec2(14.0f, 12.0f);
    s.FramePadding      = ImVec2(8.0f,  5.0f);
    s.ScrollbarSize     = 10.0f;
    s.GrabMinSize       = 8.0f;
    s.IndentSpacing     = 16.0f;

    ImVec4* c = s.Colors;

    c[ImGuiCol_WindowBg]             = ImVec4(0.09f, 0.09f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.06f, 0.06f, 0.09f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.09f, 0.09f, 0.13f, 1.00f);
    c[ImGuiCol_Border]               = ImVec4(0.22f, 0.20f, 0.32f, 0.80f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.07f, 0.07f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.14f, 0.10f, 0.26f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.07f, 0.07f, 0.11f, 1.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.06f, 0.05f, 0.09f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.12f, 0.09f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.16f, 0.12f, 0.28f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.22f, 0.16f, 0.42f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.34f, 0.26f, 0.62f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.44f, 0.34f, 0.76f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.18f, 0.14f, 0.34f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.26f, 0.20f, 0.48f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.34f, 0.26f, 0.60f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.04f, 0.04f, 0.06f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.22f, 0.17f, 0.38f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.23f, 0.52f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.40f, 0.31f, 0.66f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.22f, 0.20f, 0.32f, 1.00f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.36f, 0.28f, 0.58f, 1.00f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.46f, 0.36f, 0.72f, 1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.70f, 0.58f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.38f, 0.29f, 0.64f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.50f, 0.39f, 0.80f, 1.00f);
    c[ImGuiCol_Text]                 = ImVec4(0.92f, 0.91f, 0.96f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.44f, 0.41f, 0.56f, 1.00f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.20f, 0.15f, 0.36f, 0.50f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.30f, 0.23f, 0.54f, 0.80f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.40f, 0.31f, 0.68f, 1.00f);
    c[ImGuiCol_Tab]                  = ImVec4(0.10f, 0.09f, 0.17f, 1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.28f, 0.21f, 0.50f, 1.00f);
    c[ImGuiCol_TabActive]            = ImVec4(0.18f, 0.14f, 0.34f, 1.00f);
    c[ImGuiCol_TabUnfocused]         = ImVec4(0.08f, 0.07f, 0.13f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.14f, 0.11f, 0.26f, 1.00f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.07f, 0.07f, 0.11f, 1.00f);
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);
}

// ================================================================
// Conteúdo do overlay
// ================================================================

static void RenderOverlay()
{
    /* Ocupa toda a janela Win32 dedicada */
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)OVERLAY_W, (float)OVERLAY_H), ImGuiCond_Always);

    const ImGuiWindowFlags wflags =
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse;

    if (!ImGui::Begin("  LuaForge", nullptr, wflags)) {
        ImGui::End();
        return;
    }

    /* ---- Rótulo da área de código ---- */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.45f, 0.90f, 1.0f));
    ImGui::TextUnformatted("  Script");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.33f, 0.45f, 1.0f));
    ImGui::TextUnformatted("(Tab para indentar)");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    /* ---- Editor de código Lua ---- */
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.05f, 0.08f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

    ImGui::InputTextMultiline(
        "##lua_editor",
        g_scriptBuffer,
        sizeof(g_scriptBuffer),
        ImVec2(-1.0f, 252.0f),
        ImGuiInputTextFlags_AllowTabInput
    );

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    /* ---- Botão Executar ---- */
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.24f, 0.18f, 0.46f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.29f, 0.68f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.50f, 0.39f, 0.84f, 1.0f));

    const bool executar = ImGui::Button("  Executar  ", ImVec2(-1.0f, 34.0f));

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::Spacing();

    if (executar) {
        if (g_executeFn && g_scriptBuffer[0] != '\0') {
            g_executeFn(g_scriptBuffer);
            g_statusMsg   = "[+] Script enfileirado para execucao";
            g_statusError = false;
        }
        else {
            g_statusMsg   = "[!] Editor vazio — escreva um script primeiro";
            g_statusError = true;
        }
    }

    /* ---- Barra de status ---- */
    if (!g_statusMsg.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const ImVec4 cor = g_statusError
            ? ImVec4(1.00f, 0.40f, 0.40f, 1.0f)
            : ImVec4(0.40f, 1.00f, 0.60f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, cor);
        ImGui::TextUnformatted(g_statusMsg.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

// ================================================================
// Thread dedicada do overlay
// ================================================================

static DWORD WINAPI OverlayThread(LPVOID)
{
    const char* CLASS_NAME = "LuaForge_Overlay";

    /* Usa GetGameWindow() do LuaUtils — já filtra console e janelas auxiliares */
    HWND gameHwnd = GetGameWindow();

    /* Posição inicial: canto superior esquerdo da janela do jogo */
    int startX = 12, startY = 12;
    if (gameHwnd) {
        RECT r = {};
        GetWindowRect(gameHwnd, &r);
        startX = r.left + 12;
        startY = r.top  + 12;
    }

    /* ---- Registra e cria a janela Win32 dedicada ---- */
    WNDCLASSEXA wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.lpfnWndProc    = OverlayWndProc;
    wc.hInstance      = GetModuleHandle(nullptr);
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName  = CLASS_NAME;
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST,
        CLASS_NAME, "LuaForge",
        WS_POPUP | WS_VISIBLE,
        startX, startY,
        OVERLAY_W, OVERLAY_H,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!hwnd) {
        std::printf("[-] Overlay: falha ao criar janela\n");
        UnregisterClassA(CLASS_NAME, wc.hInstance);
        return 1;
    }

    /* ---- Cria device D3D11 e swap chain exclusivos ---- */
    DXGI_SWAP_CHAIN_DESC sd          = {};
    sd.BufferCount                   = 2;
    sd.BufferDesc.Width              = OVERLAY_W;
    sd.BufferDesc.Height             = OVERLAY_H;
    sd.BufferDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                         = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                   = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                  = hwnd;
    sd.SampleDesc.Count              = 1;
    sd.Windowed                      = TRUE;
    sd.SwapEffect                    = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL featureLevel;

    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevels, 2, D3D11_SDK_VERSION, &sd,
        &g_swapChain, &g_device, &featureLevel, &g_context);

    if (FAILED(hr)) {
        std::printf("[-] Overlay: D3D11CreateDeviceAndSwapChain falhou (0x%08X)\n", hr);
        DestroyWindow(hwnd);
        UnregisterClassA(CLASS_NAME, wc.hInstance);
        return 1;
    }

    CriarRTV();

    /* ---- Inicializa ImGui ---- */
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = nullptr; // não salva layout no disco

    AplicarEstilo();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    std::printf("[+] Overlay: janela criada e ImGui inicializado\n");

    /* ---- Loop principal ---- */
    g_running = true;
    const float clearColor[4] = { 0.09f, 0.09f, 0.13f, 1.00f }; // = WindowBg

    while (g_running) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (msg.message == WM_QUIT)
                g_running = false;
        }

        if (!g_running) break;

        /* Re-busca a janela do jogo caso ainda não tenha sido encontrada */
        if (!gameHwnd || !IsWindow(gameHwnd))
            gameHwnd = GetGameWindow();

        bool emFoco = false;
        if (gameHwnd && IsWindow(gameHwnd)) {
            /* Exibe o overlay apenas quando o jogo (ou o próprio overlay) está em foco */
            const HWND fg = GetForegroundWindow();
            emFoco        = (fg == gameHwnd || fg == hwnd);
            ShowWindow(hwnd, emFoco ? SW_SHOWNOACTIVATE : SW_HIDE);

            if (emFoco) {
                /* Segue o canto superior esquerdo da janela do jogo */
                RECT r = {};
                GetWindowRect(gameHwnd, &r);
                SetWindowPos(hwnd, nullptr,
                             r.left + 12, r.top + 42,
                             0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }

        /* Quando oculto, aguarda 50 ms e pula o render para poupar CPU */
        if (!emFoco) {
            Sleep(50);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderOverlay();

        ImGui::Render();
        g_context->OMSetRenderTargets(1, &g_mainRTV, nullptr);
        g_context->ClearRenderTargetView(g_mainRTV, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }

    /* ---- Cleanup ---- */
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    LiberarRTV();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context)   { g_context->Release();   g_context   = nullptr; }
    if (g_device)    { g_device->Release();     g_device    = nullptr; }

    DestroyWindow(hwnd);
    UnregisterClassA(CLASS_NAME, wc.hInstance);

    std::printf("[+] Overlay: encerrado\n");
    return 0;
}

// ================================================================
// Overlay_Init — inicia a thread dedicada
// ================================================================

void Overlay_Init(FnExecuteLua executeFn)
{
    g_executeFn = executeFn;

    HANDLE hThread = CreateThread(nullptr, 0, OverlayThread, nullptr, 0, nullptr);
    if (hThread)
        CloseHandle(hThread);
    else
        std::printf("[-] Overlay: falha ao criar thread\n");
}

} // namespace shared
