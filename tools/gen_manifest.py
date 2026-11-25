#!/usr/bin/env python3
"""Generate concise ground-truth docs/ARCH_MANIFEST.md from live source files."""

from __future__ import annotations

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT_PATH = REPO_ROOT / "docs" / "ARCH_MANIFEST.md"


def collapse_ws(text: str) -> str:
    return " ".join(text.split())


def extract_cmake_block(text: str, pattern: str) -> list[str]:
    return [collapse_ws(m.group(1)) for m in re.finditer(pattern, text, re.DOTALL)]


def parse_cmake(cmake_path: pathlib.Path) -> dict[str, object]:
    text = cmake_path.read_text(encoding="utf-8")
    libs = re.findall(r"add_library\(\s*([A-Za-z0-9_]+)\s+([A-Z]+)", text)
    exes = re.findall(r"add_executable\(\s*([A-Za-z0-9_]+)\s+", text)
    subdirs = re.findall(r"add_subdirectory\(\s*([^\s)]+)", text)

    def target_info(name: str) -> dict[str, str]:
        links = extract_cmake_block(text, rf"target_link_libraries\(\s*{re.escape(name)}\s+(.*?)\)")
        opts = extract_cmake_block(text, rf"target_compile_options\(\s*{re.escape(name)}\s+(.*?)\)")
        defs = extract_cmake_block(text, rf"target_compile_definitions\(\s*{re.escape(name)}\s+(.*?)\)")
        return {
            "links": "; ".join(links) if links else "-",
            "options": "; ".join(opts) if opts else "-",
            "defs": "; ".join(defs) if defs else "-",
        }

    return {
        "libs": libs,
        "exes": exes,
        "subdirs": subdirs,
        "ron_sim": target_info("ron_sim"),
        "ron_render": target_info("ron_render"),
    }


def parse_bindings(path: pathlib.Path) -> dict[str, list[tuple[str, int]]]:
    text = path.read_text(encoding="utf-8")
    block: list[str] = []
    capturing = False
    for line in text.splitlines():
        if not capturing and line.lstrip().startswith("#define RON_GL_BINDINGS"):
            capturing = True
        if capturing:
            block.append(line)
            if not line.rstrip().endswith("\\"):
                break
    macro_text = "\n".join(block)

    groups: dict[str, list[tuple[str, int]]] = {
        "BIND_UBO_": [],
        "BIND_SSBO_": [],
        "TEX_": [],
        "IMG_": [],
    }
    entry_re = re.compile(r'X\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*"([^"]*)"\s*\)')
    for name, c_val, _c_type, _glsl in entry_re.findall(macro_text):
        for prefix, lst in groups.items():
            if name.startswith(prefix):
                lst.append((name, int(c_val.strip(), 0)))
                break
    for lst in groups.values():
        lst.sort(key=lambda item: item[1])
    return groups


def next_free_slot(entries: list[tuple[str, int]]) -> int:
    used = {slot for _, slot in entries}
    idx = 0
    while idx in used:
        idx += 1
    return idx


def parse_vram_caps(cpp_path: pathlib.Path, hpp_path: pathlib.Path) -> tuple[int, list[tuple[str, int]]]:
    hpp_text = hpp_path.read_text(encoding="utf-8")
    cap_m = re.search(r"VRAM_TOTAL_CAP\s*=\s*(\d+)(?:ull)?\s*<<\s*20", hpp_text)
    total_mib = int(cap_m.group(1)) if cap_m else 2304

    cpp_text = cpp_path.read_text(encoding="utf-8")
    arr_m = re.search(r"K_VRAM_CATS\[.*?\]\s*=\s*\{(.*?)\};", cpp_text, re.DOTALL)
    cats: list[tuple[str, int]] = []
    if arr_m:
        for name, mib in re.findall(r'\{\s*"([^"]+)"\s*,\s*(\d+)(?:ull)?\s*<<\s*20\s*\}', arr_m.group(1)):
            cats.append((name, int(mib)))
    return total_mib, cats


