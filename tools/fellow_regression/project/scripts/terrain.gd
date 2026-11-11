extends Node3D
# Built the same every run (milestone 57): a FellowTerrain3D of 257 x 257 heights at 1 m (ridges and a valley from
# fixed noise), four layers (grass, triplanar rock on the slopes, sand low down, snow high up; two with a checker
# texture, height in its alpha), a short level-of-detail distance so that five levels show in the view.


func _checker(a: Color, b: Color) -> ImageTexture:
	var img := Image.create(64, 64, true, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			var c := a if ((x / 8) + (y / 8)) % 2 == 0 else b
			c.a = 0.25 + 0.5 * float((x * 7 + y * 3) % 16) / 15.0
			img.set_pixel(x, y, c)
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


func _ready() -> void:
	var noise := FastNoiseLite.new()
	noise.seed = 57
	noise.frequency = 0.012
	noise.fractal_octaves = 4
	var n := 257
	var h := PackedFloat32Array()
	h.resize(n * n)
	for z in n:
		for x in n:
			var ridge := 1.0 - absf(noise.get_noise_2d(x * 0.6 + 100.0, z * 0.6))
			h[z * n + x] = noise.get_noise_2d(x, z) * 8.0 + ridge * ridge * 14.0 + absf(x - 128.0) * 0.06
	var splat := Image.create(n, n, false, Image.FORMAT_RGBA8)
	for z in n:
		for x in n:
			var hx := h[z * n + mini(x + 1, n - 1)] - h[z * n + maxi(x - 1, 0)]
			var hz := h[mini(z + 1, n - 1) * n + x] - h[maxi(z - 1, 0) * n + x]
			var slope := sqrt(hx * hx + hz * hz) * 0.5
			var y := h[z * n + x]
			var rock := clampf((slope - 0.5) * 3.0, 0.0, 1.0)
			var sand := clampf((2.0 - y) * 0.5, 0.0, 1.0) * (1.0 - rock)
			var snow := clampf((y - 16.0) * 0.3, 0.0, 1.0) * (1.0 - rock)
			splat.set_pixel(x, z, Color(rock, sand, snow, 0.0))
	var t = ClassDB.instantiate("FellowTerrain3D")
	var layers := []
	for c in [Color(0.3, 0.45, 0.2), Color(0.5, 0.48, 0.45), Color(0.75, 0.68, 0.5), Color(0.92, 0.94, 0.97)]:
		var l = ClassDB.instantiate("FellowTerrainLayer")
		l.albedo_color = c # sRGB, as a material's
		l.roughness = 0.85
		layers.append(l)
	layers[0].albedo_texture = _checker(Color(0.85, 0.9, 0.8), Color(0.6, 0.7, 0.55))
	layers[0].texture_size = 3.0
	layers[1].albedo_texture = _checker(Color(0.9, 0.88, 0.85), Color(0.55, 0.52, 0.5))
	layers[1].height_in_albedo_alpha = true
	layers[1].triplanar = true
	layers[1].texture_size = 5.0
	t.layers = layers
	t.heightmap = Image.create_from_data(n, n, false, Image.FORMAT_RF, h.to_byte_array())
	t.splat_map = splat
	t.lod_distance = 20.0
	t.position = Vector3(-128, 0, -200)
	add_child(t)
