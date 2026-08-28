"""Unit tests for the MIPS32 benchmark orchestrator."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
from types import ModuleType

import pytest


def load_benchmark_module() -> ModuleType:
    """Load the adjacent CLI module without requiring a package."""

    path = Path(__file__).with_name("run.py")
    specification = importlib.util.spec_from_file_location("cross_mips_benchmark", path)
    assert specification is not None and specification.loader is not None
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


benchmark = load_benchmark_module()


def test_parse_levels_is_stable_and_rejects_non_objectives() -> None:
    """Only the speed and minimum-size objectives belong in this corpus."""

    assert benchmark.parse_levels("O3,-Oz,O3") == ("O3", "Oz")
    with pytest.raises(benchmark.BenchmarkError, match="O3 and Oz only"):
        benchmark.parse_levels("O2")


def test_render_template_rejects_unresolved_fields() -> None:
    """Generated benchmark sources may not retain placeholders."""

    with pytest.raises(benchmark.BenchmarkError, match="unresolved"):
        benchmark.render_template("@FIRST@ @SECOND@", {"FIRST": "done"})


def test_parse_size_sections_excludes_metadata() -> None:
    """Load bytes include image data but exclude MIPS object metadata."""

    output = """object.o  :
section          size   addr
.text              40      0
.text.hot           8      0
.rodata            12      0
.data               4      0
.pdr               64      0
.MIPS.abiflags     24      0
Total             152
"""
    assert benchmark.parse_size_sections(output) == (48, 64)


def test_parse_nm_symbols_handles_objects_and_archive_headers() -> None:
    """Runtime dependency accounting must ignore LLVM nm archive headings."""

    output = """udivdi3.o:
__udivdi3 T 0 540

kernel.o:
__umoddi3 U 0 0
"""
    assert benchmark.parse_nm_symbols(output) == {"__udivdi3", "__umoddi3"}
    assert benchmark.parse_nm_symbols(output, "U") == {"__umoddi3"}


def test_first_diagnostic_prefers_fatal_lowering_failure() -> None:
    """An optional LLVM probe should report its cause, not an earlier warning."""

    result = benchmark.CommandResult(
        ("llc",),
        1,
        "",
        "warning: generic CPU\nLLVM ERROR: Cannot select: store (s64)\n",
    )
    assert benchmark.first_diagnostic(result) == (
        "LLVM ERROR: Cannot select: store (s64)"
    )

    addressed = benchmark.CommandResult(
        ("llc",), 1, "", "LLVM ERROR: Cannot select: 0x123abc: truncate\n"
    )
    assert benchmark.first_diagnostic(addressed) == (
        "LLVM ERROR: Cannot select: <node>: truncate"
    )


def test_runner_fragments_cover_every_kernel() -> None:
    """The bare-metal runner must declare, wrap, and invoke the full corpus."""

    declarations, wrappers, runs = benchmark.runner_fragments("bench_")
    for value in benchmark.KERNELS:
        assert f"bench_{value.name}" in declarations
        assert f"invoke_{value.name}" in wrappers
        assert f'"{value.name}"' in runs


def test_parse_runtime_output_reduces_odd_samples() -> None:
    """UART hexadecimal samples should reduce to typed medians."""

    lines = ["firmware noise", "kernel,sample,ticks,checksum"]
    for value in benchmark.KERNELS:
        for sample, ticks in enumerate((9, 5, 7)):
            lines.append(f"{value.name},{sample:08x},{ticks:08x},0123456789abcdef")
    records = benchmark.parse_runtime_output("\n".join(lines), "cross", "O3", 3)
    assert len(records) == len(benchmark.KERNELS)
    assert all(record.median_ticks == 7 for record in records)
    assert all(record.checksum == "0123456789abcdef" for record in records)


def test_verify_checksums_rejects_pipeline_disagreement() -> None:
    """A speed result is invalid unless every pipeline computes the same bits."""

    records = [
        benchmark.RuntimeRecord("cross", "O3", "integer", "mix", 1, 1, 1.0, "a"),
        benchmark.RuntimeRecord("gcc", "O3", "integer", "mix", 1, 1, 1.0, "b"),
    ]
    with pytest.raises(benchmark.BenchmarkError, match="disagree"):
        benchmark.verify_checksums(records)


def test_geometric_mean_requires_positive_values() -> None:
    """Ratio aggregation must not silently accept missing or invalid scores."""

    assert benchmark.geometric_mean((1.0, 4.0)) == pytest.approx(2.0)
    with pytest.raises(benchmark.BenchmarkError, match="positive"):
        benchmark.geometric_mean((1.0, 0.0))
