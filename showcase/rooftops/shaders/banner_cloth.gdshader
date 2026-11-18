// FellowEngine showcase (rooftops): cloth — a banner's field with its bands and border, or plain awning stripes;
// both sides drawn, the light through it from behind.
shader_type spatial;
render_mode cull_disabled, diffuse_burley, specular_schlick_ggx;

#include "rooftops_common.gdshaderinc"

uniform vec3 field : source_color = vec3(0.55, 0.08, 0.07);
uniform vec3 band : source_color = vec3(0.85, 0.66, 0.25);
uniform int pattern = 0; // 0: a banner (a border, a central band, a lozenge), 1: stripes along u
uniform float stripe_width = 0.25;
uniform vec2 size = vec2(1.0, 3.0); // the cloth's size (m), its UVs 0..1

void fragment() {
	vec2 p = UV * size;
	vec3 col = field;
	if (pattern == 0) {
		float border = step(min(min(p.x, size.x - p.x), p.y), 0.07);
		float mid = step(abs(p.x - size.x * 0.5), size.x * 0.09);
		vec2 c = p - vec2(size.x * 0.5, size.y * 0.32);
		float loz = step(abs(c.x) / (size.x * 0.32) + abs(c.y) / (size.x * 0.45), 1.0);
		float loz_in = step(abs(c.x) / (size.x * 0.22) + abs(c.y) / (size.x * 0.33), 1.0);
		col = mix(col, band, max(border, mid * (1.0 - loz)));
		col = mix(col, band, loz - loz_in);
		col = mix(col, field * 0.6 + band * 0.15, loz_in);
		// The swallowtail's darker hem.
		col *= 1.0 - 0.2 * step(size.y - 0.06, p.y);
	} else {
		col = mix(field, band, step(0.5, fract(p.x / stripe_width)));
	}
	float weave = 0.5 + 0.5 * sin(p.x * 900.0) * sin(p.y * 900.0);
	col *= 0.9 + 0.1 * rt_fbm(p * 3.0, 3) + 0.03 * weave;
	ALBEDO = col;
	ROUGHNESS = 0.9;
	SPECULAR = 0.25;
	BACKLIGHT = col * 0.6;
}
