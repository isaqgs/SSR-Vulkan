#version 450

layout(location = 0) out vec2 fragUV;

void main() {
    vec2 uv;
    if (gl_VertexIndex == 0) uv = vec2(0.0, 0.0);
    else if (gl_VertexIndex == 1) uv = vec2(2.0, 0.0);
    else uv = vec2(0.0, 2.0);

    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
        fragUV = uv;
}
