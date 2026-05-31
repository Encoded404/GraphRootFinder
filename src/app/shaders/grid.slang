struct GridParams {
    float2 offset;
    float2 screen_size;
    float zoom;
    float spacing;
    float line_thickness;
    float axis_thickness;
    float4 bg_color;
    float4 grid_color;
    float4 axis_color;
};

[[vk::push_constant]]
ConstantBuffer<GridParams> params;

struct VSOutput {
    [[vk::location(0)]] float2 outUV : TEXCOORD0;
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vert_main(uint vertexIndex : SV_VertexIndex) {
    VSOutput output;
    float x = (vertexIndex == 2) ? 3.0 : -1.0;
    float y = (vertexIndex == 1) ? 3.0 : -1.0;
    output.outUV = float2(x, y) * 0.5 + 0.5;
    output.position = float4(x, y, 0.0, 1.0);
    return output;
}

struct PSInput {
    [[vk::location(0)]] float2 inUV : TEXCOORD0;
};

[shader("fragment")]
float4 frag_main(PSInput input) : SV_TARGET {
    float2 pixel_pos = (input.inUV - 0.5) * params.screen_size;
    float2 graph = pixel_pos / params.zoom + params.offset;

    float2 grid_dist = abs(frac(graph / params.spacing + 0.5) - 0.5)
                     * params.spacing * params.zoom;
    float min_grid_dist = min(grid_dist.x, grid_dist.y);

    float grid_alpha = 1.0 - smoothstep(
        params.line_thickness - 0.5,
        params.line_thickness + 0.5,
        min_grid_dist);

    float axis_dist = min(abs(graph.x), abs(graph.y)) * params.zoom;
    float axis_alpha = 1.0 - smoothstep(
        params.axis_thickness - 0.5,
        params.axis_thickness + 0.5,
        axis_dist);

    float3 color = params.bg_color.rgb;
    color = lerp(color, params.grid_color.rgb, grid_alpha);
    color = lerp(color, params.axis_color.rgb, axis_alpha);

    return float4(color, 1.0);
}
