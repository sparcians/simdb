#!/usr/bin/env python3
"""Add chapter/part anchors and cross-reference links to SimDB book sources."""

from __future__ import annotations

import re
from pathlib import Path

BOOK_DIR = Path(__file__).resolve().parent
PART_FILES = [
    "parts/part1-why-simdb.adoc",
    "parts/part2-getting-started.adoc",
    "parts/part3-sqlite-interface.adoc",
    "parts/part4-concurrent-pipelines.adoc",
    "parts/part5-app-framework.adoc",
    "parts/part6-advanced-pipelines.adoc",
    "parts/part7-argos-case-study.adoc",
    "parts/part8-patterns-cookbook.adoc",
    "parts/part9-reference.adoc",
]

PART_ANCHORS = {
    1: "part-i",
    2: "part-ii",
    3: "part-iii",
    4: "part-iv",
    5: "part-v",
    6: "part-vi",
    7: "part-vii",
    8: "part-viii",
    9: "part-ix",
}

CHAPTER_ANCHORS = {
    1: "ch-01-introduction",
    2: "ch-02-simulation-data-problem",
    3: "ch-03-landscape",
    4: "ch-04-core-concepts",
    5: "ch-05-installing-simdb",
    6: "ch-06-building-cmake",
    7: "ch-07-first-program",
    8: "ch-08-regression-tests",
    9: "ch-09-defining-schema",
    10: "ch-10-database-manager",
    11: "ch-11-inserting-data",
    12: "ch-12-querying-data",
    13: "ch-13-transactions",
    14: "ch-14-blobs-compression",
    15: "ch-15-inspecting-dumping",
    16: "ch-16-pipeline-fundamentals",
    17: "ch-17-ports-concurrent-queue",
    18: "ch-18-building-pipeline",
    19: "ch-19-database-stages",
    20: "ch-20-move-semantics",
    21: "ch-21-pipeline-examples",
    22: "ch-22-simdb-app",
    23: "ch-23-app-manager",
    24: "ch-24-multiple-apps",
    25: "ch-25-app-factories",
    26: "ch-26-flushers",
    27: "ch-27-snoopers",
    28: "ch-28-disabling-pipelines",
    29: "ch-29-async-db-access",
    30: "ch-30-threads-scheduling",
    31: "ch-31-argos-is",
    32: "ch-32-argos-collector",
    33: "ch-33-on-disk-model",
    34: "ch-34-argos-viewer",
    35: "ch-35-end-to-end",
    36: "ch-36-schema-pipeline-together",
    37: "ch-37-stage-boundaries",
    38: "ch-38-performance-tuning",
    39: "ch-39-determinism-debugging",
    40: "ch-40-application-shapes",
    41: "ch-41-testing-simdb-apps",
}

APPENDIX_ANCHORS = {
    "Utilities Reference": "appendix-utilities-reference",
    "API Quick Reference & Glossary": "appendix-api-glossary",
    "FAQ & Troubleshooting": "appendix-faq",
    "Migration Guide": "appendix-migration-guide",
}

FAQ_SECTION_ANCHORS = {
    '"Database is locked" / SQLite contention': "faq-database-locked",
    '"Why is my pipeline sleeping?"': "faq-pipeline-sleeping",
    '"Why don\'t I see my rows yet?"': "faq-rows-not-visible",
    '"My database is partial or corrupt after a crash"': "faq-partial-database",
    '"Why did CI fail in Release but not Debug?"': "faq-release-werror",
    '"Snooper never finds my key"': "faq-snooper-key",
    '"AsyncDatabaseAccessor eval hangs or times out"': "faq-async-eval-hangs",
}

ROMAN = {1: "I", 2: "II", 3: "III", 4: "IV", 5: "V", 6: "VI", 7: "VII", 8: "VIII", 9: "IX"}


def part_number(rel: str) -> int:
    m = re.search(r"part(\d+)", rel)
    assert m, rel
    return int(m.group(1))


