# fellow_agent — FellowEngine's toolkit for AI agents (milestone 51)

Commands an agent (or a script, or CI) runs to check its own work in a FellowEngine project without a person at the
editor. Each prints JSON and exits 0 (fine), 1 (ran, found problems) or 2 (could not run). Decision record:
`docs/decisions/0152-agent-toolkit.md`.

```
python tools/fellow_agent/fellow_agent.py <command> --help
```

| command | what it does | needs |
|---|---|---|
| `capture --project P --scene res://x.tscn [--views iso,top,front] [--size 1280x720] --out D` | renders the scene from its camera or framed views; `view_*.png`, `sheet.png`, `report.json` (image and rendering statistics, errors) | a renderer (a display, or xvfb-run on Linux) |
| `describe --project P --scene res://x.tscn` | the node tree as data: classes, transforms, bounds, non-default properties | headless |
| `check --project P [--scenes a,b]` | loads every script and scene, runs each scene 5 frames; errors per file with line and backtrace; every node's configuration warnings (the Scene dock's warning icons: a gas emitter outside its volume, a CollisionShape3D without a shape), reported without failing the check | headless (`--render` to run RenderingDevice nodes); the warnings need an editor build |
| `run --project P --scene res://x.tscn --seconds 10` | frame time mean and percentiles, GPU time, errors, the last frame | a renderer |
| `compare a.png b.png [--out diff.png]` | mean and largest difference, pixels over a threshold, PSNR | Python only |
| `import --project P` | imports the project's assets | an editor build |
| `api ClassName` / `api --search text` | the class reference from this repository's XML | Python only |
| `init --project P` | writes `AGENTS.md` into a game project (from `AGENTS.project.md`) | Python only |

The engine: `--godot`, `$FELLOW_GODOT`, or the newest `bin/godot.*` (editor builds first). Use an editor build for
`check` and `run`: release templates do not check scripts' errors. A capture renders at a fixed frame rate (`--fps`,
60) with the global random sequence seeded (`--seed`, 1): the same picture every run of a build on one machine (on
lavapipe bit for bit; `tools/fellow_regression/` relies on it).

How it works: `fellow_agent.py` runs `godot --path <project> -s tools/fellow_agent/fellow_agent.gd -- --cmd=...`;
the script is the main loop, catches errors through a `Logger`, and prints `FELLOW_AGENT {json}`; the wrapper adds
the log's other error lines and writes `report.json` and `engine.log` to `--out`.

Self-test: `python tools/fellow_agent/selftest.py --godot <editor build> --out <new directory>` (every command on
`test_project/`, with three planted faults and, from milestone 52, a misplaced gas emitter whose configuration warning
must be reported).
