#version 450

layout(location = 0) out vec2 fragUV;

void main() {
    vec2 vertexUV;
    if (gl_VertexIndex == 0)
        vertexUV = vec2(0.0, 0.0);
    else if (gl_VertexIndex == 1)
        vertexUV = vec2(2.0, 0.0);
    else
        vertexUV = vec2(0.0, 2.0);

    vec2 clipPosition = vertexUV * 2.0 - 1.0;
    gl_Position = vec4(clipPosition, 0.0, 1.0);
        fragUV = vertexUV;
}