def ensure_anchors() -> None:
    ch = 1
    for idx, rel in enumerate(PART_FILES, start=1):
        path = BOOK_DIR / rel
        lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
        out: list[str] = []
        part_num = part_number(rel)
        part_anchor = PART_ANCHORS[idx]
        if lines and not lines[0].strip().startswith(f"[#{part_anchor}]"):
            out.append(f"[#{part_anchor}]\n")
        for line in lines:
            if line.strip().startswith(f"[#{part_anchor}]"):
                continue
            if line.startswith("== ") and not line.startswith("=== "):
                title = line[3:].strip()
                if part_num == 9 and title in APPENDIX_ANCHORS:
                    anchor = APPENDIX_ANCHORS[title]
                else:
                    anchor = CHAPTER_ANCHORS[ch]
                    ch += 1
                if not out or not out[-1].strip().startswith(f"[#{anchor}]"):
                    out.append(f"[#{anchor}]\n")
            if line.startswith("=== ") and part_num == 9:
                title = line[4:].strip()
                if title in FAQ_SECTION_ANCHORS:
                    anchor = FAQ_SECTION_ANCHORS[title]
                    if not out or not out[-1].strip().startswith(f"[#{anchor}]"):
                        out.append(f"[#{anchor}]\n")
            out.append(line)
        path.write_text("".join(out), encoding="utf-8")


def fix_part_titles(text: str) -> str:
    for n in range(1, 10):
        roman = ROMAN[n]
        text = re.sub(
            rf"^= <<part-[ivx]+,Part {roman}>>:",
            f"= Part {roman}:",
            text,
            flags=re.MULTILINE | re.IGNORECASE,
        )
        # exact anchor ids
        anchor = PART_ANCHORS[n]
        text = re.sub(
            rf"^= <<{anchor},Part {roman}>>:",
            f"= Part {roman}:",
            text,
            flags=re.MULTILINE,
        )
    return text


def dedupe_nested_xrefs(text: str) -> str:
    prev = None
    while prev != text:
        prev = text
        text = re.sub(r"<<([^,>]+),<<\1,([^>]+)>>>>", r"<<\1,\2>>", text)
    return text


def fix_truncated_xrefs(text: str) -> str:
    """Undo part/chapter xrefs split by an over-eager repair pass."""
    for num, anchor in CHAPTER_ANCHORS.items():
        digits = str(num)
        for split in range(1, len(digits)):
            text = text.replace(
                f"<<{anchor},Chapter {digits[:split]}>>{digits[split:]}>>",
                xref(anchor, f"Chapter {num}"),
            )
    for n, anchor in PART_ANCHORS.items():
        roman = ROMAN[n]
        for split in range(1, len(roman)):
            prefix = roman[:split]
            suffix = roman[split:]
            text = text.replace(
                f"<<{anchor},Part {prefix}>>{suffix}>>",
                xref(anchor, f"Part {roman}"),
            )
    return text


def repair_broken_xrefs(text: str) -> str:
    """Close xrefs that lost their trailing `>>` during earlier passes."""
    part_label = r"Part (?:VIII|VII|III|IV|VI|II|IX|I|V)"
    text = re.sub(
        rf"<<([a-z0-9-]+),({part_label})(?![>A-Za-z])",
        r"<<\1,\2>>",
        text,
    )
    text = re.sub(
        r"<<([a-z0-9-]+),(Chapter \d+)(?![>0-9])",
        r"<<\1,\2>>",
        text,
    )
    return text


def normalize_xref_syntax(text: str) -> str:
    text = fix_truncated_xrefs(text)
    text = repair_broken_xrefs(text)
    text = dedupe_nested_xrefs(text)
    while ">>>>" in text:
        text = text.replace(">>>>", ">>")
    return text


