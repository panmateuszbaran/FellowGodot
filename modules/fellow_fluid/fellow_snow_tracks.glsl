#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine snow tracks (FellowRain3D, decision 0130): how deep the snow is pressed down, in metres (negative: a rim
// pushed up beside a track), in a texture anchored to the world and wrapping around (texel i holds the world cell
// c = i mod size, c within the window around the camera). Each dispatch covers a rectangle of cells (`region`): the
// strips the window entered as it followed the camera, the stamps' bounds, or (when falling snow fills the tracks
// back) the whole window. For each cell:
// * a texel that held another cell before starts again as fresh snow;
// * the tracks fill back by `refill` metres (falling snow), the rims settle;
// * the stamps: capsules (a segment and a radius) pressed in to their depth, a rim around them; a tracker's stamp only
//   where it touches the scene's top (the shelter heightfield) within its contact height.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(r16f, set = 0, binding = 0) uniform restrict image2D tracks;
layout(set = 0, binding = 1) uniform texture2D occlusion;
layout(set = 0, binding = 2) uniform sampler occlusion_sampler;

struct Stamp {
	vec4 segment; // x0, z0, x1, z1 (m)
	vec4 shape; // radius (m), depth (m), the tracker's height (m), contact height (m; < 0: always)
};

layout(set = 0, binding = 3, std430) restrict readonly buffer Stamps {
	Stamp data[];
}
stamps;

layout(push_constant, std430) uniform Params {
	ivec2 origin; // the window's centre, in cells
	ivec2 prev_origin; // the window the texels held before this dispatch
	ivec2 region_lo; // the first cell of this dispatch's rectangle
	ivec2 region_size; // its size in cells
	int size; // texels (a power of two)
	int stamp_count;
	float cell; // m
	float refill; // m this frame
	vec2 occ_origin; // the heightfield's centre, x and z (m)
	vec2 occ_inv; // 1 / its side, x and z (1/m)
	float occ_y; // its centre's height (m)
	float occ_half; // half its height (m)
	float occ_enabled;
	float pad;
}
params;

ivec2 cell_of(ivec2 p_texel, ivec2 p_origin) {
	ivec2 lo = p_origin - ivec2(params.size / 2);
	return lo + ((p_texel - lo) & ivec2(params.size - 1));
}

// The scene's top as the snow sees it (decision 0128): the heightfield eroded by 35 cm, the lowest of five points, so
// that a wire or a lamp overhead does not lift it.
float scene_top(vec2 p_xz) {
	vec2 uv = (p_xz - params.occ_origin) * params.occ_inv + 0.5;
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
		return -1e6;
	}
	vec2 r = 0.35 * params.occ_inv;
	float h = textureLod(sampler2D(occlusion, occlusion_sampler), uv, 0.0).r;
	h = min(h, textureLod(sampler2D(occlusion, occlusion_sampler), uv + vec2(r.x, 0.0), 0.0).r);
	h = min(h, textureLod(sampler2D(occlusion, occlusion_sampler), uv - vec2(r.x, 0.0), 0.0).r);
	h = min(h, textureLod(sampler2D(occlusion, occlusion_sampler), uv + vec2(0.0, r.y), 0.0).r);
	h = min(h, textureLod(sampler2D(occlusion, occlusion_sampler), uv - vec2(0.0, r.y), 0.0).r);
	return params.occ_y + (h * 2.0 - 1.0) * params.occ_half;
}

void main() {
	ivec2 g = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(g, params.region_size))) {
		return;
	}
	ivec2 cell = params.region_lo + g;
	ivec2 t = cell & ivec2(params.size - 1);
	float h = imageLoad(tracks, t).r;
	if (cell != cell_of(t, params.prev_origin)) {
		h = 0.0; // a cell new to the window: fresh snow
	}
	// Falling snow fills the tracks back and settles the rims.
	h = h > 0.0 ? max(0.0, h - params.refill) : min(0.0, h + params.refill * 0.5);

	vec2 p = (vec2(cell) + 0.5) * params.cell;
	for (int i = 0; i < params.stamp_count; i++) {
		Stamp s = stamps.data[i];
		vec2 a = s.segment.xy;
		vec2 b = s.segment.zw;
		float r = s.shape.x;
		// Cheap rejection: beyond the capsule's rim.
		vec2 ab = b - a;
		float len2 = dot(ab, ab);
		float u = len2 > 1e-8 ? clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
		float x = length(p - (a + ab * u)) / r;
		if (x >= 1.5) {
			continue;
		}
		if (s.shape.w >= 0.0 && params.occ_enabled > 0.5) {
			// A tracker presses only where it touches the scene's top.
			float top = scene_top(a + ab * u);
			if (abs(s.shape.z - top) > s.shape.w) {
				continue;
			}
		}
		float d = s.shape.y;
		if (x < 1.0) {
			// Pressed in, its walls sloping over the outer half (a cell is a few centimetres: steeper walls step).
			h = max(h, d * smoothstep(1.0, 0.45, x));
		} else if (h <= 0.0) {
			// The rim: the snow pushed aside, a quarter of the depth.
			h = min(h, -0.25 * d * sin((x - 1.0) * (3.14159265 / 0.5)));
		}
	}
	imageStore(tracks, t, vec4(h, 0.0, 0.0, 0.0));
}
