#!/usr/bin/env python3
"""Constitutional lint, GL bindings, build, parity, and render verification gate."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent

SIM_BANNED_PATTERNS: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"\bfloat\b"), "float"),
    (re.compile(r"\bdouble\b"), "double"),
    (re.compile(r"<cmath>"), "<cmath>"),
    (re.compile(r"\bmath\.h\b"), "math.h"),
    (re.compile(r"\bstd::vector\b"), "std::vector"),
    (re.compile(r"\bstd::unordered_map\b"), "std::unordered_map"),
    (re.compile(r"\bstd::map\b"), "std::map"),
    (re.compile(r"\bstd::set\b"), "std::set"),
    (re.compile(r"\bstd::chrono\b"), "std::chrono"),
    (re.compile(r"\brand\s*\("), "rand("),
    (re.compile(r"\bmalloc\s*\("), "malloc("),
    (re.compile(r"\bcalloc\s*\("), "calloc("),
    (re.compile(r"\brealloc\s*\("), "realloc("),
    (re.compile(r"\bfree\s*\("), "free("),
    (re.compile(r"\bto_render_f32\b"), "to_render_f32"),
    (re.compile(r"\bnew\s+(?!\()"), "non-placement new"),
    (re.compile(r"\bnew\s*\["), "new[]"),
]

RENDER_SIM_INCLUDE_RE = re.compile(r'#\s*include\s+[<"](?:[^>"]*/)?sim/')
DOC_FORBIDDEN_TOKENS = ("[WEBVIEW_BROWSER_HUD]", "<untrusted_page_content>")
CPP_EXTENSIONS = {".hpp", ".h", ".cpp", ".c"}

BINDING_PREFIXES = ("BIND_UBO_", "BIND_SSBO_", "TEX_", "IMG_")
TEX_UNIT_CAP = 32
IMG_UNIT_CAP = 8

TEST_RESULTS_RE = re.compile(r"Test results:\s*(\d+)\s+passed,\s*(\d+)\s+failed")
PARITY_RE = re.compile(r"\[PARITY\]\s+SimWorld_16k_100t\s*=\s*(0x[0-9a-fA-F]+)")
COMPILER_ERROR_RE = re.compile(r"(?:fatal error:|error:|undefined reference)", re.IGNORECASE)
RENDER_FAIL_RE = re.compile(r"\[FAIL\]|\[FATAL\]|\[  FAILED  \]")

CAPSULE_MAX_LINES = 25


@dataclass
class StageResult:
    stage: str
    status: str  # "PASS", "FAIL", "SKIP"
    summary: str
    duration_s: float = 0.0
    capsule: list[str] = field(default_factory=list)
    meta: dict[str, object] = field(default_factory=dict)


def strip_cpp_comments_and_strings(text: str) -> str:
    """Strip C/C++ comments and string/char literals while preserving line numbers and #include headers."""
    out: list[str] = []
    n = len(text)
    i = 0
    at_line_start = True
    in_include_line = False

    while i < n:
        ch = text[i]

        if ch == "\n":
            out.append("\n")
            prev_bs = i > 0 and text[i - 1] == "\\"
            if not prev_bs:
                in_include_line = False
            at_line_start = not prev_bs
            i += 1
            continue

        if at_line_start:
            if ch in (" ", "\t", "\r"):
                out.append(ch)
                i += 1
                continue
            if ch == "#" and re.match(r"#\s*include\b", text[i:]):
                in_include_line = True
            at_line_start = False

        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            i += 2
            while i < n:
                if text[i] == "\n":
                    if i > 0 and text[i - 1] == "\\":
                        out.append("\n")
                        i += 1
                        continue
                    break
                i += 1
            continue

        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            out.append(" ")
            i += 2
            while i < n:
                if text[i] == "*" and i + 1 < n and text[i + 1] == "/":
                    i += 2
                    break
                if text[i] == "\n":
                    out.append("\n")
                i += 1
            continue

        if not in_include_line:
            raw_m = re.match(r'(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(', text[i:])
            if raw_m:
                delim = raw_m.group(1)
                end_seq = f"){delim}\""
                start_body = i + raw_m.end()
                end_pos = text.find(end_seq, start_body)
                out.append('""')
                if end_pos == -1:
                    out.append("\n" * text[start_body:].count("\n"))
                    break
                out.append("\n" * text[start_body:end_pos].count("\n"))
                i = end_pos + len(end_seq)
                continue

            if ch == '"':
                out.append('""')
                i += 1
                while i < n:
                    if text[i] == "\\":
                        if i + 1 < n:
                            if text[i + 1] == "\n":
                                out.append("\n")
                            i += 2
                            continue
                        i += 1
                        break
                    if text[i] == "\n":
                        out.append("\n")
                        i += 1
                        break
                    if text[i] == '"':
                        i += 1
                        break
                    i += 1
                continue

            if ch == "'":
                out.append("''")
                i += 1
                while i < n:
                    if text[i] == "\\":
                        if i + 1 < n:
                            if text[i + 1] == "\n":
                                out.append("\n")
                            i += 2
                            continue
                        i += 1
                        break
                    if text[i] == "\n":
                        out.append("\n")
                        i += 1
                        break
                    if text[i] == "'":
                        i += 1
                        break
                    i += 1
                continue

        out.append(ch)
        i += 1

    return "".join(out)


