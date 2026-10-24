// FellowEngine dynamic diffuse global illumination (docs/decisions/0083-ddgi.md): the probe volume's addressing and its
// irradiance lookup, shared by the probe update (effects/fellow_ddgi.glsl) and the scene shader. Needs oct_inc.glsl.
//
// The volume is a grid of probes counts.xyz around the camera, spacing.x apart, at world positions cell * spacing. It
// scrolls with the camera: the cells in view are base_cell .. base_cell + counts - 1, and a cell's probe lives in slot
// cell mod counts (its data stays when the volume moves; a slot that receives a new cell starts over). Each probe has an
// octahedral irradiance tile (8 x 8 texels with a one-texel border, RGB the cosine-weighted mean radiance — the "ambient
// light" the scene shader multiplies by albedo —, A 1 if the probe is active) and a distance tile (16 x 16, mean and
// mean square of the distance to the first hit around each direction, for the visibility test).

struct FellowDDGIVolume {
	ivec4 counts; // xyz probes, w rays per probe
	ivec4 base_cell; // xyz the first cell in the volume, w 1 when the volume holds data
	vec4 spacing; // x spacing, y normal bias, z view bias (in spacings), w energy multiplier
	vec4 atlas_inv_size; // xy irradiance atlas 1 / size, zw distance atlas 1 / size
};

#define FELLOW_DDGI_IRRADIANCE_TILE 8
#define FELLOW_DDGI_DISTANCE_TILE 16

ivec3 fellow_ddgi_slot(ivec3 p_cell, ivec3 p_counts) {
	return ((p_cell % p_counts) + p_counts) % p_counts;
}

// The texel origin of a slot's tile in an atlas of tiles p_tile texels wide: slots x + y * counts.x across, z down.
ivec2 fellow_ddgi_tile_origin(ivec3 p_slot, ivec3 p_counts, int p_tile) {
	return ivec2((p_slot.x + p_slot.y * p_counts.x) * p_tile, p_slot.z * p_tile);
}

vec2 fellow_ddgi_tile_uv(ivec3 p_slot, ivec3 p_counts, int p_tile, vec3 p_dir, vec2 p_inv_size) {
	float t = float(p_tile);
	vec2 uv = vec3_to_oct_with_border(p_dir, vec2(1.0 / t, (t - 2.0) / t)); // within the tile, its border included
	return (vec2(fellow_ddgi_tile_origin(p_slot, p_counts, p_tile)) + uv * t) * p_inv_size;
}

// The irradiance ("ambient light", before albedo) at world position p_pos with normal p_normal, p_to_eye the unit vector
// towards the viewer (or the ray origin). r_coverage: the share of the trilinear weights of probes in the volume (0
// outside it), for a blend with the ambient light it replaces.
vec3 fellow_ddgi_irradiance(texture2D p_irradiance, texture2D p_distance, sampler p_sampler, FellowDDGIVolume p_volume, vec3 p_pos, vec3 p_normal, vec3 p_to_eye, out float r_coverage) {
	r_coverage = 0.0;
	if (p_volume.base_cell.w == 0) {
		return vec3(0.0);
	}
	float spacing = p_volume.spacing.x;
	ivec3 counts = p_volume.counts.xyz;
	vec3 biased = p_pos + (p_normal * p_volume.spacing.y + p_to_eye * p_volume.spacing.z) * spacing;
	vec3 g = biased / spacing;
	ivec3 c0 = ivec3(floor(g));
	vec3 a = g - vec3(c0);
	vec3 sum = vec3(0.0);
	float wsum = 0.0;
	float coverage = 0.0;
	for (int i = 0; i < 8; i++) {
		ivec3 o = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
		ivec3 cell = c0 + o;
		if (any(lessThan(cell, p_volume.base_cell.xyz)) || any(greaterThanEqual(cell, p_volume.base_cell.xyz + counts))) {
			continue;
		}
		ivec3 slot = fellow_ddgi_slot(cell, counts);
		vec3 tri3 = mix(1.0 - a, a, vec3(o));
		float tri = tri3.x * tri3.y * tri3.z;
		vec4 irr = textureLod(sampler2D(p_irradiance, p_sampler), fellow_ddgi_tile_uv(slot, counts, FELLOW_DDGI_IRRADIANCE_TILE, p_normal, p_volume.atlas_inv_size.xy), 0.0);
		if (irr.a < 0.5) {
			continue; // inactive (inside geometry) or not traced yet
		}
		coverage += tri;
		vec3 probe = vec3(cell) * spacing;
		vec3 to_probe = normalize(probe - p_pos);
		// Probes behind the surface weigh less (smoothly, so the lighting has no seams).
		float wrap = (dot(to_probe, p_normal) + 1.0) * 0.5;
		float w = wrap * wrap + 0.2;
		// Visibility: Chebyshev's bound from the distances the probe saw towards the (biased) point.
		vec3 from_probe = biased - probe;
		float d = length(from_probe);
		vec2 m = textureLod(sampler2D(p_distance, p_sampler), fellow_ddgi_tile_uv(slot, counts, FELLOW_DDGI_DISTANCE_TILE, from_probe / max(d, 1e-4), p_volume.atlas_inv_size.zw), 0.0).rg;
		if (d > m.x) {
			float variance = abs(m.y - m.x * m.x);
			float dd = d - m.x;
			float cheb = variance / (variance + dd * dd);
			w *= max(cheb * cheb * cheb, 0.0);
		}
		w = max(w, 1e-6);
		// Weights below 0.2 crushed: light leaking through thin walls fades faster.
		if (w < 0.2) {
			w *= w * w * 25.0;
		}
		w *= tri;
		sum += sqrt(max(irr.rgb, vec3(0.0))) * w; // interpolated in a perceptual space (DDGI's gamma), squared after
		wsum += w;
	}
	// Where every active probe around the point is hidden from it (their weights crushed), the normalized sum would be
	// whatever the least hidden one holds — often a probe in a sealed void or behind a wall, black: blend towards the
	// ambient light instead, by how visible the probes are (an unoccluded probe weighs at least 0.2 of its trilinear
	// weight).
	r_coverage = coverage * clamp(wsum / max(0.2 * coverage, 1e-6), 0.0, 1.0);
	if (wsum <= 0.0) {
		return vec3(0.0);
	}
	vec3 r = sum / wsum;
	return r * r * p_volume.spacing.w;
}
