#version 450
// M2.2 textured quad: the first pipeline fed by a REAL vertex buffer (QuadVertex:
// vec2 pos + vec2 uv, see the renderer's VERTEX_LAYOUT_POS2_UV2). Positions are
// already NDC (Y-down); no transforms until the camera UBO lands in M2.3.

layout(location = 0) in  vec2 a_pos;
layout(location = 1) in  vec2 a_uv;
layout(location = 0) out vec2 v_uv;

void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv        = a_uv;
}
