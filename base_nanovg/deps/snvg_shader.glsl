/*
    NanoVG shader for sokol_nanovg.h

    Compile with:
    sokol-shdc -i snvg_shader.glsl -o snvg_shader.h -l glsl410:glsl300es:hlsl4:metal_macos:metal_ios:metal_sim:wgsl:spirv_vk -f sokol_impl -b
*/
@ctype mat4 snvg_mat4
@ctype vec4 snvg_vec4
@ctype vec2 snvg_vec2

@vs vs
layout(binding=0) uniform vs_params {
    vec2 viewSize;
};

in vec2 vertex;
in vec2 tcoord;

out vec2 ftcoord;
out vec2 fpos;

void main() {
    ftcoord = tcoord;
    fpos = vertex;
    gl_Position = vec4(2.0*vertex.x/viewSize.x - 1.0, 1.0 - 2.0*vertex.y/viewSize.y, 0, 1);
}
@end

@fs fs
layout(binding=1) uniform fs_params {
    // scissorMat is mat3, but we store it as 3 vec4s for alignment
    vec4 scissorMat0;  // row 0 (x, y, z, padding)
    vec4 scissorMat1;  // row 1 (x, y, z, padding)
    vec4 scissorMat2;  // row 2 (x, y, z, padding)
    // paintMat is mat3, stored as 3 vec4s
    vec4 paintMat0;    // row 0 (x, y, z, padding)
    vec4 paintMat1;    // row 1 (x, y, z, padding)
    vec4 paintMat2;    // row 2 (x, y, z, padding)
    vec4 innerCol;
    vec4 outerCol;
    vec4 scissorExtScale;  // xy = scissorExt, zw = scissorScale
    vec4 extentRadiusFeather;  // xy = extent, z = radius, w = feather
    vec4 params;  // x = strokeMult, y = strokeThr, z = texType, w = type
};

layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 ftcoord;
in vec2 fpos;

out vec4 outColor;

mat3 getScissorMat() {
    return mat3(scissorMat0.xyz, scissorMat1.xyz, scissorMat2.xyz);
}

mat3 getPaintMat() {
    return mat3(paintMat0.xyz, paintMat1.xyz, paintMat2.xyz);
}

float sdroundrect(vec2 pt, vec2 ext, float rad) {
    vec2 ext2 = ext - vec2(rad, rad);
    vec2 d = abs(pt) - ext2;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - rad;
}

float scissorMask(vec2 p) {
    mat3 scissorMat = getScissorMat();
    vec2 scissorExt = scissorExtScale.xy;
    vec2 scissorScale = scissorExtScale.zw;
    vec2 sc = (abs((scissorMat * vec3(p, 1.0)).xy) - scissorExt);
    sc = vec2(0.5, 0.5) - sc * scissorScale;
    return clamp(sc.x, 0.0, 1.0) * clamp(sc.y, 0.0, 1.0);
}

float strokeMask() {
    float strokeMult = params.x;
    return min(1.0, (1.0 - abs(ftcoord.x * 2.0 - 1.0)) * strokeMult) * min(1.0, ftcoord.y);
}

void main() {
    mat3 paintMat = getPaintMat();
    vec2 extent = extentRadiusFeather.xy;
    float radius = extentRadiusFeather.z;
    float feather = extentRadiusFeather.w;
    float strokeThr = params.y;
    int texType = int(params.z);
    int type = int(params.w);

    vec4 result;
    float scissor = scissorMask(fpos);
    float strokeAlpha = strokeMask();

    if (strokeAlpha < strokeThr) discard;

    if (type == 0) {  // Gradient
        vec2 pt = (paintMat * vec3(fpos, 1.0)).xy;
        float d = clamp((sdroundrect(pt, extent, radius) + feather * 0.5) / feather, 0.0, 1.0);
        vec4 color = mix(innerCol, outerCol, d);
        color *= strokeAlpha * scissor;
        result = color;
    } else if (type == 1) {  // Image
        vec2 pt = (paintMat * vec3(fpos, 1.0)).xy / extent;
        vec4 color = texture(sampler2D(tex, smp), pt);
        if (texType == 1) color = vec4(color.xyz * color.w, color.w);
        if (texType == 2) color = vec4(color.x);
        color *= innerCol;
        color *= strokeAlpha * scissor;
        result = color;
    } else if (type == 2) {  // Stencil fill
        result = vec4(1, 1, 1, 1);
    } else if (type == 3) {  // Textured tris
        vec4 color = texture(sampler2D(tex, smp), ftcoord);
        if (texType == 1) color = vec4(color.xyz * color.w, color.w);
        if (texType == 2) color = vec4(color.x);
        color *= scissor;
        result = color * innerCol;
    }

    outColor = result;
}
@end

@program snvg vs fs
