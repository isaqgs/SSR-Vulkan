#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;
layout(set = 0, binding = 1) uniform sampler2D SSRResult;
layout(set = 0, binding = 2) uniform sampler2D SceneDepth;
layout(set = 0, binding = 3) uniform sampler2D NormalTexture;

layout(set = 0, binding = 4) uniform CameraUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 inverseProjection;
    mat4 inverseView;
} camera;

layout(push_constant) uniform CompositeOptions {
    uint ssrEnabled;
    uint visualizerMode;
    uint comparisonEnabled;
} options;

vec3 visualizeDepth(vec2 uv) {
    float depth = texture(SceneDepth, uv).r;
    if (depth >= 1.0)
        return vec3(0.0);

    vec4 viewPositionH = camera.inverseProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    if (abs(viewPositionH.w) < 0.000001)
        return vec3(0.0);

    float linearDepth = abs(viewPositionH.z / viewPositionH.w);
    float depthValue = 1.0 - clamp(log(1.0 + linearDepth) / log(51.0), 0.0, 1.0);
    return vec3(depthValue);
}

vec3 visualizeBuffer(vec2 uv) {
    if (options.visualizerMode == 1)
        return texture(SceneColor, uv).rgb;
    if (options.visualizerMode == 2)
        return visualizeDepth(uv);
    if (options.visualizerMode == 3) {
        if (texture(SceneDepth, uv).r >= 1.0)
            return vec3(0.0);
        return texture(NormalTexture, uv).rgb;
    }
    if (options.visualizerMode == 4)
        return vec3(texture(SSRResult, uv).a);
    if (options.visualizerMode == 5)
        return texture(SSRResult, uv).rgb;
    if (texture(SceneDepth, uv).r >= 1.0)
        return vec3(0.0);
    return vec3(texture(NormalTexture, uv).a);
}

void main() {
    if (options.visualizerMode != 0) {
        outColor = vec4(visualizeBuffer(fragUV), 1.0);
        return;
    }

    if (options.comparisonEnabled != 0) {
        if (fragUV.y < 0.25 || fragUV.y > 0.75) {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        vec2 comparisonUV = vec2(fract(fragUV.x * 2.0), (fragUV.y - 0.25) * 2.0);
        vec3 scene = texture(SceneColor, comparisonUV).rgb;
        vec3 finalColor = scene;
        if (fragUV.x < 0.5 && options.ssrEnabled != 0)
            finalColor += texture(SSRResult, comparisonUV).rgb;
        if (abs(fragUV.x - 0.5) < 0.0015)
            finalColor = vec3(0.95);
        outColor = vec4(finalColor, 1.0);
        return;
    }

    vec3 finalColor = texture(SceneColor, fragUV).rgb;
    if (options.ssrEnabled != 0)
        finalColor += texture(SSRResult, fragUV).rgb;
    outColor = vec4(finalColor, 1.0);
}
