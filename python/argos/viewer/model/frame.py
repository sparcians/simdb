import wx, sqlite3, os
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

class ArgosFrame(wx.Frame):
    def __init__(self, db_path, view_settings):
        super().__init__(None, title=db_path)

        self.db_path = db_path
        self.db = sqlite3.connect(db_path)
        self.view_settings = view_settings
        self._clocks_by_id = None
        self.dtype_inspector = DataTypeInspector(db_path)
        self.simhier = SimHierarchy(self.db, self.dtype_inspector)
        self.widget_renderer = WidgetRenderer(self)
        self.widget_creator = WidgetCreator(self)
        self.data_retriever = DataRetriever(self, db_path, self.simhier, self.dtype_inspector)
        self.inspector = DataInspector(self, self)
        self.playback_bar = PlaybackBar(self)
        self.search_results_panel = SearchResultsPanel(self)

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

        self.menu_bar.Append(file_menu, '&File')

        find_menu = wx.Menu()
        search_widgets = find_menu.Append(-1, '&Search widgets\tCtrl-F', 'Search widgets')
        search_everywhere = find_menu.Append(-1, '&Search everywhere\tCtrl-Shift-F', 'Search everywhere')
        self.Bind(wx.EVT_MENU, self.__SearchWidgets, search_widgets)
        self.Bind(wx.EVT_MENU, self.__SearchEverywhere, search_everywhere)
        self.menu_bar.Append(find_menu, '&Search')

        self.SetMenuBar(self.menu_bar)

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
            return CreateButton(widget, label=label, size=std_size)

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
                  cycle_constraints: list = []):

        data_constraints = Search.NormalizeDataConstraints(data_constraints)
        assert data_constraints
        assert not path_constraints or path_constraints in self.simhier.GetElemPaths(leaves_only=True)
        assert len(cycle_constraints) in (0,2)

        results = []

        # Give first priority to the on-screen search (find this data on existing widgets).
        for tab in self.inspector.tabs:
            for wc in tab.GetWidgetContainers():
                widget = wc.GetWidget()
                if widget:
                    result = widget.CreateSearchResults(data_constraints, path_constraints)
                    if result:
                        results.extend(result)

        results = [r for r in results if r is not None]

        if cycle_constraints:
            selected_clock = self.playback_bar.clock_combobox.GetValue()
            period = self.playback_bar.clock_periods.get(selected_clock)
            tick_range = [period * cyc for cyc in cycle_constraints]

            # These tick/CID pairs isolate the DB blob records that contain
            # the matches. They are not consumed yet.
            tick_cid_nvps_tuples = self.__FindMatchingTickCIDs(
                tick_range, data_constraints, path_constraints)

            results.extend([
                self.__CreateDatabaseSearchResult(tick, cid, nvps) for tick,cid,nvps in tick_cid_nvps_tuples
            ])

        self.__ShowSearchResults(results)

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

        tick_cid_nvps_tuples = set()
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
                        tick_cid_nvps_tuples.add((tick, cid, struct))
                        break

        return sorted(tick_cid_nvps_tuples)

    def __OnNewView(self, event):
        self.view_settings.CreateNewView()

    def __OnOpenView(self, event):
        self.view_settings.OpenView()

    def __OnSaveView(self, event):
        self.view_settings.SaveView()

    def __SearchWidgets(self, evt):
        self.__RunSearchDialog([])

    def __SearchEverywhere(self, evt):
        self.__RunSearchDialog(['start', 'end'])

    def __RunSearchDialog(self, cycle_range: list):
        dlg = Search(self, cycle_range=cycle_range)
        if dlg.ShowModal() == wx.ID_OK:
            data_constraints = dlg.GetDataConstraints()
            path_constraints = dlg.GetPathConstraints()
            cycle_constraints = dlg.GetCycleConstraints()
            self.RunSearch(data_constraints, path_constraints, cycle_constraints)

        dlg.Destroy()

    def __GetClockForElemPath(self, elem_path):
        if self._clocks_by_id is None:
            cursor = self.db.cursor()
            cursor.execute('SELECT Id,Name,Period FROM Clocks')
            self._clocks_by_id = {r[0]: (r[1], r[2]) for r in cursor.fetchall()}

        clk_id = self.simhier.GetMetaAtPath(elem_path, 'ClkID')
        return self._clocks_by_id.get(clk_id, ('', None))

    def __ShowSearchResults(self, results):
        # They all have a 'widget' field and an 'nvps' field. We will show a wx.ListCtrl
        # with the columns: (auto-color/tag), tick, cyc, clock, path, widget, data.
        #
        # Some results have a 'tick' field, but for those that don't we will assume
        # it is the current tick.
        entries = []
        for r in results:
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

            entries.append((tick, cycle, clock_name, r.elem_path, type(r.widget).__name__, data, color, tag))

        self.search_results_panel.ShowResults(entries)

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

        def BringIntoFocus(self):
            print(f"TODO XXX: BringIntoFocus {self}")

        def TakeOutOfFocus(self):
            print(f"TODO XXX: TakeOutOfFocus {self}")
