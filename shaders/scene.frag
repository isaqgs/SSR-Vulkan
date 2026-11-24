#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec3 fragWorldPosition;

layout(location = 0) out vec4 outSceneColor;
layout(location = 1) out vec4 outNormal;

void main() {
    vec3 n = normalize(fragNormal);
    vec3 lightDirection = normalize(vec3(-0.4, 0.8, 0.55));
    float diffuse = max(dot(n, lightDirection), 0.0);
    float specular = pow(max(dot(reflect(-lightDirection, n), vec3(0.0, 0.0, 1.0)), 0.0), 48.0);
    vec3 shadedColor = fragColor * (0.25 + 0.75 * diffuse) + vec3(specular * 0.16);

    if (abs(fragWorldPosition.y + 1.0) < 0.01) {
        vec2 cell = abs(fract(fragWorldPosition.xz) - 0.5);
        vec2 aa = fwidth(fragWorldPosition.xz);
        float gridLine = 1.0 - smoothstep(0.0, max(aa.x, aa.y) * 1.5, min(cell.x, cell.y));
        shadedColor = mix(shadedColor, vec3(0.38, 0.44, 0.54), gridLine * 0.22);
    }

    outSceneColor = vec4(shadedColor, 1.0);
    outNormal = vec4(n * 0.5 + 0.5, 1.0);
}
