extends CanvasLayer
## Milestone 212 (the owner: "some interactions with the NPCs"): speech bubbles over the square's people — what one
## says to the player talking to them, a shout at someone climbing, at being run into — and the hint over the one the
## player can talk to ("E  Talk"). Each bubble follows its person (their head, `height` over their origin), smaller
## with the distance, hidden behind the camera or beyond MAX_DIST; it fades in, stays its time and fades out.

const MAX_DIST := 28.0 # m
const WRAP := 300.0 # px: longer lines wrap
const FADE := 0.18 # s

var _bubbles := {} # the anchor's instance id -> {anchor, panel, label, life, t, height, hint}
var _say_style: StyleBoxFlat
var _hint_style: StyleBoxFlat


func _ready() -> void:
	layer = 4
	_say_style = StyleBoxFlat.new()
	_say_style.bg_color = Color(0.985, 0.965, 0.92, 0.95)
	_say_style.set_corner_radius_all(12)
	_say_style.set_content_margin_all(9)
	_say_style.content_margin_left = 13
	_say_style.content_margin_right = 13
	_say_style.border_color = Color(0.32, 0.24, 0.16, 0.55)
	_say_style.set_border_width_all(1)
	_say_style.shadow_color = Color(0, 0, 0, 0.28)
	_say_style.shadow_size = 5
	_say_style.shadow_offset = Vector2(0, 2)
	_hint_style = _say_style.duplicate()
	_hint_style.bg_color = Color(0.1, 0.09, 0.08, 0.72)
	_hint_style.border_color = Color(1, 1, 1, 0.35)
	_hint_style.set_content_margin_all(6)
	_hint_style.content_margin_left = 10
	_hint_style.content_margin_right = 10


## Says p_text over p_anchor for p_seconds (0: until hushed); a bubble already there is replaced. p_hint: a small "E"
## hint after the text (more to come).
func say(p_anchor: Node3D, p_text: String, p_seconds: float, p_height := 2.0, p_more := false) -> void:
	_make(p_anchor, p_text, p_seconds, p_height, false, p_more)


## The hint over p_anchor (the one the player can talk to): "E  Talk".
func hint(p_anchor: Node3D, p_height := 2.0) -> void:
	var b: Dictionary = _bubbles.get(p_anchor.get_instance_id(), {})
	if not b.is_empty():
		if b.hint:
			b.life = 0.25
			b.height = p_height
		return
	_make(p_anchor, "E   Talk", 0.25, p_height, true, false)


func hush(p_anchor: Node3D) -> void:
	var b: Dictionary = _bubbles.get(p_anchor.get_instance_id(), {})
	if not b.is_empty() and b.life > FADE:
		b.life = FADE
		b.t = 0.0


func is_saying(p_anchor: Node3D) -> bool:
	var b: Dictionary = _bubbles.get(p_anchor.get_instance_id(), {})
	return not b.is_empty() and not b.hint


func _make(p_anchor: Node3D, p_text: String, p_seconds: float, p_height: float, p_hint: bool, p_more: bool) -> void:
	var id := p_anchor.get_instance_id()
	var old: Dictionary = _bubbles.get(id, {})
	if not old.is_empty():
		(old.panel as Control).queue_free()
		_bubbles.erase(id)
	var panel := PanelContainer.new()
	panel.add_theme_stylebox_override("panel", _hint_style if p_hint else _say_style)
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var label := Label.new()
	label.text = p_text + ("   ▸ E" if p_more else "")
	label.add_theme_font_size_override("font_size", 14 if p_hint else 17)
	label.add_theme_color_override("font_color", Color(1, 1, 1, 0.95) if p_hint else Color(0.13, 0.1, 0.08))
	var font := label.get_theme_font("font")
	var w := font.get_string_size(label.text, HORIZONTAL_ALIGNMENT_LEFT, -1, 17).x if font else 0.0
	if w > WRAP:
		label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		label.custom_minimum_size = Vector2(WRAP, 0.0)
	panel.add_child(label)
	if not p_hint:
		# (the tail, under the bubble's middle, pointing down at the speaker)
		var tail := Polygon2D.new()
		tail.polygon = PackedVector2Array([Vector2(-9, -1), Vector2(9, -1), Vector2(0, 11)])
		tail.color = _say_style.bg_color
		tail.name = "Tail"
		panel.add_child(tail)
	panel.modulate.a = 0.0
	add_child(panel)
	_bubbles[id] = {"anchor": p_anchor, "panel": panel, "label": label, "life": p_seconds if p_seconds > 0.0 else 1e9,
			"t": 0.0, "height": p_height, "hint": p_hint}


func _process(delta: float) -> void:
	var cam := get_viewport().get_camera_3d()
	for id in _bubbles.keys():
		var b: Dictionary = _bubbles[id]
		var panel: PanelContainer = b.panel
		if not is_instance_valid(b.anchor):
			panel.queue_free()
			_bubbles.erase(id)
			continue
		b.t = float(b.t) + delta
		b.life = float(b.life) - delta
		if b.life <= 0.0:
			panel.queue_free()
			_bubbles.erase(id)
			continue
		var at := (b.anchor as Node3D).global_position + Vector3.UP * float(b.height)
		var alpha := minf(minf(float(b.t) / FADE, float(b.life) / FADE), 1.0)
		if cam == null or cam.is_position_behind(at):
			panel.visible = false
			continue
		var dist := cam.global_position.distance_to(at)
		if dist > MAX_DIST:
			panel.visible = false
			continue
		panel.visible = true
		var s := clampf(7.0 / maxf(dist, 0.1), 0.62, 1.0)
		panel.scale = Vector2(s, s)
		panel.size = panel.get_combined_minimum_size()
		var tail := panel.get_node_or_null("Tail") as Polygon2D
		if tail:
			tail.position = Vector2(panel.size.x * 0.5, panel.size.y)
		var p := cam.unproject_position(at)
		panel.position = p - Vector2(panel.size.x * 0.5 * s, (panel.size.y + 11.0) * s)
		# (further ones fainter, behind the nearer: drawn in order of distance)
		panel.modulate.a = alpha * clampf(1.4 - dist / MAX_DIST, 0.35, 1.0)
		panel.z_index = int(100.0 - dist)
