// FellowEngine benchmark tool: software depth rasteriser for offline occlusion-culling estimates.
// Rasterises indexed triangles (world space) for a list of cameras and writes, per camera, the nearest view depth
// (clip w) of every pixel (+inf where nothing was drawn). Near-plane clipping, no backface culling (conservative:
// occluders are never missed because of their winding).
//
// usage: occlusion_raster <positions.f32> <indices.u32> <cams.f32> <width> <height> <near> <out.f32>
//   positions.f32: float32 x,y,z per vertex      indices.u32: uint32, 3 per triangle
//   cams.f32: per camera 16 float32, view-projection, row-major (clip = M * [x y z 1], clip.w = view depth)
//   out.f32: float32 [ncam][height][width]
// build: cc -O3 -march=native -o occlusion_raster occlusion_raster.c -lm

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	float x, y, z, w;
} V4;

static void *load(const char *path, size_t *size) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		perror(path);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	*size = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	void *p = malloc(*size);
	if (fread(p, 1, *size, f) != *size) {
		perror("read");
		exit(1);
	}
	fclose(f);
	return p;
}

static int W, H;
static float *depth;

static void raster_tri(V4 a, V4 b, V4 c) {
	// Perspective divide to pixel coordinates (y down), keep 1/w for depth interpolation.
	float iwa = 1.0f / a.w, iwb = 1.0f / b.w, iwc = 1.0f / c.w;
	float ax = (a.x * iwa * 0.5f + 0.5f) * W, ay = (0.5f - a.y * iwa * 0.5f) * H;
	float bx = (b.x * iwb * 0.5f + 0.5f) * W, by = (0.5f - b.y * iwb * 0.5f) * H;
	float cx = (c.x * iwc * 0.5f + 0.5f) * W, cy = (0.5f - c.y * iwc * 0.5f) * H;
	float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
	if (fabsf(area) < 1e-12f) {
		return;
	}
	float minx = fminf(ax, fminf(bx, cx)), maxx = fmaxf(ax, fmaxf(bx, cx));
	float miny = fminf(ay, fminf(by, cy)), maxy = fmaxf(ay, fmaxf(by, cy));
	int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > W - 1) x1 = W - 1;
	if (y1 > H - 1) y1 = H - 1;
	if (x0 > x1 || y0 > y1) {
		return;
	}
	float inv_area = 1.0f / area;
	for (int y = y0; y <= y1; y++) {
		float py = y + 0.5f;
		for (int x = x0; x <= x1; x++) {
			float px = x + 0.5f;
			float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) * inv_area;
			float w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) * inv_area;
			float w2 = 1.0f - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0) {
				continue;
			}
			float iw = w0 * iwa + w1 * iwb + w2 * iwc;
			float d = 1.0f / iw;
			float *dst = &depth[(size_t)y * W + x];
			if (d < *dst) {
				*dst = d;
			}
		}
	}
}

static V4 lerp4(V4 a, V4 b, float t) {
	V4 r = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
	return r;
}

int main(int argc, char **argv) {
	if (argc != 8) {
		fprintf(stderr, "usage: %s positions.f32 indices.u32 cams.f32 width height near out.f32\n", argv[0]);
		return 1;
	}
	size_t psize, isize, csize;
	float *pos = (float *)load(argv[1], &psize);
	uint32_t *idx = (uint32_t *)load(argv[2], &isize);
	float *cams = (float *)load(argv[3], &csize);
	W = atoi(argv[4]);
	H = atoi(argv[5]);
	float znear = (float)atof(argv[6]);
	size_t nv = psize / 12, nt = isize / 12, nc = csize / 64;
	FILE *out = fopen(argv[7], "wb");
	depth = (float *)malloc(sizeof(float) * W * H);
	V4 *clip = (V4 *)malloc(sizeof(V4) * nv);
	for (size_t c = 0; c < nc; c++) {
		const float *m = cams + c * 16;
		for (size_t v = 0; v < nv; v++) {
			float x = pos[v * 3], y = pos[v * 3 + 1], z = pos[v * 3 + 2];
			clip[v].x = m[0] * x + m[1] * y + m[2] * z + m[3];
			clip[v].y = m[4] * x + m[5] * y + m[6] * z + m[7];
			clip[v].z = m[8] * x + m[9] * y + m[10] * z + m[11];
			clip[v].w = m[12] * x + m[13] * y + m[14] * z + m[15];
		}
		for (int i = 0; i < W * H; i++) {
			depth[i] = INFINITY;
		}
		for (size_t t = 0; t < nt; t++) {
			V4 p[3] = { clip[idx[t * 3]], clip[idx[t * 3 + 1]], clip[idx[t * 3 + 2]] };
			// Trivial reject against the side planes and the near plane.
			int out_l = 0, out_r = 0, out_b = 0, out_t = 0, out_n = 0;
			for (int k = 0; k < 3; k++) {
				out_l += p[k].x < -p[k].w;
				out_r += p[k].x > p[k].w;
				out_b += p[k].y < -p[k].w;
				out_t += p[k].y > p[k].w;
				out_n += p[k].w < znear;
			}
			if (out_l == 3 || out_r == 3 || out_b == 3 || out_t == 3 || out_n == 3) {
				continue;
			}
			if (out_n == 0) {
				raster_tri(p[0], p[1], p[2]);
				continue;
			}
			// Clip against w = near (Sutherland-Hodgman), fan-triangulate.
			V4 poly[4];
			int n = 0;
			for (int k = 0; k < 3; k++) {
				V4 a = p[k], b = p[(k + 1) % 3];
				int ina = a.w >= znear, inb = b.w >= znear;
				if (ina) {
					poly[n++] = a;
				}
				if (ina != inb) {
					poly[n++] = lerp4(a, b, (znear - a.w) / (b.w - a.w));
				}
			}
			for (int k = 1; k + 1 < n; k++) {
				raster_tri(poly[0], poly[k], poly[k + 1]);
			}
		}
		fwrite(depth, sizeof(float), (size_t)W * H, out);
		fprintf(stderr, "camera %zu/%zu\n", c + 1, nc);
	}
	fclose(out);
	return 0;
}
