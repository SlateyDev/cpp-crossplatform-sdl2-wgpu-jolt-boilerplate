struct Camera {
    view_proj: mat4x4<f32>,
    pos: vec4<f32>,
    forward: vec4<f32>,
}

struct CascadeInfo {
    view_proj: mat4x4<f32>,
    split_depth: f32,
}

struct Light {
    cascades: array<CascadeInfo, 4>,
    pos: vec4<f32>,
}

struct ModelMatrices {
    model_matrix: mat4x4<f32>,
    normal_matrix: mat4x4<f32>,
}

@group(0) @binding(0) var<uniform> camera: Camera;
@group(0) @binding(1) var<uniform> light: Light;
@group(1) @binding(0) var<uniform> model_matrices: ModelMatrices;

struct VertexInput {
    @builtin(vertex_index) in_vertex_index: u32,
    @location(0) pos: vec3<f32>,
    @location(1) tex_coords: vec2<f32>,
    @location(2) normal: vec3<f32>,
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) tex_coords: vec2<f32>,
    @location(1) world_normal: vec3<f32>,
    @location(2) world_position: vec3<f32>,
}

@vertex
fn vs_main(
    model : VertexInput,
) -> VertexOutput {
    var out: VertexOutput;
    out.tex_coords = model.tex_coords;
    out.world_normal = normalize((model_matrices.normal_matrix * vec4<f32>(model.normal, 0.0)).xyz);

    var world_position: vec4<f32> = model_matrices.model_matrix * vec4<f32>(model.pos, 1.0);
    out.world_position = world_position.xyz;
    out.position = camera.view_proj * world_position;
    return out;
}

@group(2) @binding(0) var myTexture: texture_2d<f32>;
@group(2) @binding(1) var mySampler: sampler;
@group(2) @binding(2) var metallicRoughnessTexture: texture_2d<f32>;
@group(2) @binding(3) var normalTexture: texture_2d<f32>;
@group(2) @binding(4) var emissiveTexture: texture_2d<f32>;
struct MaterialParams {
    base_color_factor: vec4<f32>,
    emissive_factor_metallic: vec4<f32>,
    roughness_occlusion_alpha_cutoff_flags: vec4<f32>,
}
@group(2) @binding(5) var<uniform> material: MaterialParams;

@group(3) @binding(0) var shadowMap: texture_depth_2d_array;
@group(3) @binding(1) var shadowSampler: sampler_comparison;

fn get_cascade_index(camera_space_z: f32) -> u32 {
    for (var i: u32 = 0u; i < 4u; i++) {
        if (camera_space_z < light.cascades[i].split_depth) {
            return i;
        }
    }
    return 4u;
}

override shadowDepthTextureSize: f32 = 2048.0;

const cascade_colour_modulator: array<vec3<f32>, 4> = array<vec3<f32>, 4>(
    vec3<f32>(1.5, 0.5, 0.5),
    vec3<f32>(0.5, 1.5, 0.5),
    vec3<f32>(0.5, 0.5, 1.5),
    vec3<f32>(1.5, 0.5, 1.5)
);

fn distribution_ggx(n: vec3<f32>, h: vec3<f32>, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let n_dot_h = max(dot(n, h), 0.0);
    let n_dot_h2 = n_dot_h * n_dot_h;
    let denom = (n_dot_h2 * (a2 - 1.0) + 1.0);
    return a2 / max(3.14159265 * denom * denom, 0.0001);
}

fn geometry_schlick_ggx(n_dot_v: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return n_dot_v / max(n_dot_v * (1.0 - k) + k, 0.0001);
}

fn geometry_smith(n: vec3<f32>, v: vec3<f32>, l: vec3<f32>, roughness: f32) -> f32 {
    let n_dot_v = max(dot(n, v), 0.0);
    let n_dot_l = max(dot(n, l), 0.0);
    return geometry_schlick_ggx(n_dot_v, roughness) * geometry_schlick_ggx(n_dot_l, roughness);
}

fn fresnel_schlick(cos_theta: f32, f0: vec3<f32>) -> vec3<f32> {
    return f0 + (vec3<f32>(1.0) - f0) * pow(1.0 - cos_theta, 5.0);
}

fn cotangent_frame(n: vec3<f32>, p: vec3<f32>, uv: vec2<f32>) -> mat3x3<f32> {
    let dp1 = dpdx(p);
    let dp2 = dpdy(p);
    let duv1 = dpdx(uv);
    let duv2 = dpdy(uv);

    let dp2perp = cross(dp2, n);
    let dp1perp = cross(n, dp1);
    let t = dp2perp * duv1.x + dp1perp * duv2.x;
    let b = dp2perp * duv1.y + dp1perp * duv2.y;
    let invmax = inverseSqrt(max(dot(t, t), dot(b, b)));
    return mat3x3<f32>(t * invmax, b * invmax, n);
}

