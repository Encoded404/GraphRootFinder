#version 450

// Push constants — guaranteed 128 bytes by Vulkan spec.
// Layout matches C++ GridParams struct exactly (std430 compatible).
layout(push_constant) uniform GridParams {
    vec2  offset;          // 8 bytes, graph coordinate at screen center (pan)
    vec2  screen_size;     // 8 bytes, viewport dimensions in pixels
    float zoom;            // 4 bytes, pixels per graph-space unit
    float spacing;         // 4 bytes, interval between grid lines in graph units
    float line_thickness;  // 4 bytes, grid line width in pixels
    float axis_thickness;  // 4 bytes, axis line width in pixels
    vec4  bg_color;        // 16 bytes, background color (rgba)
    vec4  grid_color;      // 16 bytes, grid line color (rgba)
    vec4  axis_color;      // 16 bytes, axis line color (rgba)
} params; // 80 bytes

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {

    // ── Coordinate transform ─────────────────────────────────────────

    // Convert UV [0,1] to pixel position relative to screen center
    vec2 pixel_pos = (inUV - 0.5) * params.screen_size;

    // Convert to graph-space coordinate
    vec2 graph = pixel_pos / params.zoom + params.offset;


    // ── Grid line computation ────────────────────────────────────────

    // Distance from current pixel to nearest grid line, in graph units.
    // fract(graph / spacing + 0.5) maps each interval to [0,1] centered,
    // so subtracting 0.5 gives distance from center of interval.
    // Multiplying by spacing * zoom converts back to screen pixels.
    vec2 grid_dist = abs(fract(graph / params.spacing + 0.5) - 0.5)
                   * params.spacing * params.zoom;

    // Take the closer of the X and Y grid lines
    float min_grid_dist = min(grid_dist.x, grid_dist.y);

    // Smooth edge: fully opaque within (thickness - 0.5) px, fade over 1 px
    float grid_alpha = 1.0 - smoothstep(
        params.line_thickness - 0.5,
        params.line_thickness + 0.5,
        min_grid_dist);


    // ── Axis computation ─────────────────────────────────────────────

    // Distance from current pixel to nearest axis (graph.x == 0 or graph.y == 0)
    float axis_dist = min(abs(graph.x), abs(graph.y)) * params.zoom;

    float axis_alpha = 1.0 - smoothstep(
        params.axis_thickness - 0.5,
        params.axis_thickness + 0.5,
        axis_dist);


    // ── Color blending ───────────────────────────────────────────────

    // Start with background, overlay grid lines, then axes on top
    vec3 color = params.bg_color.rgb;
    color = mix(color, params.grid_color.rgb, grid_alpha);
    color = mix(color, params.axis_color.rgb, axis_alpha);

    outColor = vec4(color, 1.0);
}
