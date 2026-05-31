module;

#include <glm/glm.hpp> //NOLINT(misc-include-cleaner)

export module App.GridCameraController;

import VulkanEngine.Input;
import VulkanEngine.Renderer;
import VulkanBackend.Utils.CallbackList;

export namespace App::Game {

class GridCameraController {
public:
    explicit GridCameraController(VulkanEngine::Input::InputSystem& input_system);
    ~GridCameraController();

    GridCameraController(const GridCameraController&) = delete;
    GridCameraController& operator=(const GridCameraController&) = delete;

    void Update(const VulkanEngine::Input::RawInputState& raw_state);
    void Reset();

    [[nodiscard]] const VulkanEngine::Renderer::GridParams& GetGridParams() const { return grid_params_; }
    [[nodiscard]] float GetZoom() const { return grid_params_.zoom; }
    [[nodiscard]] glm::vec2 GetOffset() const { return grid_params_.offset; }

    [[nodiscard]] static float GetDefaultZoom() { return DEFAULT_ZOOM; }

private:
    void RecalculateSpacing();
    static float SnapToNiceInterval(float value);

    VulkanEngine::Input::InputSystem& input_system_;
    VulkanEngine::Input::ActionHandle pan_handle_{};
    VulkanEngine::Utils::ScopedHandle<void()> pan_start_token_{};
    VulkanEngine::Utils::ScopedHandle<void()> pan_end_token_{};
    bool is_panning_ = false;

    VulkanEngine::Renderer::GridParams grid_params_{};

    static constexpr float DEFAULT_ZOOM = 50.0f;
    static constexpr float ZOOM_FACTOR = 1.1f;
    static constexpr float MIN_ZOOM = 0.05f;
    static constexpr float MAX_ZOOM = 1000000.0f * DEFAULT_ZOOM;
    static constexpr float TARGET_LINES = 15.0f;
    static constexpr float REF_SCREEN_WIDTH = 1920.0f;
};

} // namespace App::Game
