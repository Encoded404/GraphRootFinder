#version 450

layout(location = 0) out vec2 outUV;

void main() {
    float x = (gl_VertexIndex == 2) ? 3.0 : -1.0;
    float y = (gl_VertexIndex == 1) ? 3.0 : -1.0;
    outUV = vec2(x, y) * 0.5 + 0.5;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
