import re


_LEGACY_ROW_RE = re.compile(r"^\s*(\d+)\t(.+)$")
_STRUCTURED_ROW_RE = re.compile(r"^\s*(\d+)\s+bytes?,\s*([^,]+)(?:,\s*value\s+.*)?$")


def read_trace_rows(path: str, allowed_descriptions: set[str] | None = None) -> list[tuple[str, str]]:
    """
    Parse either legacy TSV trace rows or new structured indented rows.

    Returns normalized (num_bytes_str, description) tuples.
    """
    rows: list[tuple[str, str]] = []
    with open(path, "r", encoding="utf-8") as f:
        for i, line in enumerate(f):
            if i == 0:
                continue
            line = line.strip()
            if not line:
                continue

            # Structured trace headers.
            if line == "record" or line.endswith(":"):
                continue

            m = _LEGACY_ROW_RE.match(line)
            if m:
                rows.append((m.group(1), m.group(2)))
                continue

            m = _STRUCTURED_ROW_RE.match(line)
            if m:
                desc = m.group(2).strip()
                if allowed_descriptions is not None and desc not in allowed_descriptions:
                    continue
                rows.append((m.group(1), desc))
                continue

            raise RuntimeError(f"Malformed trace row in {path!r}: {line!r}")
    return rows
