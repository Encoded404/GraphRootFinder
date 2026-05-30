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
import App.Components.SimpleControllerComponent;
import App.Components.TransformControlComponent;
import Shaders.Engine.StandardMeshFrag;
import Shaders.App.NormalsFrag;
import Shaders.App.SolidFrag;

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

    // 3. Set initial grid parameters
    grid_params_.zoom = 50.0f;
    grid_params_.spacing = 1.0f;
    grid_params_.line_thickness = 0.75f;
    grid_params_.axis_thickness = 1.5f;
    grid_params_.bg_color = glm::vec4(0.10f, 0.10f, 0.12f, 1.0f);
    grid_params_.grid_color = glm::vec4(0.20f, 0.20f, 0.22f, 1.0f);
    grid_params_.axis_color = glm::vec4(0.45f, 0.45f, 0.55f, 1.0f);
    engine_game_.GetRenderer().SetGridParams(grid_params_);

    // 4. Create a custom material for the viking room
    const uint32_t tex_slot = engine_game_.LoadTexture(ctx, exe_dir_ / "textures" / "viking_room.png");
    constexpr auto viking_blend = VulkanEngine::MaterialManager::BlendMode::Transparent;

    const auto viking_mat_id = VulkanEngine::MaterialManager::MaterialManager::Get().RegisterMaterial({
        .technique_id = engine_game_.GetMainTechniqueId(),
        .texture_slot = VulkanEngine::BindlessManager::TextureSlot{static_cast<uint16_t>(tex_slot)},
        .blend_mode = viking_blend
    }, engine_game_.GetResourceManager(), engine_game_.GetBindlessManager());

    // 4. Load meshes and register with MeshRegistry
    const std::vector<VulkanEngine::SceneLoader::MaterialId> viking_bindings = {viking_mat_id};

    auto viking_mesh = VulkanEngine::SceneLoader::SceneLoader::LoadMeshData(
        exe_dir_ / "models" / "viking_room.obj", &viking_bindings);
    auto monkey_mesh = VulkanEngine::SceneLoader::SceneLoader::LoadMeshData(
        exe_dir_ / "models" / "simple-monkey.bin", nullptr);

    auto& mesh_registry = engine_game_.GetMeshRegistry();
    const uint32_t viking_id = mesh_registry.Register(viking_mesh);
    const uint32_t monkey_id = mesh_registry.Register(monkey_mesh);

    // Mark scene as valid so rendering begins
    engine_game_.MarkSceneValid();

    // 5. Create camera
    auto& backend = ctx.bootstrap->GetBackend();
    engine_game_.CreateCamera(backend.GetComponentRegistry());

    // 6. Create game entities with simplified MeshReference
    {
        auto& entity = backend.GetComponentRegistry().CreateEntity();
        backend.GetComponentRegistry().AddComponent<VulkanEngine::Components::Transform>(entity);
        auto& mesh_ref = backend.GetComponentRegistry().AddComponent<VulkanEngine::Components::MeshReference>(entity);
        mesh_ref.loaded_mesh_id = viking_id;

        auto& debug_comp = backend.GetComponentRegistry().AddComponent<App::Components::TransformControlComponent>(entity);
        debug_comp.position = glm::vec3{0.0f, 0.0f, 0.0f};
    }
    {
        auto& entity = backend.GetComponentRegistry().CreateEntity();
        backend.GetComponentRegistry().AddComponent<VulkanEngine::Components::Transform>(entity);
        auto& mesh_ref = backend.GetComponentRegistry().AddComponent<VulkanEngine::Components::MeshReference>(entity);
        mesh_ref.loaded_mesh_id = monkey_id;
        backend.GetComponentRegistry().AddComponent<App::Components::SimpleControllerComponent>(entity, ctx.input_system);
    }

    // 7. Register ImGui debug UI
    auto* imgui = engine_game_.GetImGuiSystem();
    if (imgui) {
        imgui_draw_handle_ = imgui->draw_callbacks.Register([this, &registry = backend.GetComponentRegistry()]() {
            if (ImGui::Begin("Grid Control", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                bool changed = false;

                ImGui::SeparatorText("Transform");
                changed |= ImGui::DragFloat2("Offset", &grid_params_.offset.x, 0.1f);
                changed |= ImGui::DragFloat("Zoom", &grid_params_.zoom, 1.0f, 1.0f, 1000.0f);
                changed |= ImGui::DragFloat("Spacing", &grid_params_.spacing, 0.1f, 0.01f, 100.0f);

                ImGui::SeparatorText("Thickness");
                changed |= ImGui::DragFloat("Grid", &grid_params_.line_thickness, 0.1f, 0.1f, 10.0f);
                changed |= ImGui::DragFloat("Axis", &grid_params_.axis_thickness, 0.1f, 0.1f, 10.0f);

                ImGui::SeparatorText("Colors");
                changed |= ImGui::ColorEdit3("Background", &grid_params_.bg_color.x);
                changed |= ImGui::ColorEdit3("Grid Lines", &grid_params_.grid_color.x);
                changed |= ImGui::ColorEdit3("Axes", &grid_params_.axis_color.x);

                if (changed) {
                    engine_game_.GetRenderer().SetGridParams(grid_params_);
                }
            }
            ImGui::End();

            auto debug_comps = registry.GetAll<App::Components::TransformControlComponent>();
            if (debug_comps.empty()) return;

            for (auto* dc : debug_comps) {
                ImGui::Begin("Transform Control", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

                ImGui::DragFloat3("Position", &dc->position.x, 0.1f);

                ImGui::SeparatorText("Texture");
                ImGui::DragInt("Slot", &dc->texture_slot, 1, 0, 255);

                ImGui::SeparatorText("Rotation");
                constexpr const char* modes[] = {"Euler (vec3)", "Quaternion (vec4)"}; // NOLINT(modernize-avoid-c-arrays)
                int mode = static_cast<int>(dc->rotation_mode);
                if (ImGui::Combo("Mode", &mode, modes, 2)) {
                    dc->rotation_mode = static_cast<App::Components::RotationMode>(mode);
                }
                if (dc->rotation_mode == App::Components::RotationMode::Euler) {
                    ImGui::DragFloat3("Euler (deg)", &dc->rotation_euler.x, 1.0f);
                } else {
                    ImGui::DragFloat4("Quaternion", &dc->rotation_quat.x, 0.01f);
                }

                ImGui::End();
            }
        });
    }

    // 8. Bind quit action
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
    engine_game_.FrameUpdate(ctx);
    engine_game_.GetRenderer().SetGridParams(grid_params_);
}

void DemoGame::OnFrameRender(const VulkanEngine::Application::ApplicationContext& ctx) {
    engine_game_.FrameRender(ctx);
}

void DemoGame::OnShutdown(VulkanEngine::Application::ApplicationContext& /*ctx*/) {
    imgui_draw_handle_ = {};
    engine_game_.Shutdown();
}

} // namespace App::Game
