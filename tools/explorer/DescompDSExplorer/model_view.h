// DescompDS Explorer — 3D model viewer window (hosts the D3D11 renderer)
#pragma once

#include <windows.h>
#include <string>

#include "bmd.h"
#include "neutral_mesh.h"

namespace modelview {

// Open a model viewer window for the given parsed model. Returns immediately.
void show(const bmd::Model& model, const std::string& title);

// Open a model viewer window for a neutral mesh (from OBJ). Returns immediately.
void show_neutral(const NeutralMesh& mesh, const std::string& title);

// True if any viewer window is still open.
bool any_open();

// Close all viewer windows.
void close_all();

} // namespace modelview