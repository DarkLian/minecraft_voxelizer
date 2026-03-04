#include "GreedyMesher.hpp"
#include <iostream>
#include <vector>

GreedyMesher::GreedyMesher(Config cfg) : cfg_(cfg) {
}

// ─────────────────────────────────────────────────────────────────────────────
// mesh — 3-D greedy volume boxing.
//
// Algorithm:
//   Iterate every solid voxel in X-major, Y-minor, Z-minor order.
//   For each unconsumed solid voxel at (x0, y0, z0):
//     1. Expand X: grow x1 while (x1, y0, z0) is solid and unconsumed.
//     2. Expand Y: grow y1 while the entire row [x0,x1) × {y1} × {z0}
//                  is solid and unconsumed.
//     3. Expand Z: grow z1 while the entire slab [x0,x1) × [y0,y1) × {z1}
//                  is solid and unconsumed.
//     4. Mark all voxels in the box [x0,x1) × [y0,y1) × [z0,z1) consumed.
//     5. For each of 6 face directions, check whether the face has at least
//        one exposed voxel. If so, emit one Quad with:
//          • from/to = full 3-D box MC extents  (new: enables AABB packing)
//          • sweepLayer = outermost voxel layer  (unchanged: used by samplePixel)
//          • uStart/vStart/uCount/vCount         (unchanged: used by samplePixel)
//
// Interior boxes (surrounded on all sides by other solid boxes) emit zero
// quads and therefore consume zero atlas pixels and produce zero elements.
//
// The three greedy expansion passes are not globally optimal, but they are
// O(N) in the number of solid voxels and produce far fewer boxes than the
// v1.x face-sweep for solid-filled models.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<GreedyMesher::Quad> GreedyMesher::mesh(const VoxelGrid &grid) const {
    const int resX = grid.resX;
    const int resY = grid.resY;
    const int resZ = grid.resZ;

    const float vsX = 16.0f / static_cast<float>(resX);
    const float vsY = 16.0f / static_cast<float>(resY);
    const float vsZ = 16.0f / static_cast<float>(resZ);

    // Flat consumed array — same layout as VoxelGrid::idx (x + y*resX + z*resX*resY)
    std::vector<uint8_t> consumed(static_cast<size_t>(resX) * resY * resZ, 0);
    auto cidx = [&](int x, int y, int z) -> size_t {
        return static_cast<size_t>(x + y * resX + z * resX * resY);
    };

    std::vector<Quad> quads;
    quads.reserve(4096);

    int boxCount = 0;
    int skippedBoxes = 0; // boxes with zero exposed faces (pure interior)

    for (int z0 = 0; z0 < resZ; z0++) {
        for (int y0 = 0; y0 < resY; y0++) {
            for (int x0 = 0; x0 < resX; x0++) {
                if (!grid.isSolid(x0, y0, z0) || consumed[cidx(x0, y0, z0)])
                    continue;

                // ── Step 1: expand X ──────────────────────────────────────
                int x1 = x0 + 1;
                while (x1 < resX &&
                       grid.isSolid(x1, y0, z0) &&
                       !consumed[cidx(x1, y0, z0)])
                    x1++;

                // ── Step 2: expand Y ──────────────────────────────────────
                int y1 = y0 + 1;
                while (y1 < resY) {
                    bool rowOk = true;
                    for (int x = x0; x < x1 && rowOk; x++)
                        if (!grid.isSolid(x, y1, z0) || consumed[cidx(x, y1, z0)])
                            rowOk = false;
                    if (!rowOk) break;
                    y1++;
                }

                // ── Step 3: expand Z ──────────────────────────────────────
                int z1 = z0 + 1;
                while (z1 < resZ) {
                    bool slabOk = true;
                    for (int y = y0; y < y1 && slabOk; y++)
                        for (int x = x0; x < x1 && slabOk; x++)
                            if (!grid.isSolid(x, y, z1) || consumed[cidx(x, y, z1)])
                                slabOk = false;
                    if (!slabOk) break;
                    z1++;
                }

                // ── Step 4: mark box consumed ─────────────────────────────
                for (int z = z0; z < z1; z++)
                    for (int y = y0; y < y1; y++)
                        for (int x = x0; x < x1; x++)
                            consumed[cidx(x, y, z)] = 1;

                boxCount++;

                // ── Step 5: emit quads for exposed faces ──────────────────
                //
                // MC-space full box extents (shared by all faces of this box)
                const glm::vec3 boxFrom(x0 * vsX, y0 * vsY, z0 * vsZ);
                const glm::vec3 boxTo(x1 * vsX, y1 * vsY, z1 * vsZ);

                int quadsEmitted = 0;

                // Helper: emit one quad if the face has at least one exposed voxel.
                // sweepLayer is the outermost voxel index for each face direction —
                // the layer that samplePixel reads triangle data from.
                auto tryEmit = [&](Face face,
                                   int sweepAxis, int uAxis, int vAxis,
                                   int sweepLayer,
                                   int uStart, int vStart,
                                   int uCount, int vCount) {
                    // Scan the face surface for at least one exposed voxel.
                    // For large boxes this is O(face area) but done once at
                    // build time — negligible vs. voxelization cost.
                    bool hasExposed = false;
                    for (int vi = vStart; vi < vStart + vCount && !hasExposed; vi++) {
                        for (int ui = uStart; ui < uStart + uCount && !hasExposed; ui++) {
                            glm::ivec3 pos;
                            pos[sweepAxis] = sweepLayer;
                            pos[uAxis] = ui;
                            pos[vAxis] = vi;
                            if (grid.isFaceExposed(pos.x, pos.y, pos.z, face))
                                hasExposed = true;
                        }
                    }
                    if (!hasExposed) return;

                    Quad q{};
                    q.from = boxFrom;
                    q.to = boxTo;
                    q.face = face;
                    q.sweepAxis = sweepAxis;
                    q.uAxis = uAxis;
                    q.vAxis = vAxis;
                    q.sweepLayer = sweepLayer;
                    q.uStart = uStart;
                    q.vStart = vStart;
                    q.uCount = uCount;
                    q.vCount = vCount;
                    quads.push_back(q);
                    quadsEmitted++;
                };

                // Face axes mirror the v1.x convention exactly so samplePixel works.
                //
                //   Down  (-Y): sweepAxis=Y, surface layer y0,   u=X, v=Z
                //   Up    (+Y): sweepAxis=Y, surface layer y1-1, u=X, v=Z
                //   North (-Z): sweepAxis=Z, surface layer z0,   u=X, v=Y
                //   South (+Z): sweepAxis=Z, surface layer z1-1, u=X, v=Y
                //   West  (-X): sweepAxis=X, surface layer x0,   u=Z, v=Y
                //   East  (+X): sweepAxis=X, surface layer x1-1, u=Z, v=Y

                // Down  (-Y)
                tryEmit(Face::Down, 1, 0, 2, y0, x0, z0, x1 - x0, z1 - z0);
                // Up    (+Y)
                tryEmit(Face::Up, 1, 0, 2, y1 - 1, x0, z0, x1 - x0, z1 - z0);
                // North (-Z)
                tryEmit(Face::North, 2, 0, 1, z0, x0, y0, x1 - x0, y1 - y0);
                // South (+Z)
                tryEmit(Face::South, 2, 0, 1, z1 - 1, x0, y0, x1 - x0, y1 - y0);
                // West  (-X)
                tryEmit(Face::West, 0, 2, 1, x0, z0, y0, z1 - z0, y1 - y0);
                // East  (+X)
                tryEmit(Face::East, 0, 2, 1, x1 - 1, z0, y0, z1 - z0, y1 - y0);

                if (quadsEmitted == 0)
                    skippedBoxes++;
            }
        }
    }

    if (cfg_.verbose) {
        std::cout << "[GreedyMesher] 3-D boxes found: " << boxCount
                << "  (interior/skipped: " << skippedBoxes << ")\n"
                << "[GreedyMesher] Exposed-face quads: " << quads.size()
                << "  -> after AABB packing = " << (boxCount - skippedBoxes)
                << " elements  (less = better)\n";
    }

    return quads;
}
