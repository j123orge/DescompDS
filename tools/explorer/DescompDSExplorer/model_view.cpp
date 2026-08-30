// DescompDS Explorer — 3D model viewer window (hosts the D3D11 renderer)
#define NOMINMAX
#include "model_view.h"
#include "d3d_view.h"
#include <windowsx.h>

#include <vector>

namespace modelview {

namespace {

struct Viewer {
    HWND hwnd = nullptr;
    bmd::Model model;
    bool dragging = false;
    int lastx = 0, lasty = 0;
};

std::vector<Viewer*> g_viewers;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Viewer* v = (Viewer*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CREATE: {
            v = (Viewer*)((CREATESTRUCT*)lp)->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v);
            if (d3dview::init(hwnd)) {
                d3dview::load_model(v->model, 1);
                d3dview::reset_camera();
            }
            return 0;
        }
        case WM_SIZE: {
            if (v && d3dview::initialized()) {
                d3dview::shutdown();
                if (d3dview::init(hwnd)) d3dview::load_model(v->model, 1);
            }
            return 0;
        }
        case WM_PAINT: {
            d3dview::render();
            ValidateRect(hwnd, nullptr);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            v->dragging = true; v->lastx = GET_X_LPARAM(lp); v->lasty = GET_Y_LPARAM(lp);
            SetCapture(hwnd); return 0;
        }
        case WM_LBUTTONUP: {
            v->dragging = false; ReleaseCapture(); return 0;
        }
        case WM_MOUSEMOVE: {
            if (v->dragging) {
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                d3dview::mouse_drag(x - v->lastx, y - v->lasty);
                v->lastx = x; v->lasty = y;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            d3dview::mouse_wheel(GET_WHEEL_DELTA_WPARAM(wp));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN: {
            if (wp == 'R' || wp == 'r') { d3dview::reset_camera(); d3dview::focus_model(); }
            if (wp == 'F' || wp == 'f') d3dview::focus_model();
            if (wp == 'W' || wp == 'w') d3dview::load_model(v->model, 0);
            if (wp == 'S' || wp == 's') d3dview::load_model(v->model, 1);
            if (wp == 'B' || wp == 'b') d3dview::load_model(v->model, 2);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DESTROY: {
            for (size_t i = 0; i < g_viewers.size(); i++)
                if (g_viewers[i]->hwnd == hwnd) { delete g_viewers[i]; g_viewers.erase(g_viewers.begin() + i); break; }
            d3dview::shutdown();
            return 0;
        }
        default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace

void show(const bmd::Model& model, const std::string& title) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DescompDSModelView";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    Viewer* v = new Viewer();
    v->model = model;
    g_viewers.push_back(v);

    std::wstring wtitle(title.begin(), title.end());
    v->hwnd = CreateWindowExW(0, L"DescompDSModelView", wtitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 100, 900, 700,
        nullptr, nullptr, wc.hInstance, v);
    ShowWindow(v->hwnd, SW_SHOW);
    UpdateWindow(v->hwnd);
}

bool any_open() { return !g_viewers.empty(); }

void close_all() {
    while (!g_viewers.empty()) {
        HWND h = g_viewers.back()->hwnd;
        DestroyWindow(h);
    }
}

} // namespace modelview