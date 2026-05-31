module;

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <glm/vec4.hpp> // NOLINT(misc-include-cleaner)
#include <glm/gtc/quaternion.hpp> // NOLINT(misc-include-cleaner)
#include <utility>
#include <filesystem>

#include <SDL3/SDL_keycode.h>
#include <imgui.h>
#include <vulkan/vulkan.hpp>

module App.Game;

import VulkanEngine.Game;
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

DemoGame::DemoGame(const RenderMode render_mode, const std::filesystem::path& executable_path,
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

DemoGame::~DemoGame() = default;

bool DemoGame::OnSetup(VulkanEngine::Application::ApplicationContext& ctx) {
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

    // 6. Register ImGui debug UI
    auto* imgui = engine_game_.GetImGuiSystem();
    if (imgui) {
        imgui_draw_handle_ = imgui->draw_callbacks.Register([this]() {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            if (ImGui::Begin("Grid View", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                auto& params = camera_controller_->GetGridParams();

                const float fixed_zoom = params.zoom / GridCameraController::GetDefaultZoom();
                ImGui::Text("Position: (%.*f, %.*f)",
                    GetDynamicPrecision(params.offset.x, 1), params.offset.x,
                    GetDynamicPrecision(params.offset.y, 1), params.offset.y
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

void DemoGame::OnPreInput(VulkanEngine::Application::ApplicationContext& /*ctx*/) {
}

bool DemoGame::ShouldFilterMouseInput() {
    return ImGui::GetIO().WantCaptureMouse;
}

bool DemoGame::ShouldFilterKeyboardInput() {
    return ImGui::GetIO().WantCaptureKeyboard;
}

void DemoGame::OnFrameUpdate(const VulkanEngine::Application::ApplicationContext& ctx) {
    camera_controller_->Update(ctx.input_system->GetRawState());
    engine_game_.FrameUpdate(ctx);
    engine_game_.GetRenderer().SetGridParams(camera_controller_->GetGridParams());
}

void DemoGame::OnFrameRender(const VulkanEngine::Application::ApplicationContext& ctx) {
    engine_game_.FrameRender(ctx);
}

void DemoGame::OnShutdown(VulkanEngine::Application::ApplicationContext& /*ctx*/) {
    imgui_draw_handle_ = {};
    engine_game_.Shutdown();
}

} // namespace App::Game
