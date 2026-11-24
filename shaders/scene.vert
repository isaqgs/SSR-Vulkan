#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec3 fragWorldPosition;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 inverseProjection;
    mat4 inverseView;
} camera;

void main() {
    vec4 world = camera.model * vec4(inPosition, 1.0);
    vec4 viewPosition = camera.view * world;
    gl_Position = camera.projection * viewPosition;
    fragNormal = mat3(camera.view * camera.model) * inNormal;
    fragColor = inColor;
    fragWorldPosition = world.xyz;
}
