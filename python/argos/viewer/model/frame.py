import wx, sqlite3, os, time, weakref
from functools import partial
from viewer.gui.widgets.playback_bar import PlaybackBar
from viewer.gui.widgets.search_results_panel import SearchResultsPanel
from viewer.gui.inspector import DataInspector
from viewer.gui.widgets.widget_renderer import WidgetRenderer
from viewer.gui.widgets.widget_creator import WidgetCreator
from viewer.gui.canvas_grid import CanvasGrid, WidgetContainer
from viewer.model.data_retriever import DataRetriever
from viewer.model.dtype_inspector import DataTypeInspector
from viewer.model.simhier import SimHierarchy
from viewer.gui.widgets.splitter_window import DirtySplitterWindow
from viewer.gui.view_settings import DirtyReasons
from viewer.gui.widgets.button import CreateButton
from viewer.gui.dialogs.search import Search
from viewer.gui.dialogs.font_settings import FontSettingsDlg
from viewer.gui.font_settings import FontSettings, FontRole

def _SetRequesterFont(requester, font):
    requester.SetFont(font)

class _TrackedFont:
    __slots__ = ('requester', 'role', 'family', 'style', 'weight', 'size_offset', 'apply')

    def __init__(self, requester, role, family, style, weight, size_offset, apply):
        self.requester = weakref.ref(requester)
        self.role = role
        self.family = family
        self.style = style
        self.weight = weight
        self.size_offset = size_offset
        self.apply = apply

