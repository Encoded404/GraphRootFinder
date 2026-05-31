module;

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <cmath>
#include <glm/vec3.hpp> // NOLINT(misc-include-cleaner)
#include <glm/vec4.hpp> // NOLINT(misc-include-cleaner)
#include <glm/gtc/quaternion.hpp> // NOLINT(misc-include-cleaner)
#include <utility>
#include <filesystem>

#include <SDL3/SDL_keycode.h>
#include <imgui.h>
#include <vulkan/vulkan.hpp>

module App.Game;

import VulkanEngine.Game;
import VulkanEngine.Components.DynamicMesh;
import VulkanEngine.Components.MeshRenderer;
import VulkanEngine.Components.LineRenderer;
import VulkanEngine.GpuResources.MeshData;
import Shaders.Engine.StandardMeshFrag;
import Shaders.App.NormalsFrag;
import Shaders.App.SolidFrag;

namespace {
    static int GetDynamicPrecision(float value, int significant_digits, float zero_morethan_equal = 10.0f)
    {
        if (std::abs(value) >= zero_morethan_equal) {
            return 0;
        }

        if (value == 0.0f) {
            return significant_digits; // edge case: just return x
        }

        value = std::fabs(value);

        int precision = 0;

        // Move decimal until first non-zero digit appears
        while (value < 1.0f)
        {
            value *= 10.0f;
            precision++;
        }

        return precision + significant_digits;
    }
}