def strip_all_xrefs(text: str) -> str:
    """Expand xrefs back to visible labels for a clean re-link pass."""
    text = normalize_xref_syntax(text)
    text = re.sub(r"(?:part-[a-z0-9-]+,)+", "", text)
    text = re.sub(r"<<([^,>]+),([^>]+)>>", r"\2", text)
    text = re.sub(r"<<([^>]+)>>", r"\1", text)
    text = re.sub(
        r"\bChapters Chapter (\d+) and Chapter (\d+)\b",
        r"Chapters \1 and \2",
        text,
    )
    return text


def xref(anchor: str, label: str) -> str:
    return f"<<{anchor},{label}>>"


def linkify_compounds(text: str) -> str:
    """Multi-part and part+chapter phrases (always emit complete xrefs)."""
    replacements = [
        (r"Parts I--VIII", f"Parts {xref('part-i','Part I')}--{xref('part-viii','Part VIII')}"),
        (r"Parts III--VI", f"Parts {xref('part-iii','Part III')}--{xref('part-vi','Part VI')}"),
        (r"Parts IV through VI", f"Parts {xref('part-iv','Part IV')} through {xref('part-vi','Part VI')}"),
        (r"Parts IV--VI", f"Parts {xref('part-iv','Part IV')}--{xref('part-vi','Part VI')}"),
        (r"Parts II--III", f"Parts {xref('part-ii','II')} and {xref('part-iii','III')}"),
        (r"Parts II and III", f"Parts {xref('part-ii','II')} and {xref('part-iii','III')}"),
        (r"Parts VIII--IX", f"Parts {xref('part-viii','VIII')} and {xref('part-ix','IX')}"),
        (r"Parts VIII and IX", f"Parts {xref('part-viii','VIII')} and {xref('part-ix','IX')}"),
        (
            rf"Parts {xref('part-iv','Part IV')}--{xref('part-vi','Part VI')}",
            f"Parts {xref('part-iv','Part IV')} through {xref('part-vi','Part VI')}",
        ),
        (r"Parts III through V", f"Parts {xref('part-iii','Part III')} through {xref('part-v','Part V')}"),
        (r"Part IV, Part VIII", f"{xref('part-iv','Part IV')}, {xref('part-viii','Part VIII')}"),
        (r"Part III, Part VIII Ch 36", f"{xref('part-iii','Part III')}, {xref('part-viii','Part VIII')} {xref('ch-36-schema-pipeline-together','Chapter 36')}"),
        (
            r"Part III, Part VIII Ch (\d+)",
            lambda m: (
                f"{xref('part-iii','Part III')}, {xref('part-viii','Part VIII')} "
                f"{xref(CHAPTER_ANCHORS[int(m.group(1))], f'Chapter {m.group(1)}')}"
            ),
        ),
        (r"Part IV, Part VIII Ch 37", f"{xref('part-iv','Part IV')}, {xref('part-viii','Part VIII')} {xref('ch-37-stage-boundaries','Chapter 37')}"),
        (r"Part V, Part VIII Ch 38", f"{xref('part-v','Part V')}, {xref('part-viii','Part VIII')} {xref('ch-38-performance-tuning','Chapter 38')}"),
    ]
    for pattern, repl in replacements:
        text = re.sub(pattern, repl, text)

    text = re.sub(
        r"Part VIII Ch (\d+)",
        lambda m: f"{xref('part-viii','Part VIII')} {xref(CHAPTER_ANCHORS[int(m.group(1))], f'Chapter {m.group(1)}')}",
        text,
    )
    text = re.sub(
        r"Part VIII, Chapter (\d+)",
        lambda m: f"{xref('part-viii','Part VIII')}, {xref(CHAPTER_ANCHORS[int(m.group(1))], f'Chapter {m.group(1)}')}",
        text,
    )
    return text


