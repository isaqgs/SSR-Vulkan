#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;
layout(set = 0, binding = 1) uniform sampler2D SSRResult;

void main() {
    vec3 scene = texture(SceneColor, fragUV).rgb;
    vec4 ssr = texture(SSRResult, fragUV);
    outColor = vec4(scene + ssr.rgb, 1.0);
}
