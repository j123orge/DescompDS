// DescompDS Explorer — Assimp → NeutralMesh adapter
// Loads .dae via Assimp, populates NeutralMesh. No rendering code.
#pragma once

#include "neutral_mesh.h"

// Load a COLLADA .dae file via Assimp into a NeutralMesh.
// dae_path: full path to .dae file
// out: output mesh
// Returns true on success
bool load_dae_to_neutral(const char* dae_path, NeutralMesh& out);