def extract_static_asserts(path: pathlib.Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    return [collapse_ws(m.group(1)) for m in re.finditer(r"static_assert\s*\(([^;]+?)\)\s*;", text, re.DOTALL)]


def parse_sim_contracts() -> dict[str, object]:
    fixed_path = REPO_ROOT / "src" / "core" / "fixed_math.hpp"
    cmd_path = REPO_ROOT / "src" / "sim" / "command_bus.hpp"
    handle_path = REPO_ROOT / "src" / "sim" / "entity_handle.hpp"
    soa_path = REPO_ROOT / "src" / "sim" / "entity_soa.hpp"
    world_path = REPO_ROOT / "src" / "sim" / "sim_world.hpp"

    cmd_text = cmd_path.read_text(encoding="utf-8")
    enum_m = re.search(r"enum\s+class\s+CommandType\s*:\s*u8\s*\{(.*?)\};", cmd_text, re.DOTALL)
    cmd_types: list[tuple[str, str]] = []
    if enum_m:
        for name, val in re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([0-9]+)", enum_m.group(1)):
            cmd_types.append((name, val))

    constants: list[tuple[str, str]] = []
    for path in (handle_path, cmd_path, fixed_path):
        txt = path.read_text(encoding="utf-8")
        for key in (
            "MAX_ENTITIES",
            "MAX_PLAYERS",
            "SIM_TICKS_PER_SECOND",
            "MAX_PENDING_COMMANDS",
            "MAX_REPLAY_COMMANDS",
        ):
            m = re.search(rf"constexpr\s+\w+\s+{key}\s*=\s*([^;]+);", txt)
            if m:
                constants.append((key, m.group(1).strip()))

    asserts = {
        "fixed_math.hpp": extract_static_asserts(fixed_path),
        "entity_handle.hpp": extract_static_asserts(handle_path),
        "command_bus.hpp": extract_static_asserts(cmd_path),
        "entity_soa.hpp": extract_static_asserts(soa_path),
        "sim_world.hpp": extract_static_asserts(world_path),
    }
    return {"cmd_types": cmd_types, "constants": constants, "asserts": asserts}


def parse_deviations(path: pathlib.Path) -> list[tuple[str, list[str]]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    sections: list[tuple[str, list[str]]] = []
    cur_gate: str | None = None
    cur_items: list[str] = []

    for line in lines:
        if line.startswith("## "):
            if cur_gate is not None:
                sections.append((cur_gate, cur_items))
            cur_gate = line[3:].strip()
            cur_items = []
        elif line.startswith("### ") and cur_gate is not None:
            cur_items.append(line[4:].strip())
        elif line.strip().startswith("None.") and cur_gate is not None and not cur_items:
            cur_items.append("None")

    if cur_gate is not None:
        sections.append((cur_gate, cur_items))
    return sections


def render_manifest() -> str:
    cmake = parse_cmake(REPO_ROOT / "CMakeLists.txt")
    bindings = parse_bindings(REPO_ROOT / "src" / "render" / "gl_bindings.hpp")
    total_mib, vram_cats = parse_vram_caps(
        REPO_ROOT / "src" / "render" / "vram_ledger.cpp",
        REPO_ROOT / "src" / "render" / "vram_ledger.hpp",
    )
    sim = parse_sim_contracts()
    deviations = parse_deviations(REPO_ROOT / "docs" / "spec_deviations.md")

    lines: list[str] = [
        "# DOMINION 2026 — Architecture Manifest",
        "",
        "Generated by `tools/gen_manifest.py` from live repository headers and build files.",
        "",
        "## 1. Build Targets & Dependencies (`CMakeLists.txt`)",
        f"- **Libraries**: {', '.join(f'`{n}` ({k})' for n, k in cmake['libs'])} + `SDL3::SDL3-static` (`{', '.join(cmake['subdirs'])}`)",
        f"- **Executables**: {', '.join(f'`{e}`' for e in cmake['exes'])}",
        f"- **`ron_sim`**: links `{cmake['ron_sim']['links']}`; defs `{cmake['ron_sim']['defs']}`; opts `{cmake['ron_sim']['options']}`",
        f"- **`ron_render`**: links `{cmake['ron_render']['links']}`; opts `{cmake['ron_render']['options']}`",
        "",
        "## 2. GL Shader Bindings (`src/render/gl_bindings.hpp`)",
        "",
        "| Group | Occupied Slots | Next Free | Cap / Headroom |",
        "| :--- | :--- | :---: | :---: |",
    ]

    caps = {"TEX_": 32, "IMG_": 8}
    for prefix in ("BIND_UBO_", "BIND_SSBO_", "TEX_", "IMG_"):
        entries = bindings[prefix]
        short_list = ", ".join(f"`{slot}:{name.removeprefix(prefix)}`" for name, slot in entries)
        nf = next_free_slot(entries)
        if prefix in caps:
            cap = caps[prefix]
            headroom = cap - (max((s for _, s in entries), default=-1) + 1)
            cap_str = f"{cap} ({headroom} free)"
        else:
            cap_str = "-"
        lines.append(f"| `{prefix}*` ({len(entries)}) | {short_list} | `{nf}` | {cap_str} |")

    vram_sum = sum(mib for _, mib in vram_cats)
    lines.extend([
        "",
        f"## 3. VRAM Ledger Caps (`src/render/vram_ledger.cpp`, Total = {vram_sum} / {total_mib} MiB)",
        "",
        "| Category | Cap (MiB) | Category | Cap (MiB) | Category | Cap (MiB) |",
        "| :--- | :---: | :--- | :---: | :--- | :---: |",
    ])
    for i in range(0, len(vram_cats), 3):
        row_cells: list[str] = []
        for j in range(3):
            if i + j < len(vram_cats):
                cname, cmib = vram_cats[i + j]
                row_cells.append(f"`{cname}` | {cmib}")
            else:
                row_cells.append(" | ")
        lines.append("| " + " | ".join(row_cells) + " |")

    const_str = ", ".join(f"`{k}={v}`" for k, v in sim["constants"])
    cmd_str = ", ".join(f"`{v}:{n}`" for n, v in sim["cmd_types"])
    lines.extend([
        "",
        "## 4. Core & Simulation Contracts",
        f"- **Constants**: {const_str}",
        f"- **`CommandType` (`u8`)**: {cmd_str}",
        "- **`static_assert` invariants**:",
    ])
    for fname, asserts in sim["asserts"].items():
        if asserts:
            joined = "; ".join(f"`{a}`" for a in asserts)
            lines.append(f"  - `{fname}`: {joined}")

    lines.extend([
        "",
        "## 5. Deviations (`docs/spec_deviations.md`)",
    ])
    for gate, items in deviations:
        if items == ["None"]:
            lines.append(f"- **{gate}**: None")
        else:
            lines.append(f"- **{gate}**: " + "; ".join(items))

    return "\n".join(lines) + "\n"


def main() -> int:
    manifest = render_manifest()
    OUT_PATH.write_text(manifest, encoding="utf-8")
    size_bytes = OUT_PATH.stat().st_size
    print(f"Wrote {OUT_PATH.relative_to(REPO_ROOT).as_posix()} ({size_bytes} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