def build_capsule(header: str, lines: list[str]) -> list[str]:
    """Clamp a failure capsule to CAPSULE_MAX_LINES total lines."""
    body_limit = CAPSULE_MAX_LINES - 1
    trimmed = [ln.rstrip() for ln in lines[:body_limit]]
    if len(lines) > body_limit and body_limit >= 1:
        trimmed[-1] = f"... ({len(lines) - body_limit + 1} more line(s) omitted)"
    return [header, *trimmed]


def stage_lint() -> StageResult:
    """Run static constitutional lint across src/sim, src/render, and docs/*.md."""
    t0 = time.monotonic()
    violations: list[str] = []

    sim_dir = REPO_ROOT / "src" / "sim"
    sim_files = sorted(p for p in sim_dir.rglob("*") if p.is_file() and p.suffix in CPP_EXTENSIONS)
    for path in sim_files:
        rel = path.relative_to(REPO_ROOT).as_posix()
        raw = path.read_text(encoding="utf-8", errors="replace")
        stripped = strip_cpp_comments_and_strings(raw)
        for pattern, label in SIM_BANNED_PATTERNS:
            for match in pattern.finditer(stripped):
                line_no = stripped.count("\n", 0, match.start()) + 1
                src_line = raw.splitlines()[line_no - 1].strip() if line_no <= len(raw.splitlines()) else ""
                violations.append(f"{rel}:{line_no}: banned '{label}' -> {src_line[:100]}")

    render_dir = REPO_ROOT / "src" / "render"
    render_files = sorted(p for p in render_dir.rglob("*") if p.is_file() and p.suffix in CPP_EXTENSIONS)
    for path in render_files:
        if "bridge" in path.name.lower():
            continue
        rel = path.relative_to(REPO_ROOT).as_posix()
        raw = path.read_text(encoding="utf-8", errors="replace")
        stripped = strip_cpp_comments_and_strings(raw)
        for match in RENDER_SIM_INCLUDE_RE.finditer(stripped):
            line_no = stripped.count("\n", 0, match.start()) + 1
            src_line = raw.splitlines()[line_no - 1].strip() if line_no <= len(raw.splitlines()) else ""
            violations.append(f"{rel}:{line_no}: forbidden sim/ include in render -> {src_line[:100]}")

    docs_dir = REPO_ROOT / "docs"
    doc_files = sorted(docs_dir.glob("*.md"))
    for path in doc_files:
        rel = path.relative_to(REPO_ROOT).as_posix()
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for idx, line in enumerate(lines, 1):
            for tok in DOC_FORBIDDEN_TOKENS:
                if tok in line:
                    violations.append(f"{rel}:{idx}: forbidden doc marker {tok}")

    dt = time.monotonic() - t0
    if violations:
        return StageResult(
            stage="LINT",
            status="FAIL",
            summary=f"{len(violations)} constitutional lint violation(s)",
            duration_s=dt,
            capsule=build_capsule(f"[FAIL] LINT: {len(violations)} violation(s)", violations),
        )

    return StageResult(
        stage="LINT",
        status="PASS",
        summary=f"sim={len(sim_files)} files, render={len(render_files)} files, docs={len(doc_files)} files clean",
        duration_s=dt,
    )


