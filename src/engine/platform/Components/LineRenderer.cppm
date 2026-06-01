module;

#include <vector>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp> // NOLINT(misc-include-cleaner)

export module VulkanEngine.Components.LineRenderer;

import VulkanBackend.Component;
import VulkanEngine.Components.Camera;
import VulkanEngine.Components.DynamicMesh;
import VulkanEngine.MeshManager;
import VulkanEngine.GpuResources.MeshData;
import VulkanEngine.Mesh.MeshTypes;
import VulkanEngine.MaterialManager.MaterialId;

export namespace VulkanEngine::Components {

struct LineSegment {
    glm::vec3 start; // NOLINT(misc-non-private-member-variables-in-classes)
    glm::vec3 end;   // NOLINT(misc-non-private-member-variables-in-classes)
};

class LineRenderer : public VulkanEngine::Component {
public:
    // --- Configuration ---
    std::vector<LineSegment> segments; // NOLINT(misc-non-private-member-variables-in-classes)
    float thickness = 0.01f;           // NOLINT(misc-non-private-member-variables-in-classes)
    bool mesh_dirty = true;            // NOLINT(misc-non-private-member-variables-in-classes)

    // --- Lifecycle ---
    void Initialize() override;
    void Update(float delta_time) override;

    // --- API ---
    void Setup(MeshManager& mgr, size_t max_segments,
               MaterialManager::MaterialId material_id = {});
    void SetCamera(Camera* cam) { camera_ = cam; }
    void SetSegments(const std::vector<LineSegment>& segs);
    void SetThickness(float t);

private:
    void RegenerateMesh();

    DynamicMesh* dyn_mesh_ = nullptr;
    Camera* camera_ = nullptr;
    size_t max_segments_ = 128;
    MaterialManager::MaterialId material_id_{0};
};

} // namespace VulkanEngine::Components
