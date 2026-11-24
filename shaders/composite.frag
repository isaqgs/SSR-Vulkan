#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;
layout(set = 0, binding = 1) uniform sampler2D SSRResult;

layout(push_constant) uniform CompositeOptions {
    uint ssrEnabled;
} options;

void main() {
    vec3 scene = texture(SceneColor, fragUV).rgb;
    vec3 finalColor = scene;
    if (options.ssrEnabled != 0) {
        finalColor += texture(SSRResult, fragUV).rgb;
    }

    outColor = vec4(finalColor, 1.0);
}
