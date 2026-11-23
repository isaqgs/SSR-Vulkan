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

    bool insideButton = fragUV.x >= 0.02 && fragUV.x <= 0.22 &&
                        fragUV.y >= 0.02 && fragUV.y <= 0.09;
    if (insideButton) {
        bool buttonBorder = fragUV.x < 0.023 || fragUV.x > 0.217 ||
                            fragUV.y < 0.025 || fragUV.y > 0.085;
        vec3 buttonColor = options.ssrEnabled != 0
                               ? vec3(0.12, 0.55, 0.24)
                               : vec3(0.65, 0.16, 0.14);
        finalColor = buttonBorder ? vec3(0.9) : buttonColor;
    }

    outColor = vec4(finalColor, 1.0);
}
