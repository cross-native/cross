"""Unit tests for the x86-64 benchmark orchestrator."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
from types import ModuleType

import pytest


def load_benchmark_module() -> ModuleType:
    """Load the adjacent runner as a module without requiring a package."""

    path = Path(__file__).with_name("run.py")
    specification = importlib.util.spec_from_file_location("cross_benchmark", path)
    assert specification is not None and specification.loader is not None
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


benchmark = load_benchmark_module()


def test_parse_levels_preserves_order_and_removes_duplicates() -> None:
    """Optimization level parsing should be stable and duplicate-free."""

    levels = benchmark.parse_levels("O3,-O2,O3,Oz")
    assert [level.name for level in levels] == ["O3", "O2", "Oz"]


def test_parse_levels_rejects_unknown_value() -> None:
    """An unknown optimization preset should fail before tools are invoked."""

    with pytest.raises(benchmark.BenchmarkError, match="unknown optimization"):
        benchmark.parse_levels("O4")


def test_render_template_rejects_unresolved_fields() -> None:
    """Source templates may not silently retain a benchmark placeholder."""

    with pytest.raises(benchmark.BenchmarkError, match="unresolved template"):
        benchmark.render_template("@FIRST@ @SECOND@", {"FIRST": "done"})


def test_parse_text_size_sums_text_subsections() -> None:
    """Code size includes function sections as well as the root text section."""

    output = """object.o  :
section           size   addr
.text               10      0
.text.function      12      0
.data                7      0
Total               29
"""
    assert benchmark.parse_text_size(output) == 22


def test_parse_section_size_uses_total_footprint() -> None:
    """Constants and unwind metadata must remain visible beside text size."""

    output = """object.o  :
section           size   addr
.text               10      0
.rdata               8      0
.xdata               4      0
Total               22
"""
    assert benchmark.parse_section_size(output) == 22


def test_parse_runtime_csv_requires_rows() -> None:
    """An empty but well-formed timing stream is still invalid."""

    header = (
        "compiler,level,category,kernel,work_units,median_ns,mad_ns,"
        "ns_per_unit,checksum\n"
    )
    with pytest.raises(benchmark.BenchmarkError, match="no measurements"):
        benchmark.parse_runtime_csv(header)


def test_parse_runtime_csv_accepts_category_and_dispersion() -> None:
    """The stable runtime schema should retain grouping and timing noise."""

    output = (
        "compiler,level,category,kernel,work_units,median_ns,mad_ns,"
        "ns_per_unit,checksum\n"
        "cross,O3,scalar,mix,100,250.0,5.0,2.5,0x1\n"
    )
    records = benchmark.parse_runtime_csv(output)
    assert records[0].category == "scalar"
    assert records[0].mad_ns == 5.0


def test_geometric_mean() -> None:
    """Aggregate speedups use a scale-independent geometric mean."""

    assert benchmark.geometric_mean((1.0, 4.0)) == pytest.approx(2.0)


def test_balanced_runtime_advantage_weights_categories_equally() -> None:
    """The aggregate is explicitly composed from category-level scores."""

    index = {}
    favored = benchmark.CATEGORIES[0]
    favored_ratio = float(2 ** len(benchmark.CATEGORIES))
    for kernel in benchmark.KERNELS:
        for compiler, value in (
            ("cross", 1.0),
            ("gcc", favored_ratio if kernel.category == favored else 1.0),
        ):
            record = benchmark.RuntimeRecord(
                compiler,
                "O3",
                kernel.category,
                kernel.name,
                1,
                value,
                0.0,
                value,
                "0x1",
            )
            index[(compiler, "O3", kernel.category, kernel.name)] = record
    assert benchmark.balanced_runtime_advantage(index, "gcc", "O3") == pytest.approx(
        2.0
    )
    assert benchmark.leave_one_category_out_runtime_advantage(
        index, "gcc", "O3"
    ) == pytest.approx(1.0)


def test_kernel_templates_are_split_by_build_unit() -> None:
    """Every source build unit must retain equivalent Cross and C inputs."""

    directory = Path(__file__).with_name("kernels")
    for unit in benchmark.BUILD_UNITS:
        assert (directory / f"{unit}.x.in").is_file()
        assert (directory / f"{unit}.c.in").is_file()


def test_every_category_has_multiple_kernels() -> None:
    """A category must not reduce to a single hand-picked program."""

    for category in benchmark.CATEGORIES:
        assert sum(kernel.category == category for kernel in benchmark.KERNELS) >= 2


def test_unstable_runtime_measurements_reports_only_selected_level() -> None:
    """The speed gate should reject noisy O3 data without inspecting Oz."""

    records = [
        benchmark.RuntimeRecord(
            "cross", level, "integer", "mix", 1, 100.0, mad, 100.0, "0x1"
        )
        for level, mad in (("O3", 6.0), ("Oz", 20.0))
    ]
    assert benchmark.unstable_runtime_measurements(records, "O3", 0.05) == [
        {
            "compiler": "cross",
            "category": "integer",
            "kernel": "mix",
            "relative_mad": 0.06,
        }
    ]


def test_concise_version_skips_generic_llvm_heading() -> None:
    """LLVM's banner heading should not hide its numeric version."""

    lines = ["LLVM (http://llvm.org/):", "LLVM version 22.1.8"]
    assert benchmark.concise_version(lines) == "LLVM version 22.1.8"


def test_concise_version_keeps_cross_identity_before_license() -> None:
    """A license's wording must not replace Cross's identity line."""

    lines = [
        "Cross toolchain 0.1.0 (language 0.8)",
        "License GPLv3+: GNU GPL version 3 or later",
    ]
    assert benchmark.concise_version(lines) == lines[0]
