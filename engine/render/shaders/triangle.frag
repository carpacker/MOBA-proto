#version 450
// M2.1: pass the interpolated vertex color through. The swapchain is *_SRGB, so the
// hardware encodes this linear value to sRGB on write — do not encode manually.

layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 o_color;

void main() {
    o_color = vec4(v_color, 1.0);
}
