module;

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp> //NOLINT(misc-include-cleaner)
#include <glm/gtc/matrix_transform.hpp> //NOLINT(misc-include-cleaner)
#include <cstdint>
#include <array>
#include <memory>

#include <vulkan/vulkan.hpp>
#include <logging/logging.hpp>

module VulkanEngine.Renderer;

import VulkanBackend.Runtime.VulkanBootstrap;
import VulkanBackend.Utils.VulkanDebugUtils;
import VulkanBackend.RenderGraph;
import VulkanEngine.RenderPipeline;
import VulkanEngine.SceneRenderer;
import VulkanEngine.TechniqueManager;
import VulkanEngine.BindlessManager;
import VulkanEngine.Components.Camera;
import VulkanEngine.GpuResources;
import VulkanEngine.ImGui;

namespace VulkanEngine::Renderer {

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Initialize(VulkanEngine::Runtime::VulkanBootstrap& bootstrap,
                                  const RendererConfig& config) {
    bootstrap_ = &bootstrap;

    pipeline_ = std::make_unique<VulkanEngine::RenderPipeline::RenderPipeline>();
    pipeline_->Initialize(bootstrap);

    auto backbuffer = pipeline_->ImportBackbuffer();
    auto depth_buffer = pipeline_->ImportDepthBuffer();
    auto hiz_image = pipeline_->ImportImage("hiz-image");
    auto scene_buffers = pipeline_->ImportBuffer("scene-buffers");
    auto draw_indirect = pipeline_->ImportBuffer("draw-indirect");

    pipeline_->RegisterResourceResolver("hiz-image",
        [this](uint32_t) { return current_scene_renderer_ ? current_scene_renderer_->GetHizImage(frame_counter_) : VK_NULL_HANDLE; },
        [this](uint32_t) { return current_scene_renderer_ ? current_scene_renderer_->GetHizFullView(frame_counter_) : VK_NULL_HANDLE; },
        vk::Format::eR32Sfloat);

    pipeline_->SetFinalState(
        backbuffer,
        VulkanEngine::RenderGraph::ResourceState::ImageState(
            VulkanEngine::RenderGraph::PipelineStageIntent::BottomOfPipe,
            VulkanEngine::RenderGraph::AccessIntent::None,
            VulkanEngine::RenderGraph::QueueType::Graphics,
            VulkanEngine::RenderGraph::ImageLayoutIntent::Present));

    // ── Grid background pipeline ──
    if (!config.grid_vert_spv.empty() && !config.grid_frag_spv.empty()) {
        auto& device = bootstrap.GetBackend().GetDevice();

        const vk::ShaderModuleCreateInfo vert_info({}, config.grid_vert_spv.size() * sizeof(uint32_t), config.grid_vert_spv.data());
        const vk::raii::ShaderModule vert_module(device, vert_info);
        const vk::ShaderModuleCreateInfo frag_info({}, config.grid_frag_spv.size() * sizeof(uint32_t), config.grid_frag_spv.data());
        const vk::raii::ShaderModule frag_module(device, frag_info);

        std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
            vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vert_module, "main"),
            vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *frag_module, "main")
        };

        constexpr uint32_t push_size = sizeof(GridParams);
        const vk::PushConstantRange push_range(vk::ShaderStageFlagBits::eFragment, 0, push_size);

        vk::PipelineLayoutCreateInfo layout_info{};
        layout_info.setLayoutCount = 0;
        layout_info.pSetLayouts = nullptr;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;
        grid_pipeline_layout_ = std::make_unique<vk::raii::PipelineLayout>(device, layout_info);
        VulkanEngine::Utils::SetVulkanObjectName(device, *grid_pipeline_layout_, "graph-grid-pipeline-layout");

        const vk::PipelineVertexInputStateCreateInfo vertex_input({}, 0, nullptr, 0, nullptr);
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly({}, vk::PrimitiveTopology::eTriangleList);
        constexpr vk::PipelineViewportStateCreateInfo viewport_state({}, 1, nullptr, 1, nullptr);
        const vk::PipelineRasterizationStateCreateInfo rasterization({}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f);
        const vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);
        const vk::PipelineDepthStencilStateCreateInfo depth_stencil({}, false, false, vk::CompareOp::eAlways);

        const vk::PipelineColorBlendAttachmentState color_attach(
            false,
            vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
            vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
        const vk::PipelineColorBlendStateCreateInfo color_blend({}, false, vk::LogicOp::eCopy, color_attach);

        const std::array<vk::DynamicState, 2> dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state({}, dynamic_states);

        const vk::Format surface_format = bootstrap.GetBackend().GetSurfaceFormat().format;
        vk::PipelineRenderingCreateInfo rendering_info{};
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachmentFormats = &surface_format;

        vk::GraphicsPipelineCreateInfo pipeline_info({}, stages, &vertex_input, &input_assembly, nullptr, &viewport_state, &rasterization, &multisample, &depth_stencil, &color_blend, &dynamic_state, *grid_pipeline_layout_);
        pipeline_info.setPNext(&rendering_info);

        grid_pipeline_ = std::make_unique<vk::raii::Pipeline>(device, nullptr, pipeline_info);
        VulkanEngine::Utils::SetVulkanObjectName(device, *grid_pipeline_, "graph-grid-pipeline");
        grid_pass_enabled_ = true;
    }

    // ── Pass 1: Graph background (full-screen grid) ──
    RenderGraph::PassHandle grid_pass;
    if (grid_pass_enabled_) {
        RenderGraph::PassAttachmentSetup grid_setup{};
        grid_setup.auto_begin_rendering = true;

        RenderGraph::AttachmentInfo grid_color{};
        grid_color.resource = backbuffer;
        grid_color.load_op = vk::AttachmentLoadOp::eClear;
        grid_color.store_op = vk::AttachmentStoreOp::eStore;
        grid_color.clear_color = vk::ClearColorValue(std::array<float, 4>{0.10f, 0.10f, 0.12f, 1.0f});
        grid_setup.color_attachments.push_back(grid_color);

        grid_pass = pipeline_->AddPass({
            .name = "graph-grid",
            .queue = RenderGraph::QueueType::Graphics,
            .writes = {backbuffer},
            .attachments = grid_setup,
            .execute = [this](const void*, vk::CommandBuffer cmd) {
                if (!grid_pipeline_) return;
                cmd.setViewport(0, vk::Viewport(0.0f, 0.0f,
                    static_cast<float>(current_width_),
                    static_cast<float>(current_height_), 0.0f, 1.0f));
                cmd.setScissor(0, vk::Rect2D({0, 0},
                    {current_width_, current_height_}));

                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **grid_pipeline_);

                GridParams params = grid_params_;
                params.screen_size = glm::vec2(static_cast<float>(current_width_), static_cast<float>(current_height_));
                cmd.pushConstants(*grid_pipeline_layout_, vk::ShaderStageFlagBits::eFragment, 0, sizeof(GridParams), &params);
                cmd.draw(3, 1, 0, 0);
            }
        });
    }

    // ── Pass 2: Expand (compute) ──
    auto expand_pass = pipeline_->AddPass({
        .name = "expand",
        .queue = VulkanEngine::RenderGraph::QueueType::Graphics,
        .writes = {scene_buffers, draw_indirect},
        .execute = [this](const void*, vk::CommandBuffer cmd) {
            if (current_scene_renderer_) {
                const uint32_t cnt = current_scene_renderer_->GetCurrentEntityCount();
                if (cnt) {
                    current_scene_renderer_->DispatchExpand(cmd, cnt,
                        current_view_proj_, frame_counter_);
                }
            }
        }
    });

    // ── Pass 2: Depth pre-pass ──
    VulkanEngine::RenderGraph::PassAttachmentSetup depth_setup{};
    depth_setup.auto_begin_rendering = true;

    VulkanEngine::RenderGraph::AttachmentInfo depth_attach{};
    depth_attach.resource = depth_buffer;
    depth_attach.load_op = vk::AttachmentLoadOp::eClear;
    depth_attach.store_op = vk::AttachmentStoreOp::eStore;
    depth_attach.clear_depth = config.clear_depth_stencil;
    depth_setup.depth_attachment = depth_attach;

    auto depth_pass = pipeline_->AddPass({
        .name = "depth-prepass",
        .queue = VulkanEngine::RenderGraph::QueueType::Graphics,
        .reads = {{scene_buffers,
            VulkanEngine::RenderGraph::PipelineStageIntent::VertexShader,
            VulkanEngine::RenderGraph::AccessIntent::Read},
            {draw_indirect,
            VulkanEngine::RenderGraph::PipelineStageIntent::IndirectDraw,
            VulkanEngine::RenderGraph::AccessIntent::Read}},
        .writes = {depth_buffer},
        .attachments = depth_setup,
        .execute = [this](const void*, vk::CommandBuffer cmd) {
            if (current_scene_renderer_) {
                current_scene_renderer_->DepthPrepass(cmd, current_width_, current_height_,
                                                     frame_counter_);
            }
        }
    });

    // ── Pass 3: Hi-Z generation compute ──
    auto hiz_pass = pipeline_->AddPass({
        .name = "hiz-gen",
        .queue = VulkanEngine::RenderGraph::QueueType::Graphics,
        .reads = {{depth_buffer,
            VulkanEngine::RenderGraph::PipelineStageIntent::ComputeShader,
            VulkanEngine::RenderGraph::AccessIntent::Read}},
        .writes = {hiz_image},
        .execute = [this](const void*, vk::CommandBuffer cmd) {
            if (current_scene_renderer_) {
                current_scene_renderer_->DispatchHiZGen(cmd, current_width_, current_height_,
                                                        frame_counter_, current_image_index_);
            }
        }
    });

    // ── Pass 4: Occlusion cull compute ──
    auto occlusion_pass = pipeline_->AddPass({
        .name = "occlusion",
        .queue = VulkanEngine::RenderGraph::QueueType::Graphics,
        .reads = {{hiz_image,
            VulkanEngine::RenderGraph::PipelineStageIntent::ComputeShader,
            VulkanEngine::RenderGraph::AccessIntent::Read},
            {scene_buffers,
            VulkanEngine::RenderGraph::PipelineStageIntent::ComputeShader,
            VulkanEngine::RenderGraph::AccessIntent::Read}},
        .writes = {scene_buffers},
        .execute = [this](const void*, vk::CommandBuffer cmd) {
            if (current_scene_renderer_) {
                current_scene_renderer_->DispatchOcclusion(cmd, frame_counter_);
            }
        }
    });

    // ── Pass 5: Collect compute (count + compact + draw commands) ──
    auto collect_pass = pipeline_->AddPass({
        .name = "collect",
        .queue = VulkanEngine::RenderGraph::QueueType::Graphics,
        .reads = {{scene_buffers,
            VulkanEngine::RenderGraph::PipelineStageIntent::ComputeShader,
            VulkanEngine::RenderGraph::AccessIntent::Read}},
        .writes = {scene_buffers, draw_indirect},
        .execute = [this](const void*, vk::CommandBuffer cmd) {
            if (current_scene_renderer_) {
                current_scene_renderer_->DispatchCollect(cmd, frame_counter_);
            }
        }
    });

    // ── Pass 6: Main pass (opaque) ──
    VulkanEngine::RenderGraph::PassAttachmentSetup main_setup{};
    main_setup.auto_begin_rendering = true;

    VulkanEngine::RenderGraph::AttachmentInfo color_attach{};
    color_attach.resource = backbuffer;
    color_attach.load_op = grid_pass_enabled_ ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear;
    color_attach.store_op = vk::AttachmentStoreOp::eStore;
    if (!grid_pass_enabled_) {
        color_attach.clear_color = vk::ClearColorValue(std::array<float, 4>{
            config.clear_color.r, config.clear_color.g, config.clear_color.b, config.clear_color.a});
    }
    main_setup.color_attachments.push_back(color_attach);

    VulkanEngine::RenderGraph::AttachmentInfo main_depth_attach{};
    main_depth_attach.resource = depth_buffer;
    main_depth_attach.load_op = vk::AttachmentLoadOp::eLoad;
    main_depth_attach.store_op = vk::AttachmentStoreOp::eStore;
    main_setup.depth_attachment = main_depth_attach;

    auto main_pass = pipeline_->AddPass({
        .name = "main-pass",
        .queue = VulkanEngine::RenderGraph::QueueType::Graphics,
        .reads = {{scene_buffers,
            VulkanEngine::RenderGraph::PipelineStageIntent::VertexShader,
            VulkanEngine::RenderGraph::AccessIntent::Read},
            {draw_indirect,
            VulkanEngine::RenderGraph::PipelineStageIntent::IndirectDraw,
            VulkanEngine::RenderGraph::AccessIntent::Read}},
        .writes = {backbuffer, depth_buffer},
        .attachments = main_setup,
        .execute = [this](const void*, vk::CommandBuffer cmd) {
            if (current_scene_renderer_) {
                const float aspect = static_cast<float>(current_width_) / static_cast<float>(current_height_);
                const glm::mat4 view = current_camera_->GetViewMatrix();
                const glm::mat4 proj = current_camera_->GetProjectionMatrix(aspect);

                current_scene_renderer_->Render(cmd, *current_registry_,
                                                *current_technique_mgr_, *current_bindless_mgr_,
                                                proj, view,
                                                current_width_, current_height_,
                                                frame_counter_);
            }
        }
    });

    // ── Pass 7: ImGui overlay ──
    if (config.enable_imgui) {
        VulkanEngine::RenderGraph::PassAttachmentSetup imgui_setup{};
        imgui_setup.auto_begin_rendering = false;

        VulkanEngine::RenderGraph::AttachmentInfo imgui_color_attach{};
        imgui_color_attach.resource = backbuffer;
        imgui_color_attach.load_op = vk::AttachmentLoadOp::eLoad;
        imgui_color_attach.store_op = vk::AttachmentStoreOp::eStore;
        imgui_setup.color_attachments.push_back(imgui_color_attach);

        pipeline_->AddPass({
            .name = "imgui-overlay",
            .queue = VulkanEngine::RenderGraph::QueueType::Graphics,
            .writes = {backbuffer},
            .attachments = imgui_setup,
            .execute = [this](const void*, vk::CommandBuffer cmd) {
                if (current_imgui_ && current_imgui_->IsInitialized()) {
                    auto& backend = bootstrap_->GetBackend();
                    current_imgui_->RenderDrawData(cmd,
                        *backend.GetSwapchainImageViews()[current_image_index_],
                        current_width_, current_height_);
                }
            }
        });
    }

    // Explicit ordering ensures correct pipeline
    if (grid_pass_enabled_) {
        pipeline_->AddDependency(grid_pass, expand_pass);
    }
    pipeline_->AddDependency(expand_pass, depth_pass);
    pipeline_->AddDependency(depth_pass, hiz_pass);
    pipeline_->AddDependency(hiz_pass, occlusion_pass);
    pipeline_->AddDependency(occlusion_pass, collect_pass);
    pipeline_->AddDependency(collect_pass, main_pass);

    pipeline_->Compile();
    if (!pipeline_->IsCompiled()) return false;

    clear_depth_stencil_ = config.clear_depth_stencil;

    {
        auto& device = bootstrap.GetBackend().GetDevice();
        vk::QueryPoolCreateInfo qp_info{};
        qp_info.queryType = vk::QueryType::ePipelineStatistics;
        qp_info.pipelineStatistics = GPU_STATS_FLAGS;
        qp_info.queryCount = 1;
        gpu_stats_pool_ = std::make_unique<vk::raii::QueryPool>(device, qp_info);
        VulkanEngine::Utils::SetVulkanObjectName(device, *gpu_stats_pool_, "gpu-stats-pool");
        vkResetQueryPool(*device, **gpu_stats_pool_, 0, 1);
    }

    LOGIFACE_LOG(info, "Renderer initialized with full render-graph pipeline");
    return true;
}

