module;

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp> // NOLINT(misc-include-cleaner)
#include <algorithm>
#include <cmath>

module App.GridCameraController;

namespace App::Game {

GridCameraController::GridCameraController(VulkanEngine::Input::InputSystem& input_system)
    : input_system_(input_system) {
    grid_params_.zoom = DEFAULT_ZOOM;
    grid_params_.offset = {0.0f, 0.0f};
    RecalculateSpacing();

    pan_handle_ = input_system_.BindAction("grid_pan",
        VulkanEngine::Input::InputBinding::MouseButton(1));

    pan_start_token_ = input_system_.RegisterStartedCallback(pan_handle_, [this]() {
        is_panning_ = true;
    });
    pan_end_token_ = input_system_.RegisterEndedCallback(pan_handle_, [this]() {
        is_panning_ = false;
    });
}

GridCameraController::~GridCameraController() {
    pan_start_token_ = {};
    pan_end_token_ = {};
    input_system_.UnbindAction(pan_handle_);
}

void GridCameraController::Update(const VulkanEngine::Input::RawInputState& raw_state) {
    if (raw_state.wheel_y != 0.0f) {
        const float factor = (raw_state.wheel_y > 0.0f) ? ZOOM_FACTOR : (1.0f / ZOOM_FACTOR);
        const float ticks = std::abs(raw_state.wheel_y);
        grid_params_.zoom *= std::pow(factor, ticks);
        grid_params_.zoom = std::clamp(grid_params_.zoom, MIN_ZOOM, MAX_ZOOM);
        RecalculateSpacing();
    }

    if (is_panning_ && (raw_state.mouse_delta_x != 0.0f || raw_state.mouse_delta_y != 0.0f)) {
        grid_params_.offset.x -= raw_state.mouse_delta_x / grid_params_.zoom;
        grid_params_.offset.y += raw_state.mouse_delta_y / grid_params_.zoom;
    }
}

void GridCameraController::Reset() {
    grid_params_.zoom = DEFAULT_ZOOM;
    grid_params_.offset = {0.0f, 0.0f};
    RecalculateSpacing();
}

void GridCameraController::RecalculateSpacing() {
    const float visible_range = REF_SCREEN_WIDTH / grid_params_.zoom;
    const float target = visible_range / TARGET_LINES;
    grid_params_.spacing = SnapToNiceInterval(target);
}

float GridCameraController::SnapToNiceInterval(float value) {
    if (value <= 0.0f) return 0.01f;
    const float mag = std::pow(10.0f, std::floor(std::log10(value)));
    const float norm = value / mag;
    if (norm < 1.5f) return mag * 1.0f;
    if (norm < 3.5f) return mag * 2.0f;
    if (norm < 7.5f) return mag * 5.0f;
    return mag * 10.0f;
}

} // namespace App::Game