def parse_gl_bindings(path: pathlib.Path) -> tuple[dict[str, list[tuple[str, int]]], list[str]]:
    """Parse RON_GL_BINDINGS(X) entries grouped by prefix."""
    lines = path.read_text(encoding="utf-8").splitlines()
    block: list[str] = []
    capturing = False
    for line in lines:
        if not capturing and line.lstrip().startswith("#define RON_GL_BINDINGS"):
            capturing = True
        if capturing:
            block.append(line)
            if not line.rstrip().endswith("\\"):
                break
    macro_text = "\n".join(block)

    groups: dict[str, list[tuple[str, int]]] = {p: [] for p in BINDING_PREFIXES}
    errors: list[str] = []
    entry_re = re.compile(r'X\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*"([^"]*)"\s*\)')
    matches = list(entry_re.finditer(macro_text))
    if not matches:
        errors.append(f"No RON_GL_BINDINGS entries found in {path.relative_to(REPO_ROOT).as_posix()}")
        return groups, errors

    for m in matches:
        name, c_val_raw, _c_type, _glsl = m.groups()
        prefix = next((p for p in BINDING_PREFIXES if name.startswith(p)), None)
        if prefix is None:
            continue
        try:
            slot = int(c_val_raw.strip(), 0)
        except ValueError:
            errors.append(f"{name}: non-integer slot value '{c_val_raw.strip()}'")
            continue
        if slot < 0:
            errors.append(f"{name}: negative slot {slot}")
            continue
        groups[prefix].append((name, slot))

    return groups, errors


def stage_bindings() -> StageResult:
    """Validate GL binding slot uniqueness and Ampere unit ceilings."""
    t0 = time.monotonic()
    bindings_path = REPO_ROOT / "src" / "render" / "gl_bindings.hpp"
    groups, errors = parse_gl_bindings(bindings_path)

    for prefix, entries in groups.items():
        seen: dict[int, str] = {}
        for name, slot in entries:
            if slot in seen:
                errors.append(f"Duplicate {prefix}* slot {slot}: {seen[slot]} and {name}")
            else:
                seen[slot] = name

    tex_slots = [slot for _, slot in groups["TEX_"]]
    img_slots = [slot for _, slot in groups["IMG_"]]
    tex_max = max(tex_slots, default=-1)
    img_max = max(img_slots, default=-1)

    if tex_max >= TEX_UNIT_CAP:
        errors.append(f"TEX_* max slot {tex_max} violates GL_MAX_TEXTURE_IMAGE_UNITS (< {TEX_UNIT_CAP})")
    if img_max >= IMG_UNIT_CAP:
        errors.append(f"IMG_* max slot {img_max} violates GL_MAX_IMAGE_UNITS (< {IMG_UNIT_CAP})")

    tex_headroom = TEX_UNIT_CAP - (tex_max + 1) if tex_slots else TEX_UNIT_CAP
    img_headroom = IMG_UNIT_CAP - (img_max + 1) if img_slots else IMG_UNIT_CAP

    dt = time.monotonic() - t0
    meta = {
        "counts": {k: len(v) for k, v in groups.items()},
        "tex_max": tex_max,
        "tex_cap": TEX_UNIT_CAP,
        "tex_headroom": tex_headroom,
        "img_max": img_max,
        "img_cap": IMG_UNIT_CAP,
        "img_headroom": img_headroom,
    }
    if errors:
        return StageResult(
            stage="BINDINGS",
            status="FAIL",
            summary=f"{len(errors)} GL binding violation(s)",
            duration_s=dt,
            capsule=build_capsule(f"[FAIL] BINDINGS: {len(errors)} violation(s)", errors),
            meta=meta,
        )

    summary = (
        f"UBO={len(groups['BIND_UBO_'])} SSBO={len(groups['BIND_SSBO_'])} "
        f"TEX={len(tex_slots)}/{TEX_UNIT_CAP} (headroom {tex_headroom}) "
        f"IMG={len(img_slots)}/{IMG_UNIT_CAP} (headroom {img_headroom})"
    )
    return StageResult(stage="BINDINGS", status="PASS", summary=summary, duration_s=dt, meta=meta)


