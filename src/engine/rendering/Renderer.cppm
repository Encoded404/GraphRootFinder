module;

#include <cstdint>
#include <memory>

#include <glm/glm.hpp> //NOLINT(misc-include-cleaner)
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

export module VulkanEngine.Renderer;

export import VulkanBackend.Runtime.VulkanBootstrap;
export import VulkanBackend.Component;
export import VulkanEngine.RenderPipeline;
export import VulkanEngine.StandardMeshPipeline;
export import VulkanEngine.SceneRenderer;
export import VulkanEngine.TechniqueManager;
export import VulkanEngine.BindlessManager;
export import VulkanEngine.Components.Camera;
export import VulkanEngine.GpuResources;
export import VulkanEngine.ImGui;

export namespace VulkanEngine::Renderer {

struct GridParams {
    glm::vec2 offset{0.0f, 0.0f};
    glm::vec2 screen_size{0.0f, 0.0f};
    float zoom = 50.0f;
    float spacing = 1.0f;
    float line_thickness = 1.0f;
    float axis_thickness = 2.0f;
    glm::vec4 bg_color{0.10f, 0.10f, 0.12f, 1.0f};
    glm::vec4 grid_color{0.20f, 0.20f, 0.22f, 1.0f};
    glm::vec4 axis_color{0.45f, 0.45f, 0.55f, 1.0f};
};

struct RendererConfig {
    bool enable_imgui = true;
    glm::vec4 clear_color{0.1f, 0.1f, 0.1f, 1.0f};
    vk::ClearDepthStencilValue clear_depth_stencil{1.0f, 0};
    std::span<const std::uint32_t> grid_vert_spv{};
    std::span<const std::uint32_t> grid_frag_spv{};
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Initialize(VulkanEngine::Runtime::VulkanBootstrap& bootstrap,
                    const RendererConfig& config);

    void Shutdown();

    void SetGridParams(const GridParams& params) { grid_params_ = params; }

    void RenderFrame(VulkanEngine::Runtime::VulkanBootstrap& bootstrap,
                     VulkanEngine::ComponentRegistry& registry,
                     const VulkanEngine::Components::Camera& camera,
                     VulkanEngine::TechniqueManager::TechniqueManager& technique_mgr,
                     VulkanEngine::BindlessManager::BindlessManager& bindless_mgr,
                     VulkanEngine::SceneRenderer::SceneRenderer& scene_renderer,
                     VulkanEngine::ImGui::ImGuiSystem* imgui,
                     uint32_t image_index);

private:
    VulkanEngine::Runtime::VulkanBootstrap* bootstrap_ = nullptr;
    std::unique_ptr<VulkanEngine::RenderPipeline::RenderPipeline> pipeline_{};

    VulkanEngine::ComponentRegistry* current_registry_ = nullptr;
    const VulkanEngine::Components::Camera* current_camera_ = nullptr;
    VulkanEngine::TechniqueManager::TechniqueManager* current_technique_mgr_ = nullptr;
    VulkanEngine::BindlessManager::BindlessManager* current_bindless_mgr_ = nullptr;
    VulkanEngine::SceneRenderer::SceneRenderer* current_scene_renderer_ = nullptr;
    VulkanEngine::ImGui::ImGuiSystem* current_imgui_ = nullptr;
    vk::ClearDepthStencilValue clear_depth_stencil_{1.0f, 0};
    uint32_t current_width_ = 0;
    uint32_t current_height_ = 0;
    uint32_t current_image_index_ = 0;
    glm::mat4 current_view_proj_{1.0f};
    uint32_t frame_counter_ = 0;
    uint32_t last_swapchain_image_count_ = 0;


    bool grid_pass_enabled_ = false;
    std::unique_ptr<vk::raii::PipelineLayout> grid_pipeline_layout_{};
    std::unique_ptr<vk::raii::Pipeline> grid_pipeline_{};
    GridParams grid_params_{};

    static constexpr vk::QueryPipelineStatisticFlags GPU_STATS_FLAGS =
        vk::QueryPipelineStatisticFlagBits::eInputAssemblyVertices |
        vk::QueryPipelineStatisticFlagBits::eInputAssemblyPrimitives |
        vk::QueryPipelineStatisticFlagBits::eVertexShaderInvocations |
        vk::QueryPipelineStatisticFlagBits::eClippingInvocations |
        vk::QueryPipelineStatisticFlagBits::eClippingPrimitives |
        vk::QueryPipelineStatisticFlagBits::eFragmentShaderInvocations |
        vk::QueryPipelineStatisticFlagBits::eComputeShaderInvocations;
    std::unique_ptr<vk::raii::QueryPool> gpu_stats_pool_{};
};

} // namespace VulkanEngine::Renderer
