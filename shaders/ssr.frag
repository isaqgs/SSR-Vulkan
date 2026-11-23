#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;
layout(set = 0, binding = 1) uniform sampler2D SceneDepth;
layout(set = 0, binding = 2) uniform sampler2D NormalTexture;

layout(set = 0, binding = 3) uniform CameraUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 inverseProjection;
    mat4 inverseView;
} camera;

layout(set = 0, binding = 4) uniform SSRParameters {
    int maxSteps;
    float maxDistance;
    float stride;
    float thickness;
    float intensity;
    int debugMode;
} params;

vec3 reconstructViewPosition(vec2 uv, float depth) {
    vec4 clipPosition = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPositionH = camera.inverseProjection * clipPosition;
    if (abs(viewPositionH.w) < 0.000001)
        return vec3(0.0);
    return viewPositionH.xyz / viewPositionH.w;
}

vec2 projectToUV(vec3 viewPosition) {
    vec4 clipPosition = camera.projection * vec4(viewPosition, 1.0);
    if (abs(clipPosition.w) < 0.000001)
        return vec2(-1.0);
    vec3 ndc = clipPosition.xyz / clipPosition.w;
    return ndc.xy * 0.5 + 0.5;
}

bool insideScreen(vec2 uv) {
    return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
}

vec3 decodeNormal(vec3 encodedNormal) {
    vec3 n = encodedNormal * 2.0 - 1.0;
    float lengthN = length(n);
    if (lengthN < 0.000001)
        return vec3(0.0, 0.0, 1.0);
    return n / lengthN;
}

void main() {
    float depth = texture(SceneDepth, fragUV).r;
    if (depth <= 0.0 || depth >= 1.0) {
        outColor = vec4(0.0);
        return;
    }

    vec3 normal = decodeNormal(texture(NormalTexture, fragUV).rgb);
    vec3 origin = reconstructViewPosition(fragUV, depth);
    vec3 viewDirection = normalize(-origin);
    vec3 reflectionDirection = normalize(reflect(-viewDirection, normal));

    if (dot(reflectionDirection, normal) <= 0.0) {
        outColor = vec4(0.0);
        return;
    }

    vec3 rayOrigin = origin + reflectionDirection * 0.01;
    vec3 rayEnd = rayOrigin + reflectionDirection * params.maxDistance;
    vec2 rayStartUV = projectToUV(rayOrigin);
    vec2 rayEndUV = projectToUV(rayEnd);

    if (!insideScreen(rayStartUV)) {
        outColor = vec4(0.0);
        return;
    }

    float worldStep = max(params.stride, 0.0001);

    bool hit = false;
    vec2 hitUV = vec2(0.0);
    float hitDistance = 0.0;
    float previousDifference = 0.0;
    bool hasPreviousDifference = false;

    int stepCount = clamp(params.maxSteps, 1, 256);
    for (int i = 1; i <= stepCount; ++i) {
        hitDistance = worldStep * float(i);
        if (hitDistance > params.maxDistance)
            break;

        vec3 rayPosition = rayOrigin + reflectionDirection * hitDistance;
        vec2 sampleUV = projectToUV(rayPosition);
        if (!insideScreen(sampleUV))
            break;

        float sceneDepth = texture(SceneDepth, sampleUV).r;
        if (sceneDepth <= 0.0 || sceneDepth >= 1.0)
            continue;

        vec3 scenePosition = reconstructViewPosition(sampleUV, sceneDepth);
        float depthDifference = rayPosition.z - scenePosition.z;

        bool crossedSurface = hasPreviousDifference &&
                              previousDifference * depthDifference <= 0.0;
        bool withinThickness = abs(depthDifference) <= params.thickness;

        if (crossedSurface && withinThickness && hitDistance >= 0.05) {
            float loDistance = hitDistance - worldStep;
            float hiDistance = hitDistance;

            for (int refine = 0; refine < 6; ++refine) {
                float midDistance = (loDistance + hiDistance) * 0.5;
                vec3 midRayPosition = rayOrigin + reflectionDirection * midDistance;
                vec2 midUV = projectToUV(midRayPosition);

                if (!insideScreen(midUV)) {
                    loDistance = midDistance;
                    continue;
                }

                float midSceneDepth = texture(SceneDepth, midUV).r;
                if (midSceneDepth <= 0.0 || midSceneDepth >= 1.0) {
                    loDistance = midDistance;
                    continue;
                }

                vec3 midScenePosition = reconstructViewPosition(midUV, midSceneDepth);
                float midDifference = midRayPosition.z - midScenePosition.z;

                if (midDifference >= -params.thickness && midDifference <= params.thickness)
                    hiDistance = midDistance;
                else
                    loDistance = midDistance;
            }

            hitDistance = (loDistance + hiDistance) * 0.5;
            vec3 hitPosition = rayOrigin + reflectionDirection * hitDistance;
            hitUV = projectToUV(hitPosition);
            hit = insideScreen(hitUV);
            break;
        }

        previousDifference = depthDifference;
        hasPreviousDifference = true;
    }

    if (!hit) {
        outColor = params.debugMode != 0 ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(0.0);
        return;
    }

    if (params.debugMode != 0) {
        outColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 reflectedColor = texture(SceneColor, hitUV).rgb;
    float distanceFactor = 1.0 - clamp(hitDistance / params.maxDistance, 0.0, 1.0);
    float edgeFactor = smoothstep(0.0, 0.08, hitUV.x) * smoothstep(0.0, 0.08, 1.0 - hitUV.x);
    edgeFactor *= smoothstep(0.0, 0.08, hitUV.y) * smoothstep(0.0, 0.08, 1.0 - hitUV.y);
    float viewFactor = clamp(dot(normal, viewDirection), 0.0, 1.0);
    float fresnelFactor = pow(1.0 - viewFactor, 2.0);
    float reflectionFactor = clamp(params.intensity * distanceFactor * edgeFactor * fresnelFactor, 0.0, 1.0);

    outColor = vec4(reflectedColor * reflectionFactor, reflectionFactor);
}
