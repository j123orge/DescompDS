// DescompDS Explorer — D3D11 model view renderer
// Native Win32 + Direct3D11, no external engine. Renders a bmd::Model as
// wireframe or solid colored geometry with an orbit camera.
#pragma once

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdint>

#include "bmd.h"

namespace d3dview {

// Initialize D3D11 for the given HWND. Returns false on failure.
bool init(HWND hwnd);

// Load a bmd model into the renderer (converts primitives to triangles).
// mode 0 = wireframe, 1 = solid, 2 = solid+wireframe.
bool load_model(const bmd::Model& model, int mode);

// Render one frame (called on WM_PAINT).
void render();

// True if the D3D device was initialized successfully.
bool initialized();

// Camera: frame the current model from its bounding box.
void focus_model();

// Camera controls.
void mouse_drag(int dx, int dy);
void mouse_wheel(int delta);
void reset_camera();

// Debug: read back the current back buffer and save as a 24-bit BMP.
bool save_frame_bmp(const char* path);

// Capture the next rendered frame to the given path (saved before Present).
void set_save_path(const char* path);

void shutdown();

} // namespace d3dview