void Renderer::Shutdown() {
    if (bootstrap_) {
        try {
            bootstrap_->GetBackend().GetDevice().waitIdle();
        } catch (const std::exception& err) {
            LOGIFACE_LOG(error, "Error during Renderer shutdown: " + std::string(err.what()));
        }
    }
    grid_pipeline_.reset();
    grid_pipeline_layout_.reset();
    gpu_stats_pool_.reset();
    if (pipeline_) {
        pipeline_->Shutdown();
        pipeline_.reset();
    }
    bootstrap_ = nullptr;
}

void Renderer::RenderFrame(VulkanEngine::Runtime::VulkanBootstrap& bootstrap,
                                   VulkanEngine::ComponentRegistry& registry,
                                   const VulkanEngine::Components::Camera& camera,
                                   VulkanEngine::TechniqueManager::TechniqueManager& technique_mgr,
                                   VulkanEngine::BindlessManager::BindlessManager& bindless_mgr,
                                   VulkanEngine::SceneRenderer::SceneRenderer& scene_renderer,
                                   VulkanEngine::ImGui::ImGuiSystem* imgui,
                                   uint32_t image_index) {
    if (!pipeline_ || !pipeline_->IsCompiled()) return;

    current_registry_ = &registry;
    current_camera_ = &camera;
    current_technique_mgr_ = &technique_mgr;
    current_bindless_mgr_ = &bindless_mgr;
    current_scene_renderer_ = &scene_renderer;
    current_imgui_ = imgui;
    current_image_index_ = image_index;

    (void)bootstrap.GetBackend().GetSwapchainExtent(current_width_, current_height_);

    LOGIFACE_LOG(trace, "RenderFrame frame=" + std::to_string(frame_counter_) +
                 " img=" + std::to_string(image_index) + " w=" + std::to_string(current_width_) +
                 " h=" + std::to_string(current_height_));

    if (imgui && imgui->IsInitialized()) {
        imgui->NewFrame();
    }

    auto& backend = bootstrap.GetBackend();
    const uint32_t sc_count = bootstrap.GetSnapshot().swapchain_image_count;
    if (sc_count != last_swapchain_image_count_) {
        last_swapchain_image_count_ = sc_count;
        if (current_imgui_ && current_imgui_->IsInitialized()) {
            current_imgui_->OnSwapchainRecreated(sc_count,
                static_cast<vk::Format>(bootstrap.GetBackend().GetSurfaceFormat().format));
        }
    }

    // Enable GPU stats
    const uint32_t frame_idx = bootstrap.GetSnapshot().frame_index;
    auto& cmd = backend.GetCommandBuffer(frame_idx);
    cmd.reset({});
    cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    if (gpu_stats_pool_) {
        auto& device = backend.GetDevice();
        std::array<uint64_t, 8> stats{};
        const VkResult qr = vkGetQueryPoolResults(
            static_cast<VkDevice>(*device),
            static_cast<VkQueryPool>(**gpu_stats_pool_),
            0, 1,
            sizeof(stats), stats.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (qr == VK_SUCCESS && stats[7] != 0) {
            LOGIFACE_LOG(trace,
                "GPU frame=" + std::to_string(frame_counter_) +
                " IA_verts=" + std::to_string(stats[0]) +
                " IA_prims=" + std::to_string(stats[1]) +
                " VS_invoc=" + std::to_string(stats[2]) +
                " clip_invoc=" + std::to_string(stats[3]) +
                " clip_prims=" + std::to_string(stats[4]) +
                " FS_invoc=" + std::to_string(stats[5]) +
                " CS_invoc=" + std::to_string(stats[6]));
        }
        cmd.resetQueryPool(**gpu_stats_pool_, 0, 1);
        cmd.beginQuery(**gpu_stats_pool_, 0, {});
    }

    // Phase 1: CPU gather + upload + descriptor writes (before render graph)
    if (current_scene_renderer_ && current_registry_) {
        const float aspect = static_cast<float>(current_width_) / static_cast<float>(current_height_);
        const glm::mat4 view = current_camera_->GetViewMatrix();
        const glm::mat4 proj = current_camera_->GetProjectionMatrix(aspect);
        current_view_proj_ = proj * view;

        // Bind actual depth to Hi-Z descriptor before hiz-gen pass executes
        const auto& depth_view = backend.GetDepthImageView(image_index);
        current_scene_renderer_->UpdateHizDepthBinding(frame_counter_, *depth_view);

        // Initialize Hi-Z on first frame
        current_scene_renderer_->InitializeHizFirstFrame(cmd);

        // CPU gather + upload + descriptor writes for all passes
        current_scene_renderer_->PrepareCompute(cmd, *current_registry_,
                                                view, proj,
                                                current_width_, current_height_,
                                                frame_counter_);
    }

    // Phase 2: Render graph executes all GPU passes in dependency order
    pipeline_->Execute(nullptr, cmd, image_index);

    if (gpu_stats_pool_) {
        cmd.endQuery(**gpu_stats_pool_, 0);
    }

    cmd.end();

    frame_counter_++;

    current_registry_ = nullptr;
    current_camera_ = nullptr;
    current_technique_mgr_ = nullptr;
    current_bindless_mgr_ = nullptr;
    current_scene_renderer_ = nullptr;
    current_imgui_ = nullptr;
}

} // namespace VulkanEngine::Renderer
