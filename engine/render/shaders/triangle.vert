#version 450
// M2.1 first triangle (roadmap): vertices hardcoded IN the shader — no vertex input
// state, no buffers yet (those arrive with the textured quad, M2.2). Indexed by
// gl_VertexIndex from vkCmdDraw(3, 1, 0, 0).
//
// Vulkan NDC is Y-DOWN (+y toward the bottom of the screen), so (0, -0.5) is the TOP
// vertex: red on top, green bottom-right, blue bottom-left.

layout(location = 0) out vec3 v_color;

const vec2 k_pos[3] = vec2[](
    vec2( 0.0, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);
const vec3 k_color[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

void main() {
    gl_Position = vec4(k_pos[gl_VertexIndex], 0.0, 1.0);
    v_color     = k_color[gl_VertexIndex];
}
