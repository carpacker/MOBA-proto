#version 450
// M2.2: sample the material texture (set=1 per the roadmap — set=0 is reserved for
// the M2.3 per-frame UBO). The image is R8G8B8A8_SRGB, so texture() returns LINEAR
// values; the *_SRGB swapchain re-encodes on write. No manual conversion anywhere.

layout(set = 1, binding = 0) uniform sampler2D u_texture;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    o_color = texture(u_texture, v_uv);
}