def run_process(cmd: list[str]) -> tuple[int, str, float]:
    """Run subprocess in repo root and capture combined stdout/stderr."""
    t0 = time.monotonic()
    proc = subprocess.run(
        cmd,
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, proc.stdout, time.monotonic() - t0


def extract_compile_failure(output: str) -> list[str]:
    """Extract first compiler/linker error with 5 lines before and 15 lines after."""
    lines = output.splitlines()
    for idx, line in enumerate(lines):
        if COMPILER_ERROR_RE.search(line):
            start = max(0, idx - 5)
            end = min(len(lines), idx + 16)
            return lines[start:end]
    return lines[-24:] if len(lines) > 24 else lines


def stage_compile(stage: str, build_dir_name: str, build_type: str) -> StageResult:
    """Configure if needed and build a CMake/Ninja directory."""
    t0 = time.monotonic()
    build_dir = REPO_ROOT / build_dir_name
    if not (build_dir / "build.ninja").is_file():
        cfg_cmd = ["cmake", "-S", ".", "-B", build_dir_name, "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={build_type}"]
        rc, out, _ = run_process(cfg_cmd)
        if rc != 0:
            lines = extract_compile_failure(out)
            return StageResult(
                stage=stage,
                status="FAIL",
                summary=f"CMake configure failed ({build_dir_name}, exit {rc})",
                duration_s=time.monotonic() - t0,
                capsule=build_capsule(f"[FAIL] {stage}: cmake configure failed (exit {rc})", lines),
            )

    rc, out, _ = run_process(["cmake", "--build", build_dir_name])
    dt = time.monotonic() - t0
    if rc != 0:
        lines = extract_compile_failure(out)
        return StageResult(
            stage=stage,
            status="FAIL",
            summary=f"Build failed ({build_dir_name}, exit {rc})",
            duration_s=dt,
            capsule=build_capsule(f"[FAIL] {stage}: build failed (exit {rc})", lines),
        )

    return StageResult(stage=stage, status="PASS", summary=f"{build_dir_name} ({build_type}) built", duration_s=dt)


def extract_render_failure(output: str) -> list[str]:
    """Extract first [FAIL]/[FATAL]/[  FAILED  ] block with context."""
    lines = output.splitlines()
    for idx, line in enumerate(lines):
        if RENDER_FAIL_RE.search(line):
            start = max(0, idx - 2)
            end = min(len(lines), idx + 20)
            return lines[start:end]
    return lines[-24:] if len(lines) > 24 else lines


def stage_sim_test(stage: str, exe_rel: str) -> tuple[StageResult, str | None]:
    """Run ron_tests.exe, assert 0 failed, and extract [PARITY] hash."""
    exe_path = REPO_ROOT / exe_rel
    if not exe_path.is_file():
        return (
            StageResult(
                stage=stage,
                status="FAIL",
                summary=f"Missing binary: {exe_rel}",
                capsule=[f"[FAIL] {stage}: missing binary {exe_rel}"],
            ),
            None,
        )

    rc, out, dt = run_process([str(exe_path)])
    res_m = TEST_RESULTS_RE.search(out)
    parity_m = PARITY_RE.search(out)
    parity_hash = parity_m.group(1).lower() if parity_m else None

    if rc != 0 or not res_m or int(res_m.group(2)) != 0 or int(res_m.group(1)) <= 0:
        fail_lines = extract_render_failure(out)
        return (
            StageResult(
                stage=stage,
                status="FAIL",
                summary=f"{exe_rel} failed (exit {rc})",
                duration_s=dt,
                capsule=build_capsule(f"[FAIL] {stage}: {exe_rel} failed (exit {rc})", fail_lines),
                meta={"parity_hash": parity_hash},
            ),
            parity_hash,
        )

    passed = int(res_m.group(1))
    summary = f"{passed} passed, 0 failed"
    if parity_hash:
        summary += f", parity={parity_hash}"
    return (
        StageResult(
            stage=stage,
            status="PASS",
            summary=summary,
            duration_s=dt,
            meta={"passed": passed, "failed": 0, "parity_hash": parity_hash},
        ),
        parity_hash,
    )


def stage_hash_parity(debug_hash: str | None, release_hash: str | None) -> StageResult:
    """Verify Debug and Release SimWorld parity hashes exist and match bit-for-bit."""
    if not debug_hash or not release_hash:
        missing = []
        if not debug_hash:
            missing.append("Debug [PARITY] line missing")
        if not release_hash:
            missing.append("Release [PARITY] line missing")
        return StageResult(
            stage="HASH_MISMATCH",
            status="FAIL",
            summary="; ".join(missing),
            capsule=build_capsule("[FAIL] HASH_MISMATCH: missing [PARITY] hash", missing),
            meta={"debug_hash": debug_hash, "release_hash": release_hash},
        )

    if debug_hash != release_hash:
        lines = [
            f"Debug   SimWorld_16k_100t = {debug_hash}",
            f"Release SimWorld_16k_100t = {release_hash}",
        ]
        return StageResult(
            stage="HASH_MISMATCH",
            status="FAIL",
            summary=f"Debug ({debug_hash}) != Release ({release_hash})",
            capsule=build_capsule("[FAIL] HASH_MISMATCH: Debug != Release", lines),
            meta={"debug_hash": debug_hash, "release_hash": release_hash},
        )

    return StageResult(
        stage="HASH_MISMATCH",
        status="PASS",
        summary=f"Debug == Release == {release_hash}",
        meta={"debug_hash": debug_hash, "release_hash": release_hash},
    )


def stage_render_test(stage: str, exe_rel: str) -> StageResult:
    """Run ron_render_tests.exe and assert exit 0 and 0 failed."""
    exe_path = REPO_ROOT / exe_rel
    if not exe_path.is_file():
        return StageResult(
            stage=stage,
            status="FAIL",
            summary=f"Missing binary: {exe_rel}",
            capsule=[f"[FAIL] {stage}: missing binary {exe_rel}"],
        )

    rc, out, dt = run_process([str(exe_path)])
    res_m = TEST_RESULTS_RE.search(out)
    if rc != 0 or not res_m or int(res_m.group(2)) != 0 or int(res_m.group(1)) <= 0:
        fail_lines = extract_render_failure(out)
        return StageResult(
            stage=stage,
            status="FAIL",
            summary=f"{exe_rel} failed (exit {rc})",
            duration_s=dt,
            capsule=build_capsule(f"[FAIL] {stage}: {exe_rel} failed (exit {rc})", fail_lines),
        )

    passed = int(res_m.group(1))
    return StageResult(
        stage=stage,
        status="PASS",
        summary=f"{passed} passed, 0 failed",
        duration_s=dt,
        meta={"passed": passed, "failed": 0},
    )


def emit_stage(result: StageResult, json_mode: bool) -> None:
    """Print human-readable stage status (and failure capsule if failed) when not in --json mode."""
    if json_mode:
        return
    tag = {"PASS": "PASS", "FAIL": "FAIL", "SKIP": "SKIP"}[result.status]
    print(f"[{tag}] {result.stage:<16} {result.summary} ({result.duration_s:.2f}s)")
    if result.status == "FAIL" and result.capsule:
        print("--- Failure Capsule (<=25 lines) ---")
        for line in result.capsule[:CAPSULE_MAX_LINES]:
            print(line)
        print("-----------------------------------")


def main() -> int:
    parser = argparse.ArgumentParser(description="Dominion 2026 constitutional gate check")
    parser.add_argument("--skip-build", action="store_true", help="Skip CMake Debug/Release builds")
    parser.add_argument("--render-debug", action="store_true", help="Also run build-debug/ron_render_tests.exe")
    parser.add_argument("--skip-render", action="store_true", help="Skip ron_render_tests.exe")
    parser.add_argument("--json", action="store_true", help="Emit structured JSON report")
    args = parser.parse_args()

    results: list[StageResult] = []
    failed_stage: StageResult | None = None

    def record(res: StageResult) -> bool:
        nonlocal failed_stage
        results.append(res)
        emit_stage(res, args.json)
        if res.status == "FAIL":
            failed_stage = res
            return False
        return True

    ok = record(stage_lint())
    if ok:
        ok = record(stage_bindings())

    if ok:
        if args.skip_build:
            record(StageResult("COMPILE_DEBUG", "SKIP", "--skip-build set"))
            record(StageResult("COMPILE_RELEASE", "SKIP", "--skip-build set"))
        else:
            ok = record(stage_compile("COMPILE_DEBUG", "build-debug", "Debug"))
            if ok:
                ok = record(stage_compile("COMPILE_RELEASE", "build", "Release"))

    debug_hash: str | None = None
    release_hash: str | None = None
    if ok:
        res_dbg, debug_hash = stage_sim_test("TEST_DEBUG", "build-debug/ron_tests.exe")
        ok = record(res_dbg)
    if ok:
        res_rel, release_hash = stage_sim_test("TEST_RELEASE", "build/ron_tests.exe")
        ok = record(res_rel)
    if ok:
        ok = record(stage_hash_parity(debug_hash, release_hash))

    if ok:
        if args.skip_render:
            record(StageResult("RENDER_RELEASE", "SKIP", "--skip-render set"))
        else:
            ok = record(stage_render_test("RENDER_RELEASE", "build/ron_render_tests.exe"))
            if ok and args.render_debug:
                ok = record(stage_render_test("RENDER_DEBUG", "build-debug/ron_render_tests.exe"))

    if args.json:
        payload = {
            "ok": failed_stage is None,
            "failed_stage": failed_stage.stage if failed_stage else None,
            "stages": [asdict(r) for r in results],
        }
        print(json.dumps(payload, indent=2))
    else:
        if failed_stage is None:
            print("ALL GATES PASSED.")
        else:
            print(f"GATE CHECK FAILED at {failed_stage.stage}.")

    return 0 if failed_stage is None else 1


if __name__ == "__main__":
    sys.exit(main())