def linkify_parts_and_chapters(text: str) -> str:
    """Link lone Part/Chapter mentions in plain prose only."""
    text = re.sub(
        r"Chapters (\d+) and (\d+)",
        lambda m: (
            f"Chapters {xref(CHAPTER_ANCHORS[int(m.group(1))], f'Chapter {m.group(1)}')} "
            f"and {xref(CHAPTER_ANCHORS[int(m.group(2))], f'Chapter {m.group(2)}')}"
        ),
        text,
    )
    segments = re.split(r"(<<[^>]+>>)", text)
    out: list[str] = []
    for seg in segments:
        if seg.startswith("<<"):
            out.append(seg)
            continue
        for n in range(9, 0, -1):
            roman = ROMAN[n]
            anchor = PART_ANCHORS[n]
            seg = re.sub(
                rf"\bPart {roman}'s\b",
                f"{xref(anchor, f'Part {roman}')}'s",
                seg,
            )
        for num in sorted(CHAPTER_ANCHORS.keys(), reverse=True):
            seg = re.sub(
                rf"\bChapter {num}\b",
                xref(CHAPTER_ANCHORS[num], f"Chapter {num}"),
                seg,
            )
        seg = re.sub(
            r"\bCh\. (\d+)\b",
            lambda m: xref(CHAPTER_ANCHORS[int(m.group(1))], f"Chapter {m.group(1)}"),
            seg,
        )
        for n in range(9, 0, -1):
            roman = ROMAN[n]
            anchor = PART_ANCHORS[n]
            seg = re.sub(rf"\bPart {roman}\b", xref(anchor, f"Part {roman}"), seg)
        faq_rows = xref("faq-rows-not-visible", "Why don't I see my rows yet?")
        seg = seg.replace('FAQ\'s "Why don\'t I see my rows yet?"', f"FAQ's {faq_rows}")
        out.append(seg)
    return "".join(out)


def linkify_prose(text: str) -> str:
    text = linkify_compounds(text)
    segments = re.split(r"(<<[^>]+>>)", text)
    return "".join(
        seg if seg.startswith("<<") else linkify_parts_and_chapters(seg) for seg in segments
    )


def linkify_segment(text: str) -> str:
    return linkify_prose(text)


def linkify_plain(text: str) -> str:
    return linkify_segment(text)


def linkify_outside_xrefs(text: str) -> str:
    return linkify_segment(text)


def process_book_intro() -> None:
    path = BOOK_DIR / "book.adoc"
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    out: list[str] = []
    for line in lines:
        if line.startswith("= SimDB") or line.startswith("include::"):
            out.append(line)
            continue
        if line.strip().startswith(":") or line.strip().startswith("[") or line.strip().startswith("//"):
            out.append(line)
            continue
        out.append(normalize_xref_syntax(linkify_segment(line)))
    path.write_text("".join(out), encoding="utf-8")


def process_file(rel: str) -> None:
    path = BOOK_DIR / rel
    text = fix_part_titles(path.read_text(encoding="utf-8"))
    lines = text.splitlines(keepends=True)
    out: list[str] = []
    in_literal = False
    source_pending = False
    for line in lines:
        if line.startswith("[source") or line.startswith("[,cpp]") or line.startswith("[,bash]"):
            source_pending = True
            out.append(line)
            continue
        if line.strip() == "----":
            if source_pending:
                source_pending = False
                in_literal = True
            elif in_literal:
                in_literal = False
            else:
                in_literal = True
            out.append(line)
            continue
        if line.startswith("...."):
            in_literal = not in_literal
            out.append(line)
            continue
        if in_literal:
            out.append(line)
            continue
        if line.startswith("= ") and not line.startswith("=="):
            out.append(line)
            continue
        if line.strip().startswith("[#"):
            out.append(line)
            continue
        out.append(normalize_xref_syntax(linkify_outside_xrefs(line)))
    path.write_text("".join(out), encoding="utf-8")


def main() -> None:
    ensure_anchors()
    for rel in PART_FILES:
        process_file(rel)
    process_book_intro()
    print("Cross-references added.")


if __name__ == "__main__":
    main()