class ArgosFrame(wx.Frame):
    MAX_SEARCH_HISTORY = 20

    # Buttons created with an explicit size have that size at this font point size,
    # and are scaled proportionally when the button font size changes.
    BUTTON_BASE_POINT_SIZE = 10

    def __init__(self, db_path, view_settings):
        super().__init__(None, title=db_path)

        # Font sizes are user settings (not part of the layout). Widgets register the
        # fonts they use with TrackFont() so they can be updated when the sizes change.
        self.font_settings = FontSettings()
        self._tracked_fonts = []
        self._prune_tracked_fonts_at = 256

        self.db_path = db_path
        self.db = sqlite3.connect(db_path)
        self.view_settings = view_settings
        self._clocks_by_id = None

        # (data_constraints, path_constraints) of the search whose results are being shown, if any.
        self._active_search = None

        # The data constraints most recently searched for in the Search dialog, newest first.
        self.search_history = []

        # (start, end, unit) of the last database search. Kept for the session only.
        self._last_db_range = None
        self.dtype_inspector = DataTypeInspector(db_path)
        self.simhier = SimHierarchy(self.db, self.dtype_inspector)
        self.widget_renderer = WidgetRenderer(self)
        self.widget_creator = WidgetCreator(self)
        self.data_retriever = DataRetriever(self, db_path, self.simhier, self.dtype_inspector)
        self.inspector = DataInspector(self, self)
        self.playback_bar = PlaybackBar(self)
        self.search_results_panel = SearchResultsPanel(self)
        self.search_results_panel.on_hide = self.__OnSearchResultsHidden
        self.search_results_panel.validate_db_range = self.__ValidateDatabaseRange
        self.search_results_panel.on_search_db = self.RunDatabaseSearch
        self.search_results_panel.on_go_to_db_result = self.__GoToDatabaseResult

        self.menu_bar = wx.MenuBar()
        file_menu = wx.Menu()

        new_view = file_menu.Append(wx.ID_NEW, '&New Layout\tCtrl+N', 'Create a new layout')
        open_view = file_menu.Append(wx.ID_OPEN, '&Open Layout\tCtrl+O', 'Open an existing layout')
        save_view = file_menu.Append(wx.ID_SAVE, '&Save Layout\tCtrl+S', 'Save the current layout')
        file_menu.AppendSeparator()
        exit_argos = file_menu.Append(wx.ID_EXIT, 'E&xit\tAlt+F4', 'Exit Argos')

        self.Bind(wx.EVT_MENU, self.__OnNewView, new_view)
        self.Bind(wx.EVT_MENU, self.__OnOpenView, open_view)
        self.Bind(wx.EVT_MENU, self.__OnSaveView, save_view)
        self.Bind(wx.EVT_MENU, lambda event: self.Close(), exit_argos)

        settings_menu = wx.Menu()
        fonts = settings_menu.Append(wx.ID_ANY, '&Fonts...', 'Change the font sizes used throughout Argos')
        self.Bind(wx.EVT_MENU, self.__OnFontSettings, fonts)

        self.menu_bar.Append(file_menu, '&File')
        self.menu_bar.Append(settings_menu, '&Settings')
        self.SetMenuBar(self.menu_bar)

        # Ctrl-F opens the Search dialog. There is no menu item for it, so it needs its own accelerator.
        self._search_id = wx.NewIdRef()
        self.Bind(wx.EVT_MENU, self.__RunSearchDialog, id=self._search_id)
        self.SetAcceleratorTable(wx.AcceleratorTable([(wx.ACCEL_CTRL, ord('F'), self._search_id)]))

        # Layout
        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(self.inspector, 1, wx.EXPAND)
        sizer.Add(self.search_results_panel, 0, wx.EXPAND)
        sizer.Add(self.playback_bar, 0, wx.EXPAND)
        self.SetSizer(sizer)

    def PostLoad(self, view_file):
        self.view_settings.PostLoad(self, view_file)
        if not self.view_settings.tick_overridden:
            time_vals = self.data_retriever.GetAllTimeVals()
            if time_vals:
                self.widget_renderer.GoToTick(time_vals[len(time_vals) // 2])
        self.__AutoMaximizeSingleWidget()

    def __AutoMaximizeSingleWidget(self):
        if self.inspector.GetTabNames() != ['Tab 1']:
            return

        containers = self.inspector.tabs[0].GetWidgetContainers()
        widgets = [c.GetWidget() for c in containers if c.GetWidget() is not None]
        if len(widgets) != 1:
            return

        btn = self.__FindMaximizeButton(widgets[0])
        if btn is not None:
            self.inspector.SelectTab(self.inspector.tabs[0])
            event = wx.CommandEvent(wx.EVT_BUTTON.typeId, btn.GetId())
            event.SetEventObject(btn)
            btn.GetEventHandler().ProcessEvent(event)
            # Do not dirty the layout. This forced button click is a workaround
            # for single-SchedulingLines layouts on first load ("ghost" buttons
            # appear and you have to mouseover to get rid of them).
            self.view_settings.SetDirty(False)

    def __FindMaximizeButton(self, window):
        for child in window.GetChildren():
            if isinstance(child, wx.Button) and child.GetLabel() == "@":
                return child
            btn = self.__FindMaximizeButton(child)
            if btn is not None:
                return btn
        return None

    def CreateResourceBitmap(self, filename, size=(16, 16)):
        w,h = size
        resources_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'resources'))
        filepath = os.path.join(resources_dir, filename)
        bitmap = wx.Bitmap(filepath, wx.BITMAP_TYPE_PNG)
        bitmap = bitmap.ConvertToImage().Rescale(w,h).ConvertToBitmap()
        return bitmap

    def CreateWidgetStandardButtons(self, widget, settings_callback, settings_tooltip):
        std_size = (36,22)
        def CreateTheButton(label):
            return self.CreateTrackedButton(widget, label=label, size=std_size)

        settings_btn = CreateTheButton(">>")
        settings_btn.Bind(wx.EVT_BUTTON, settings_callback)
        settings_btn.SetToolTip(settings_tooltip)

        def ClearWidget(evt, widget):
            if isinstance(widget, WidgetContainer):
                widget.DestroyAllWidgets()
                self.view_settings.SetDirty(reason=DirtyReasons.WidgetRemoved)
            else:
                ClearWidget(evt, widget.GetParent())

        clear_btn = CreateTheButton("X")
        clear_btn.Bind(wx.EVT_BUTTON, partial(ClearWidget, widget=widget))
        clear_btn.SetToolTip("Clear widget")

        canvas_grid = widget.GetParent()
        while canvas_grid is not None and not isinstance(canvas_grid, CanvasGrid):
            canvas_grid = canvas_grid.GetParent()

        assert canvas_grid is not None
        splitters = []
        canvas_grid.AddQuickLinks(splitters)

        split_lr = CreateTheButton("|")
        split_lr.Bind(wx.EVT_BUTTON, splitters[0][1])
        split_lr.SetToolTip("Split left/right")

        split_tb = CreateTheButton("-")
        split_tb.Bind(wx.EVT_BUTTON, splitters[1][1])
        split_tb.SetToolTip("Split top/bottom")

        maximize_btn = CreateTheButton("@")
        maximize_btn.Bind(wx.EVT_BUTTON, splitters[2][1])
        maximize_btn.SetToolTip("Clear all widgets except this one")

        return settings_btn, clear_btn, split_lr, split_tb, maximize_btn

    def RunSearch(self,
                  data_constraints: str,
                  path_constraints: str = '',
                  range_unit: str = None):

        data_constraints = Search.NormalizeDataConstraints(data_constraints)
        assert data_constraints
        assert not path_constraints or path_constraints in self.simhier.GetElemPaths(leaves_only=True)

        start_time = time.perf_counter()
        cycle_constraints = []#['start', 'end']
        results = []

        # Give first priority to the on-screen search (find this data on existing widgets).
        # Each result is paired with the name of the tab it was found on.
        tab_names = self.inspector.GetTabNames()
        for tab, tab_name in zip(self.inspector.tabs, tab_names):
            for wc in tab.GetWidgetContainers():
                widget = wc.GetWidget()
                if widget:
                    result = widget.CreateSearchResults(data_constraints, path_constraints)
                    if result:
                        results.extend((r, tab_name) for r in result if r is not None)

        self._active_search = (data_constraints, path_constraints)

        search_text = ' '.join(f'{k}({v})' for k, v in data_constraints.items())
        if path_constraints:
            search_text += f' in {path_constraints}'

        panel = self.search_results_panel
        panel.ShowResults(self.__CreateSearchEntries(results), search_text, self.__RefreshSearch)

        # The database range comes from the caller (e.g. 'start'/'end' for Search
        # everywhere), otherwise the last range that was searched is offered again.
        if cycle_constraints:
            unit = range_unit or self.__GetDefaultRangeUnit()
            panel.SetDatabaseRange(str(cycle_constraints[0]), str(cycle_constraints[1]), unit)
        else:
            panel.SetDatabaseRange(*self.__GetInitialDatabaseRange())

        # Widgets highlight any data matching the active search while the results are shown.
        self.inspector.RefreshWidgetsOnAllTabs()

        if cycle_constraints:
            self.__RunDatabaseSearch(*panel.GetDatabaseRange())

        panel.SetElapsedTime(time.perf_counter() - start_time)

    def RunDatabaseSearch(self, start_text: str, end_text: str, unit: str):
        """Searches the database over the given range for the active search, replacing
        any database results already shown."""
        start_time = time.perf_counter()
        if self.__RunDatabaseSearch(start_text, end_text, unit):
            self.search_results_panel.SetElapsedTime(time.perf_counter() - start_time)

    def __RunDatabaseSearch(self, start_text: str, end_text: str, unit: str):
        # Returns whether a search was run (False if there is no active search or the range is invalid).
        if self._active_search is None:
            return False

        data_constraints, path_constraints = self._active_search
        try:
            tick_range = self.__ResolveDatabaseRange(start_text, end_text, unit)
        except ValueError as ex:
            wx.MessageBox(str(ex), 'Search DB', wx.OK | wx.ICON_ERROR)
            return False

        self._last_db_range = (start_text, end_text, unit)

        wx.BeginBusyCursor()
        try:
            # These tick/CID pairs isolate the DB blob records that contain the matches.
            tick_cid_nvps_tuples = self.__FindMatchingTickCIDs(
                tick_range, data_constraints, path_constraints)
        finally:
            wx.EndBusyCursor()

        results = [
            (self.__CreateDatabaseSearchResult(tick, cid, nvps), None) for tick,cid,nvps in tick_cid_nvps_tuples
        ]
        self.search_results_panel.SetDatabaseResults(self.__CreateSearchEntries(results))
        return True

    def __RefreshSearch(self):
        if self._active_search is None:
            return

        data_constraints, path_constraints = self._active_search
        panel = self.search_results_panel
        db_range = panel.GetDatabaseRange()
        if panel.HasDatabaseResults():
            self.RunSearch(data_constraints, path_constraints, list(db_range[:2]), db_range[2])
        else:
            # Keep whatever is typed in the database inputs.
            self.RunSearch(data_constraints, path_constraints)
            panel.SetDatabaseRange(*db_range)

    def __GetSelectedClockPeriod(self):
        period = self.playback_bar.clock_periods.get(self.playback_bar.clock_combobox.GetValue())
        return int(period) if period else None

    def __GetDefaultRangeUnit(self):
        return 'cycles' if self.__GetSelectedClockPeriod() else 'ticks'

    DEFAULT_RANGE_WINDOW_CYCLES = 100

    def __GetInitialDatabaseRange(self):
        default_unit = self.__GetDefaultRangeUnit()
        if self._last_db_range and self._last_db_range[2] in (default_unit, 'ticks'):
            return self._last_db_range

        # Searching the whole database can take minutes, so start from a window around
        # the current time. Without a selected clock the window is sized with the slowest
        # clock's period, so it still covers at least that many cycles of every clock.
        window = self.DEFAULT_RANGE_WINDOW_CYCLES
        if default_unit == 'ticks':
            window *= max(int(period) for period in self.playback_bar.clock_periods.values())
        return (f'-{window}', f'+{window}', default_unit)

    def __ResolveDatabaseRange(self, start_text, end_text, unit):
        """Returns the (start_tick, end_tick) of the text range, or raises a ValueError
        with a user-facing message."""
        widget_renderer = self.widget_renderer
        start_tick = int(widget_renderer.start_tick)
        end_tick = int(widget_renderer.end_tick)
        current_tick = int(widget_renderer.tick)

        if unit == 'cycles':
            period = self.__GetSelectedClockPeriod()
            if not period:
                raise ValueError('A clock must be selected to search by cycles')

            start_cycle, end_cycle = Search.ResolveCycleRange(
                start_text, end_text, start_tick // period, end_tick // period, current_tick // period)
            return start_cycle * period, end_cycle * period

        return Search.ResolveCycleRange(start_text, end_text, start_tick, end_tick, current_tick, unit='tick')

    def __ValidateDatabaseRange(self, start_text, end_text, unit):
        try:
            self.__ResolveDatabaseRange(start_text, end_text, unit)
        except ValueError as ex:
            return str(ex)
        return None

    def __GoToDatabaseResult(self, tick):
        # With a clock selected we go to that clock's cycle, otherwise to the exact tick.
        tick = int(tick)
        period = self.__GetSelectedClockPeriod()
        if period:
            tick = tick // period * period
        self.widget_renderer.GoToTick(tick)

    def MatchesActiveSearch(self, elem_path, nvps):
        """Whether the name/value pairs (a dict or a list of pairs) found at elem_path
        satisfy the active search, so that widgets can highlight them."""
        if self._active_search is None or not nvps:
            return False

        data_constraints, path_constraints = self._active_search
        if path_constraints and elem_path != path_constraints:
            return False

        nvps = dict(nvps)
        return all(k in nvps and str(nvps[k]) == str(v) for k, v in data_constraints.items())

    def __OnSearchResultsHidden(self):
        if self._active_search is not None:
            self._active_search = None
            self.inspector.RefreshWidgetsOnAllTabs()

    def __CreateDatabaseSearchResult(self, tick, cid, nvps):
        return self.SearchResult(self, tick, cid, nvps)

    def __FindMatchingTickCIDs(self, tick_range, data_constraints, path_constraints=''):
        elem_paths = [path_constraints] if path_constraints else self.simhier.GetItemElemPaths()
        unpacked = self.data_retriever.UnpackRange(tick_range[0], tick_range[1], elem_paths)
        constraints = [(k, str(v)) for k, v in data_constraints.items()]

        def Matches(nvps):
            return all(nvps.get(k) == v for k, v in constraints)

        def IsStruct(value):
            # Structs are lists of (name, value) pairs. Plain scalars have no
            # names to match against.
            return isinstance(value, (list, tuple)) and \
                all(isinstance(pair, (list, tuple)) and len(pair) == 2 and isinstance(pair[0], str)
                    for pair in value)

        # At most one match per element and tick. Keyed rather than a set of tuples
        # because a struct is a list and so can't be hashed.
        matches = {}
        for elem_path, vals in unpacked.items():
            cid = self.simhier.GetCollectionID(elem_path)
            for tick, data in zip(vals['TimeVals'], vals['DataVals']):
                if not data:
                    continue

                # Scalar structs hold one list of pairs; containers hold one
                # such list per bin (or None for an empty bin).
                structs = [data] if IsStruct(data) else \
                    [d for d in data if d and IsStruct(d)] if isinstance(data, (list, tuple)) else []

                for struct in structs:
                    if Matches({k: str(v) for k, v in struct}):
                        matches[(tick, cid)] = struct
                        break

        return [(tick, cid, struct) for (tick, cid), struct in sorted(matches.items(), key=lambda kv: kv[0])]

    def __OnNewView(self, event):
        self.view_settings.CreateNewView()

    def __OnOpenView(self, event):
        self.view_settings.OpenView()

    def __OnSaveView(self, event):
        self.view_settings.SaveView()

    def __RunSearchDialog(self, event):
        dlg = Search(self)
        if dlg.ShowModal() == wx.ID_OK:
            data_constraints = dlg.GetDataConstraints()
            path_constraints = dlg.GetPathConstraints()
            self.__AddToSearchHistory(dlg.GetDataConstraintsText())
            self.RunSearch(data_constraints, path_constraints)

        dlg.Destroy()

    def __AddToSearchHistory(self, text):
        if text in self.search_history:
            self.search_history.remove(text)
        self.search_history.insert(0, text)
        del self.search_history[self.MAX_SEARCH_HISTORY:]

    def GetCurrentUserSettings(self):
        return {
            'search_history': list(self.search_history),
            'fonts': self.font_settings.GetSizes()
        }

    def ApplyUserSettings(self, settings):
        history = settings.get('search_history') or []
        self.search_history = [str(text) for text in history][:self.MAX_SEARCH_HISTORY]

        # Widgets were already created with the default sizes, so they have to be updated
        # and refreshed. Not in settings files written by older versions.
        if self.SetFontSizes(settings.get('fonts')):
            wx.CallAfter(self.inspector.RefreshWidgetsOnAllTabs)

    def TrackFont(self, requester, role, family=wx.FONTFAMILY_MODERN, style=wx.FONTSTYLE_NORMAL,
                  weight=wx.FONTWEIGHT_NORMAL, size_offset=0, apply=None):
        """Creates a font for 'requester' whose point size is the user's size for the given
        FontRole (plus 'size_offset'). Whenever that size changes, 'apply(requester, font)' is
        called with a new font; by default this is requester.SetFont(font). The caller is
        responsible for using the returned font initially. The requester is not kept alive."""
        if len(self._tracked_fonts) >= self._prune_tracked_fonts_at:
            self._tracked_fonts = [t for t in self._tracked_fonts if t.requester() is not None]
            self._prune_tracked_fonts_at = max(256, 2 * len(self._tracked_fonts))

        tracked = _TrackedFont(requester, role, family, style, weight, size_offset, apply)
        self._tracked_fonts.append(tracked)
        return self.__CreateTrackedFont(tracked)

    def SetFontSizes(self, sizes):
        """Updates the user's font sizes ({FontRole: point size}) and every tracked font that
        uses a size that changed. Returns True if anything changed. This does not refresh
        the widgets' contents, and does not save the settings."""
        changed_roles = self.font_settings.SetSizes(sizes)
        if not changed_roles:
            return False

        live_fonts = []
        for tracked in self._tracked_fonts:
            requester = tracked.requester()
            if requester is None:
                continue

            if tracked.role in changed_roles:
                apply = tracked.apply or _SetRequesterFont
                try:
                    apply(requester, self.__CreateTrackedFont(tracked))
                except RuntimeError:
                    # The wx object was destroyed, but its Python wrapper is still alive.
                    continue

            live_fonts.append(tracked)

        self._tracked_fonts = live_fonts
        self.__RelayoutAll(self)
        self.Refresh()
        return True

    def __RelayoutAll(self, window):
        # Controls change size with their font, so every level of nested sizers must be laid out again.
        for child in window.GetChildren():
            self.__RelayoutAll(child)

        if window.GetSizer():
            window.Layout()

    def CreateTrackedButton(self, parent, label, size=wx.DefaultSize):
        button = CreateButton(parent, label=label, size=size)
        width, height = size
        if width > 0 and height > 0:
            button.argos_base_size = (width, height)

        font = self.TrackFont(button, FontRole.BUTTONS, family=wx.FONTFAMILY_DEFAULT, apply=self.__ApplyButtonFont)
        self.__ApplyButtonFont(button, font)
        return button

    def __CreateTrackedFont(self, tracked):
        size = max(4, self.font_settings.GetSize(tracked.role) + tracked.size_offset)
        return wx.Font(size, tracked.family, tracked.style, tracked.weight)

    def __ApplyButtonFont(self, button, font):
        button.SetFont(font)
        base_size = getattr(button, 'argos_base_size', None)
        if base_size:
            scale = font.GetPointSize() / self.BUTTON_BASE_POINT_SIZE
            size = (round(base_size[0] * scale), round(base_size[1] * scale))
            button.SetMinSize(size)
            button.SetSize(size)

    def __OnFontSettings(self, event):
        dlg = FontSettingsDlg(self, self.font_settings.GetSizes())
        try:
            if dlg.ShowModal() == wx.ID_OK and self.SetFontSizes(dlg.GetSizes()):
                # Font sizes are user settings, so they are saved right away and
                # do not make the layout dirty.
                self.view_settings.SaveUserSettings()
                self.inspector.RefreshWidgetsOnCurrentTab()
        finally:
            dlg.Destroy()

    def __GetClockForElemPath(self, elem_path):
        if self._clocks_by_id is None:
            cursor = self.db.cursor()
            cursor.execute('SELECT Id,Name,Period FROM Clocks')
            self._clocks_by_id = {r[0]: (r[1], r[2]) for r in cursor.fetchall()}

        clk_id = self.simhier.GetMetaAtPath(elem_path, 'ClkID')
        return self._clocks_by_id.get(clk_id, ('', None))

    def __CreateSearchEntries(self, results):
        # 'results' is a list of (result, tab_name) pairs, where tab_name is None for results
        # that came from the database. They all have a 'widget' field and an 'nvps' field.
        # We will show a wx.ListCtrl with the columns: (auto-color/tag), tick, cyc, clock,
        # path, caption, source, data.
        #
        # Some results have a 'tick' field, but for those that don't we will assume
        # it is the current tick.
        #
        # The tab name is only shown in the source when there are multiple widget tabs.
        show_tab_names = len(self.inspector.tabs) > 1
        entries = []
        for r, tab_name in results:
            if hasattr(r, 'tick'):
                tick = r.tick
            else:
                tick = self.widget_renderer.tick

            clock_name, period = self.__GetClockForElemPath(r.elem_path)
            cycle = int(tick) // int(period) if period else ''

            # Widget results hold a dict of name/value pairs; database results hold
            # the raw list of (name, value) pairs from UnpackRange.
            pairs = list(r.nvps.items() if isinstance(r.nvps, dict) else r.nvps or [])
            data = [(str(k), str(v)) for k, v in pairs]
            color, tag = self.__GetSearchResultColorAndTag(r.elem_path, pairs, data)

            if r.widget is None:
                source = 'Database'
            else:
                source = r.widget.GetSearchResultSourceName()
                if show_tab_names and tab_name:
                    source = f'{source} ({tab_name})'

            caption = getattr(r, 'caption', '')

            entries.append((tick, cycle, clock_name, r.elem_path, caption, source, data, color, tag))

        return entries

    def __GetSearchResultColorAndTag(self, elem_path, pairs, data):
        # Same keying as the detailed packet column in SchedulingLinesWidget: the
        # auto-colorize column's value for structs, otherwise the whole text.
        auto_colorize_column = self.data_retriever.GetAutoColorizeColumn(elem_path)
        key = None
        for name, value in pairs:
            if name == auto_colorize_column:
                key = value
                # Widgets hand back stringified values, but the auto-color index is
                # keyed on the raw (integer) values from the database.
                if isinstance(key, str) and key.isdigit():
                    key = int(key)
                break

        if key is None:
            key = ' '.join(f'{k}({v})' for k, v in data)

        return (self.widget_renderer.GetAutoColor(key, elem_path),
                self.widget_renderer.GetAutoTag(key, elem_path))

    class SearchResult:
        def __init__(self, frame, tick, cid, nvps):
            self.widget = None
            self.elem_path = frame.simhier.GetElemPathByCollectionID(cid)
            self.frame = frame
            self.tick = tick
            self.nvps = nvps
