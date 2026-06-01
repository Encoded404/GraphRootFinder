module;

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>

#include <logging/logging.hpp>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp> // NOLINT(misc-include-cleaner)

module VulkanEngine.Components.LineRenderer;

import VulkanBackend.Component;
import VulkanEngine.Components.DynamicMesh;
import VulkanEngine.MeshManager;
import VulkanEngine.GpuResources.MeshData;
import VulkanEngine.StandardMeshPipeline;
import VulkanEngine.Mesh.MeshTypes;
import VulkanEngine.MaterialManager.MaterialId;

namespace VulkanEngine::Components {

void LineRenderer::Initialize() {
    if (GetOwner() != nullptr) {
        dyn_mesh_ = GetOwner()->GetComponent<DynamicMesh>();
    }
}

void LineRenderer::Setup(MeshManager& mgr, size_t max_segments,
                          MaterialManager::MaterialId material_id) {
    max_segments_ = max_segments;
    material_id_ = material_id;

    if (dyn_mesh_ == nullptr && GetOwner() != nullptr) {
        dyn_mesh_ = GetOwner()->GetComponent<DynamicMesh>();
    }
    if (dyn_mesh_ == nullptr) return;

    const size_t vert_count = max_segments_ * 4;
    const size_t idx_count = max_segments_ * 6;

    GpuResources::MeshData initial;
    initial.vertices.resize(vert_count);
    initial.indices.resize(idx_count);
    initial.sub_meshes.push_back({});
    initial.sub_meshes[0].index_start = 0;
    initial.sub_meshes[0].index_count = static_cast<uint32_t>(idx_count);
    initial.sub_meshes[0].material_id = material_id_;
    initial.sub_meshes[0].sphere = {};
    initial.sub_meshes[0].obb = {};

    dyn_mesh_->SetupStreamed(mgr, initial);
    mesh_dirty = true;
}

void LineRenderer::SetSegments(const std::vector<LineSegment>& segs) {
    segments = segs;
    mesh_dirty = true;
}

void LineRenderer::SetThickness(float t) {
    thickness = t;
    mesh_dirty = true;
}

void LineRenderer::Update(float /*delta_time*/) {
    if (mesh_dirty && dyn_mesh_ != nullptr) {
        RegenerateMesh();
    }
}

void LineRenderer::RegenerateMesh() {
    if (dyn_mesh_ == nullptr) return;

    const size_t active = std::min(segments.size(), max_segments_);
    const size_t vert_count = max_segments_ * 4;
    const size_t idx_count = max_segments_ * 6;

    auto& vertices = dyn_mesh_->mesh_data.vertices;
    auto& indices = dyn_mesh_->mesh_data.indices;
    auto& submeshes = dyn_mesh_->mesh_data.sub_meshes;

    vertices.resize(vert_count);
    indices.resize(idx_count);

    for (size_t i = 0; i < max_segments_; ++i) {
        const size_t vi = i * 4;
        const size_t ii = i * 6;
        const uint32_t b = static_cast<uint32_t>(vi);

        if (i < active) {
            const auto& seg = segments[i];

            // Compute direction and perpendicular
            const glm::vec3 dir_vec = seg.end - seg.start;
            const float len = glm::length(dir_vec);
            if (len < 1e-7f) {
                // Zero-length segment: write a degenerate quad at origin
                vertices[vi + 0] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
                vertices[vi + 1] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f};
                vertices[vi + 2] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
                vertices[vi + 3] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f};

                // Regular triangle connectivity (zero-area, will be discarded)
                indices[ii + 0] = b + 0;
                indices[ii + 1] = b + 1;
                indices[ii + 2] = b + 2;
                indices[ii + 3] = b + 2;
                indices[ii + 4] = b + 1;
                indices[ii + 5] = b + 3;
                continue;
            }

            const glm::vec3 dir = dir_vec / len;

            glm::vec3 perp;
            if (camera_ != nullptr) {
                const glm::vec3 view_dir = glm::normalize(camera_->target - camera_->position);
                const float dv_dot = glm::dot(dir, view_dir);
                if (std::abs(dv_dot) > 0.999f) {
                    perp = glm::normalize(glm::cross(dir, camera_->up));
                } else {
                    perp = glm::normalize(glm::cross(dir, view_dir));
                }
            } else {
                if (std::abs(dir.y) < 0.9f) {
                    perp = glm::normalize(glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f)));
                } else {
                    perp = glm::normalize(glm::cross(dir, glm::vec3(1.0f, 0.0f, 0.0f)));
                }
            }
            perp *= thickness * 0.5f;

            const glm::vec3 normal = glm::normalize(glm::cross(dir, perp));

            const auto& s = seg.start;
            const auto& e = seg.end;

            // Quad vertices (flat struct: px,py,pz, nx,ny,nz, u,v)
            vertices[vi + 0] = {s.x - perp.x, s.y - perp.y, s.z - perp.z,
                                normal.x, normal.y, normal.z,
                                0.0f, 0.0f};
            vertices[vi + 1] = {s.x + perp.x, s.y + perp.y, s.z + perp.z,
                                normal.x, normal.y, normal.z,
                                1.0f, 0.0f};
            vertices[vi + 2] = {e.x - perp.x, e.y - perp.y, e.z - perp.z,
                                normal.x, normal.y, normal.z,
                                0.0f, 1.0f};
            vertices[vi + 3] = {e.x + perp.x, e.y + perp.y, e.z + perp.z,
                                normal.x, normal.y, normal.z,
                                1.0f, 1.0f};

            indices[ii + 0] = b + 0;
            indices[ii + 1] = b + 1;
            indices[ii + 2] = b + 2;
            indices[ii + 3] = b + 2;
            indices[ii + 4] = b + 1;
            indices[ii + 5] = b + 3;
        } else {
            // Unused segment: all vertices at origin → degenerate zero-area triangles
            vertices[vi + 0] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
            vertices[vi + 1] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f};
            vertices[vi + 2] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
            vertices[vi + 3] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f};

            indices[ii + 0] = b + 0;
            indices[ii + 1] = b + 1;
            indices[ii + 2] = b + 2;
            indices[ii + 3] = b + 2;
            indices[ii + 4] = b + 1;
            indices[ii + 5] = b + 3;
        }
    }

    // Index count stays fixed at max (set during Setup).
    // Degenerate trailing quads are zero-area and will be discarded by the rasterizer.
    if (submeshes.empty()) {
        submeshes.push_back({});
        submeshes[0].index_start = 0;
        submeshes[0].index_count = static_cast<uint32_t>(idx_count);
        submeshes[0].material_id = material_id_;
    }

    LOGIFACE_LOG(trace, "updated line with index count of: " + std::to_string(static_cast<uint32_t>(idx_count)));

    dyn_mesh_->submesh_count = static_cast<uint32_t>(submeshes.size());
    dyn_mesh_->first_submesh = 0;
    mesh_dirty = false;
}

} // namespace VulkanEngine::Components
