#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require
#include "utils/ray_payload.glsl"
#include "utils/uniform_buffer_object.glsl"

layout(binding = 2) readonly uniform UniformBufferObjectStruct { UniformBufferObject ubo_; };

layout(location = 0) rayPayloadInEXT PerRayData prd_;

void main() {
    if (ubo_.has_sky) {
        const float t = 0.5*(normalize(gl_WorldRayDirectionEXT).y + 1);
        const vec3 skyColor = mix(vec3(1.0), vec3(0.5, 0.7, 1.0), t);
        prd_.radiance = skyColor;
    }
    else {
        prd_.radiance = vec3(0.0f);
    }

    prd_.done = true;
}
