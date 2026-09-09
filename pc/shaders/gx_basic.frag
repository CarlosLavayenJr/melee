#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set=0, binding=0) uniform sampler2D gxTexture;
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 reg0;
    uint color;
    uint alpha;
    uint compare;
    uint textured;
} pc;

vec3 colorInput(uint n, vec4 t) {
    switch(n) {
    case 2: return pc.reg0.rgb;
    case 3: return pc.reg0.aaa;
    case 8: return t.rgb;
    case 9: return t.aaa;
    case 10: return vColor.rgb;
    case 11: return vColor.aaa;
    case 12: return vec3(1);
    case 13: return vec3(0.5);
    default: return vec3(0); // ZERO; unsupported inputs rejected by CPU
    }
}
float alphaInput(uint n, vec4 t) {
    if(n == 1) return pc.reg0.a;
    if(n == 4) return t.a;
    if(n == 5) return vColor.a;
    return 0;
}
vec3 operation(vec3 a, vec3 b, vec3 c, vec3 d, uint op) {
    vec3 v = mix(a, b, c);
    v = d + (((op >> 18) & 1) != 0 ? -v : v);
    uint bias = (op >> 16) & 3;
    v += bias == 1 ? 0.5 : (bias == 2 ? -0.5 : 0.0);
    uint scale = (op >> 20) & 3;
    v *= scale == 3 ? 0.5 : float(1 << scale);
    return ((op >> 19) & 1) != 0 ? clamp(v, 0, 1) : clamp(v, -4, 4);
}
bool comparison(uint op, float a, float refValue) {
    switch(op) {
    case 0: return false;
    case 1: return a < refValue;
    case 2: return a == refValue;
    case 3: return a <= refValue;
    case 4: return a > refValue;
    case 5: return a != refValue;
    case 6: return a >= refValue;
    default: return true;
    }
}

void main() {
    vec4 t = pc.textured != 0 ? texture(gxTexture, vUV) : vec4(0);
    uint c = pc.color, a = pc.alpha;
    vec3 rgb = operation(colorInput((c>>12)&15,t), colorInput((c>>8)&15,t),
                         colorInput((c>>4)&15,t), colorInput(c&15,t), c);
    float alpha = operation(vec3(alphaInput((a>>13)&7,t)), vec3(alphaInput((a>>10)&7,t)),
                             vec3(alphaInput((a>>7)&7,t)), vec3(alphaInput((a>>4)&7,t)), a).x;
    float alpha8 = round(clamp(alpha, 0, 1) * 255.0);
    bool p = comparison((pc.compare>>16)&7, alpha8, float(pc.compare&255));
    bool q = comparison((pc.compare>>19)&7, alpha8, float((pc.compare>>8)&255));
    uint logic = (pc.compare>>22)&3;
    if (!(logic == 0 ? p && q : logic == 1 ? p || q : logic == 2 ? p != q : p == q)) discard;
    outColor = clamp(vec4(rgb, alpha), 0, 1);
}
