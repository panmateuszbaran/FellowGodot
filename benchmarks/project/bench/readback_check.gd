extends SceneTree
## RenderingDevice's uploads and downloads read back byte for byte (milestones 118-119, decisions 0234 and 0236).
## 30 textures (BC7, BC1, BC4, BC5, RGBA8, R8; 4x4 to 4096x2048, square and not, every mip) created with data, read back,
## updated and read back again (texture_get_data()); then every one read back asynchronously in one frame
## (texture_get_data_async()); then a 144 MiB buffer and two 4096x4096 RGBA8 textures read back asynchronously, more than
## the download staging ring (128 MiB by default) holds. Prints one JSON line; "ok" is every byte the same.
##   godot --path benchmarks/project --rendering-driver vulkan -s res://bench/readback_check.gd

const CASES := [
	["BC7", RenderingDevice.DATA_FORMAT_BC7_UNORM_BLOCK, 4, 4, 16],
	["BC1", RenderingDevice.DATA_FORMAT_BC1_RGBA_UNORM_BLOCK, 4, 4, 8],
	["BC4", RenderingDevice.DATA_FORMAT_BC4_UNORM_BLOCK, 4, 4, 8],
	["BC5", RenderingDevice.DATA_FORMAT_BC5_UNORM_BLOCK, 4, 4, 16],
	["RGBA8", RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM, 1, 1, 4],
	["R8", RenderingDevice.DATA_FORMAT_R8_UNORM, 1, 1, 1],
]
const SIZES := [[256, 256], [100, 60], [1024, 512], [4, 4], [4096, 2048]]
const USAGE := RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT | RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT

var result := {
	"sync_checked": 0, "sync_bad": [], "staging_after_bc7_4096": -1,
	"async_checked": 0, "async_bad": [], "split_checked": 0, "split_bad": [],
}
var kept := [] # [name, texture, expected bytes]
var big := [] # the same, more than the staging ring
var buffer: RID
var buffer_data: PackedByteArray
var pending := 0
var frames := 0


func _noise(n: int, seed_value: int) -> PackedByteArray:
	var b := PackedByteArray()
	b.resize(n)
	var x := seed_value
	for i in n:
		x = (x * 1103515245 + 12345) & 0x7fffffff
		b[i] = (x >> 16) & 0xff
	return b


# A 1 MiB block of noise repeated, each copy starting with its index: a part misplaced by less than a MiB meets other
# noise, by whole MiBs another index.
func _big_noise(n: int, seed_value: int) -> PackedByteArray:
	var block := _noise(1 << 20, seed_value)
	var b := PackedByteArray()
	for c in (n + block.size() - 1) / block.size():
		block.encode_u32(0, c)
		b.append_array(block)
	b.resize(n)
	return b


func _chain_size(w: int, h: int, mips: int, bw: int, bh: int, block_bytes: int) -> int:
	var total := 0
	for m in mips:
		total += ceili(maxi(1, w >> m) / float(bw)) * ceili(maxi(1, h >> m) / float(bh)) * block_bytes
	return total


func _difference(data: PackedByteArray, expected: PackedByteArray) -> String:
	var first := -1
	var count := 0
	for i in mini(data.size(), expected.size()):
		if data[i] != expected[i]:
			count += 1
			if first < 0:
				first = i
	return "%d/%d bytes, %d differ from %d" % [data.size(), expected.size(), count, first]


func _texture_format(format: int, w: int, h: int, mips: int) -> RDTextureFormat:
	var tf := RDTextureFormat.new()
	tf.format = format
	tf.width = w
	tf.height = h
	tf.mipmaps = mips
	tf.usage_bits = USAGE
	return tf


func _initialize() -> void:
	var rd := RenderingServer.get_rendering_device()
	for c in CASES:
		if not rd.texture_is_format_supported_for_usage(c[1], USAGE):
			continue
		for s in SIZES:
			var w: int = s[0]
			var h: int = s[1]
			var mips := int(floor(log(maxi(w, h)) / log(2.0))) + 1
			var n := _chain_size(w, h, mips, c[2], c[3], c[4])
			var name := "%s %dx%d" % [c[0], w, h]
			var data := _noise(n, w * 7 + h + c[1])
			var tex := rd.texture_create(_texture_format(c[1], w, h, mips), RDTextureView.new(), [data])
			var back := rd.texture_get_data(tex, 0)
			var data2 := _noise(n, w * 13 + h * 3 + c[1])
			rd.texture_update(tex, 0, data2)
			var back2 := rd.texture_get_data(tex, 0)
			result.sync_checked += 1
			if back != data:
				result.sync_bad.append(name + " created: " + _difference(back, data))
			if back2 != data2:
				result.sync_bad.append(name + " updated: " + _difference(back2, data2))
			if name == "BC7 4096x2048" and RenderingServer.has_method("fellow_get_stats"):
				result.staging_after_bc7_4096 = RenderingServer.call("fellow_get_stats").get("transfer_staging_bytes", -1)
			kept.append([name, tex, data2])
	for k in 2:
		var n := _chain_size(4096, 4096, 13, 1, 1, 4)
		var data := _big_noise(n, 1000 + k)
		big.append(["RGBA8 4096x4096 #%d" % k, rd.texture_create(_texture_format(RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM, 4096, 4096, 13), RDTextureView.new(), [data]), data])
	buffer_data = _big_noise(144 << 20, 77)
	buffer = rd.storage_buffer_create(buffer_data.size(), buffer_data)


func _received(data: PackedByteArray, name: String, expected: PackedByteArray, checked_key: String, bad_key: String) -> void:
	pending -= 1
	result[checked_key] += 1
	if data != expected:
		result[bad_key].append(name + ": " + _difference(data, expected))


func _process(_delta: float) -> bool:
	frames += 1
	var rd := RenderingServer.get_rendering_device()
	if frames == 2:
		for k in kept:
			pending += 1
			rd.texture_get_data_async(k[1], 0, _received.bind(k[0], k[2], "async_checked", "async_bad"))
	if frames == 4:
		pending += 1
		rd.buffer_get_data_async(buffer, _received.bind("buffer 144 MiB", buffer_data, "split_checked", "split_bad"))
	if frames == 7:
		for k in big:
			pending += 1
			rd.texture_get_data_async(k[1], 0, _received.bind(k[0], k[2], "split_checked", "split_bad"))
	if (frames > 7 and pending == 0) or frames > 120:
		result.pending = pending
		result.ok = pending == 0 and result.sync_bad.is_empty() and result.async_bad.is_empty() and result.split_bad.is_empty() and result.sync_checked > 0
		for k in kept + big:
			rd.free_rid(k[1])
		rd.free_rid(buffer)
		print(JSON.stringify(result))
		quit(0 if result.ok else 1)
		return true
	return false
