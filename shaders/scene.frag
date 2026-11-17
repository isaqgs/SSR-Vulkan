#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;

layout(location = 0) out vec4 outSceneColor;
layout(location = 1) out vec4 outNormal;

void main() {
    vec3 n = normalize(fragNormal);
    outSceneColor = vec4(fragColor, 1.0);
    outNormal = vec4(n * 0.5 + 0.5, 1.0);
}
