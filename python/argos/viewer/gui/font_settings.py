class FontRole:
    """Identifies a group of UI elements that share one user-configurable font size."""
    BUTTONS = 'buttons'
    TABS = 'tabs'
    METADATA = 'metadata'
    GRID_LABELS = 'grid_labels'
    GRID_CELLS = 'grid_cells'
    SEARCH_TITLEBAR = 'search_titlebar'
    SEARCH_RESULTS = 'search_results'

class FontSettings:
    MIN_SIZE = 6
    MAX_SIZE = 32

    # (group name, ((role, label, default size), ...)), in the order shown in the Fonts dialog.
    GROUPS = (
        ('Global', (
            (FontRole.BUTTONS, 'Buttons', 10),
            (FontRole.TABS, 'Tabs', 10),
        )),
        ('Widgets', (
            (FontRole.METADATA, 'Metadata', 10),
            (FontRole.GRID_LABELS, 'Grid labels', 12),
            (FontRole.GRID_CELLS, 'Grid cells', 10),
        )),
        ('Search', (
            (FontRole.SEARCH_TITLEBAR, 'Titlebar', 10),
            (FontRole.SEARCH_RESULTS, 'Results', 10),
        )),
    )

    DEFAULTS = {role: size for _, roles in GROUPS for role, _, size in roles}

    def __init__(self):
        self._sizes = dict(self.DEFAULTS)

    def GetSize(self, role):
        return self._sizes[role]

    def GetSizes(self):
        return dict(self._sizes)

    def SetSizes(self, sizes):
        """Applies the given {role: point size} entries and returns the set of roles whose
        size changed. Unknown roles and invalid sizes are ignored, so settings files
        written by other versions can still be loaded."""
        changed = set()
        for role, size in (sizes or {}).items():
            if role not in self._sizes:
                continue

            if isinstance(size, bool) or not isinstance(size, int):
                continue

            size = min(max(size, self.MIN_SIZE), self.MAX_SIZE)
            if self._sizes[role] != size:
                self._sizes[role] = size
                changed.add(role)

        return changed
