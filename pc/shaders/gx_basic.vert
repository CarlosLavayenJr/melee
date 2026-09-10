#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor1;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vColor1;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vColor = inColor;
    vUV = inUV;
    vColor1 = inColor1;
}