@fragment
fn fs_main(
    in: VertexOutput,
) -> @location(0) vec4<f32> {
    let base_color_sample = textureSample(myTexture, mySampler, in.tex_coords);
    let base_color = base_color_sample * material.base_color_factor;
    let material_flags = u32(material.roughness_occlusion_alpha_cutoff_flags.w + 0.5);
    let is_alpha_mask = (material_flags & 1u) != 0u;
    let has_metallic_roughness_texture = (material_flags & 2u) != 0u;
    let has_normal_texture = (material_flags & 4u) != 0u;
    let has_emissive_texture = (material_flags & 8u) != 0u;
    if (is_alpha_mask && base_color.a < material.roughness_occlusion_alpha_cutoff_flags.z) {
        discard;
    }

    let metallic_roughness_sample = textureSample(metallicRoughnessTexture, mySampler, in.tex_coords);
    let roughness = select(
        1.0,
        clamp(material.roughness_occlusion_alpha_cutoff_flags.x * metallic_roughness_sample.g, 0.045, 1.0),
        has_metallic_roughness_texture
    );
    let metallic = select(
        0.0,
        clamp(material.emissive_factor_metallic.w * metallic_roughness_sample.b, 0.0, 1.0),
        has_metallic_roughness_texture
    );

    var n = normalize(in.world_normal);
    if (has_normal_texture) {
        let normal_sample = textureSample(normalTexture, mySampler, in.tex_coords).xyz * 2.0 - vec3<f32>(1.0);
        n = normalize(cotangent_frame(n, in.world_position, in.tex_coords) * normal_sample);
    }
    let v = normalize(camera.pos.xyz - in.world_position);
    let l = normalize(light.pos.xyz - in.world_position);
    let h = normalize(v + l);
    let n_dot_l = max(dot(n, l), 0.0);
    let n_dot_v = max(dot(n, v), 0.0);

    let albedo = base_color.xyz;
    let f0 = mix(vec3<f32>(0.04), albedo, metallic);
    let ndf = distribution_ggx(n, h, roughness);
    let g = geometry_smith(n, v, l, roughness);
    let f = fresnel_schlick(max(dot(h, v), 0.0), f0);
    let numerator = ndf * g * f;
    let denominator = max(4.0 * n_dot_v * n_dot_l, 0.0001);
    let specular = numerator / denominator;
    let k_d = (vec3<f32>(1.0) - f) * (1.0 - metallic);
    let diffuse = (k_d * albedo) / 3.14159265;

    let camera_to_fragment = in.world_position - camera.pos.xyz;
    let camera_depth = dot(camera_to_fragment, normalize(camera.forward.xyz));
    let cascade_idx = get_cascade_index(camera_depth);

    let kernelSize: i32 = 1;
    let weightTotal: f32 = f32(kernelSize * 2 + 1) * f32(kernelSize * 2 + 1);

    var visibility = 1.0;
    if (cascade_idx < 4u) {
        let shadowCoord = light.cascades[cascade_idx].view_proj * vec4<f32>(in.world_position, 1.0);
        let projCoords = shadowCoord.xyz / shadowCoord.w;
        let shadow_uv = projCoords.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5);
        let shadow_depth = projCoords.z;
        if (shadow_uv.x >= 0.0 && shadow_uv.x <= 1.0 && shadow_uv.y >= 0.0 && shadow_uv.y <= 1.0 && shadow_depth >= 0.0 && shadow_depth <= 1.0) {
            visibility = 0.0;
            let oneOverShadowDepthTextureSize = 1.0 / shadowDepthTextureSize;
            for (var y = -kernelSize; y <= kernelSize; y++) {
                for (var x = -kernelSize; x <= kernelSize; x++) {
                    let offset = vec2<f32>(vec2(x, y)) * oneOverShadowDepthTextureSize;
                    visibility += textureSampleCompareLevel(
                        shadowMap,
                        shadowSampler,
                        shadow_uv + offset,
                        i32(cascade_idx),
                        shadow_depth - (0.001 + 0.001 * f32(cascade_idx))
                    );
                }
            }
            visibility /= weightTotal;
        }
    }

    let direct_lighting = (diffuse + specular) * n_dot_l * visibility;
    let ambient = vec3<f32>(0.03) * albedo * material.roughness_occlusion_alpha_cutoff_flags.y;
    var emissive = vec3<f32>(0.0);
    if (has_emissive_texture) {
        emissive = material.emissive_factor_metallic.xyz * textureSample(emissiveTexture, mySampler, in.tex_coords).xyz;
    }
    var color = ambient + direct_lighting + emissive;
    color = color / (color + vec3<f32>(1.0));
    color = pow(color, vec3<f32>(1.0 / 2.2));

    return vec4<f32>(color, base_color.a);
}