namespace App::Game {

GraphGame::GraphGame(const RenderMode render_mode, const std::filesystem::path& executable_path,
                   std::filesystem::path model_path,
                   std::filesystem::path texture_path)
    : render_mode_(render_mode)
    , exe_dir_(executable_path.parent_path())
    , model_path_(std::move(model_path))
    , texture_path_(std::move(texture_path)) {
    setup_token_ = hooks_.on_setup.Register([this](VulkanEngine::Application::ApplicationContext& ctx) -> bool {
        return OnSetup(ctx);
    });
    pre_input_token_ = hooks_.on_pre_input.Register([this](VulkanEngine::Application::ApplicationContext& ctx) {
        OnPreInput(ctx);
    });
    hooks_.should_filter_mouse_input = [this]() -> bool {
        return ShouldFilterMouseInput();
    };
    hooks_.should_filter_keyboard_input = [this]() -> bool {
        return ShouldFilterKeyboardInput();
    };
    frame_update_token_ = hooks_.on_frame_update.Register([this](VulkanEngine::Application::ApplicationContext& ctx) {
        OnFrameUpdate(ctx);
    });
    frame_render_token_ = hooks_.on_frame_render.Register([this](VulkanEngine::Application::ApplicationContext& ctx) {
        OnFrameRender(ctx);
    });
    shutdown_token_ = hooks_.on_shutdown.Register([this](VulkanEngine::Application::ApplicationContext& ctx) {
        OnShutdown(ctx);
    });
}

GraphGame::~GraphGame() = default;

bool GraphGame::OnSetup(VulkanEngine::Application::ApplicationContext& ctx) {
    // 1. Configure and init engine subsystems
    VulkanEngine::Game::GameConfig config{};
    config.enable_imgui = true;
    config.renderer_config.clear_color = {0.1f, 0.1f, 0.1f, 1.0f};
    config.renderer_config.grid_vert_spv = Shaders::App::GraphGridVert::GetSpirvWords();
    config.renderer_config.grid_frag_spv = Shaders::App::GraphGridFrag::GetSpirvWords();

    if (!engine_game_.Setup(ctx, config)) {
        return false;
    }

    // 2. Select fragment shader and init renderer
    std::span<const std::uint32_t> frag_spv;
    switch (render_mode_) {
        case RenderMode::Normals:
            frag_spv = Shaders::App::NormalsFrag::GetSpirvWords();
            break;
        case RenderMode::NoTextures:
            frag_spv = Shaders::App::SolidFrag::GetSpirvWords();
            break;
        default:
            frag_spv = Shaders::Engine::StandardMeshFrag::GetSpirvWords();
            break;
    }
    if (!engine_game_.InitRenderer(ctx, {}, frag_spv)) {
        return false;
    }

    // 3. Create grid camera controller
    camera_controller_ = std::make_unique<GridCameraController>(*ctx.input_system);
    engine_game_.GetRenderer().SetGridParams(camera_controller_->GetGridParams());

    // 4. Upload empty scene (no demo models)
    engine_game_.UploadScene(ctx, {});

    // 5. Create camera
    auto& backend = ctx.bootstrap->GetBackend();
    engine_game_.CreateCamera(backend.GetComponentRegistry());

    // 5b. Create a LineRenderer demo entity (rotating cross)
    {
        auto& entity = backend.GetComponentRegistry().CreateEntity();
        backend.GetComponentRegistry().AddComponent<VulkanEngine::Components::Transform>(entity);
        backend.GetComponentRegistry().AddComponent<VulkanEngine::Components::DynamicMesh>(entity);
        backend.GetComponentRegistry().AddComponent<VulkanEngine::Components::MeshRenderer>(entity);
        auto& line = backend.GetComponentRegistry().AddComponent<VulkanEngine::Components::LineRenderer>(entity);
        line.Setup(engine_game_.GetMeshManager(), 256);
        line_renderer_ = &line;
    }

    // 6. Register ImGui debug UI
    auto* imgui = engine_game_.GetImGuiSystem();
    if (imgui) {
        imgui_draw_handle_ = imgui->draw_callbacks.Register([this]() {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            if (ImGui::Begin("Grid View", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                auto& params = camera_controller_->GetGridParams();

                const float fixed_zoom = params.zoom / GridCameraController::GetDefaultZoom();
                const int position_precision = GetDynamicPrecision(fixed_zoom, 2);
                ImGui::Text("Position: (%.*f, %.*f)",
                    position_precision, params.offset.x,
                    position_precision, params.offset.y
                );
                ImGui::Text("Zoom: %.*fx", GetDynamicPrecision(fixed_zoom, 1), fixed_zoom); // default zoom appears to be 1x

                ImGui::Separator();

                if (ImGui::Button("Reset View")) {
                    camera_controller_->Reset();
                    engine_game_.GetRenderer().SetGridParams(camera_controller_->GetGridParams());
                }
            }
            ImGui::End();
        });
    }

    // 7. Bind quit action
    ctx.quit_action_handle = ctx.input_system->BindAction("quit",
        VulkanEngine::Input::InputBinding::Key(SDLK_ESCAPE));

    return true;
}

void GraphGame::OnPreInput(VulkanEngine::Application::ApplicationContext& /*ctx*/) {
}

bool GraphGame::ShouldFilterMouseInput() {
    return ImGui::GetIO().WantCaptureMouse;
}

bool GraphGame::ShouldFilterKeyboardInput() {
    return ImGui::GetIO().WantCaptureKeyboard;
}

void GraphGame::OnFrameUpdate(const VulkanEngine::Application::ApplicationContext& ctx) {
    camera_controller_->Update(ctx.input_system->GetRawState());

    // Update line renderer segments — runs BEFORE engine_game_.FrameUpdate
    // so that LineRenderer::Update (called inside FrameUpdate) picks up the changes.
    if (line_renderer_ != nullptr) {
        line_angle_ += ctx.frame.delta_time * 0.5f;
        const float r = 0.5f;
        const float c = std::cos(line_angle_);
        const float s = std::sin(line_angle_);

        line_renderer_->segments = {
            {{-r * c, -r * s, 0.0f}, {r * c, r * s, 0.0f}},
            {{-r * s,  r * c, 0.0f}, {r * s, -r * c, 0.0f}},
            {{-r, 0.0f, -r}, {r, 0.0f, -r}},
            {{0.0f, -r, -r}, {0.0f, r, -r}},
        };
        line_renderer_->mesh_dirty = true;
    }

    engine_game_.FrameUpdate(ctx);
    engine_game_.GetRenderer().SetGridParams(camera_controller_->GetGridParams());
}

void GraphGame::OnFrameRender(const VulkanEngine::Application::ApplicationContext& ctx) {
    engine_game_.FrameRender(ctx);
}

void GraphGame::OnShutdown(VulkanEngine::Application::ApplicationContext& /*ctx*/) {
    imgui_draw_handle_ = {};
    camera_controller_.reset();
    engine_game_.Shutdown();
}

} // namespace App::Game
