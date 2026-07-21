import wx, sqlite3, os
from functools import partial
from viewer.gui.widgets.playback_bar import PlaybackBar
from viewer.gui.inspector import DataInspector
from viewer.gui.widgets.widget_renderer import WidgetRenderer
from viewer.gui.widgets.widget_creator import WidgetCreator
from viewer.gui.canvas_grid import CanvasGrid, WidgetContainer
from viewer.model.data_retriever import DataRetriever
from viewer.model.dtype_inspector import DataTypeInspector
from viewer.model.simhier import SimHierarchy
from viewer.gui.widgets.splitter_window import DirtySplitterWindow
from viewer.gui.view_settings import DirtyReasons

class ArgosFrame(wx.Frame):
    def __init__(self, db_path, view_settings):
        super().__init__(None, title=db_path)
        
        self.db = sqlite3.connect(db_path)
        self.view_settings = view_settings
        self.dtype_inspector = DataTypeInspector(db_path)
        self.simhier = SimHierarchy(self.db, self.dtype_inspector)
        self.widget_renderer = WidgetRenderer(self)
        self.widget_creator = WidgetCreator(self)
        self.data_retriever = DataRetriever(self, db_path, self.simhier, self.dtype_inspector)
        self.inspector = DataInspector(self, self)
        self.playback_bar = PlaybackBar(self)

        self.menu_bar = wx.MenuBar()
        file_menu = wx.Menu()

        new_view = file_menu.Append(wx.ID_NEW, '&New View\tCtrl+N', 'Create a new view')
        open_view = file_menu.Append(wx.ID_OPEN, '&Open View\tCtrl+O', 'Open an existing view')
        save_view = file_menu.Append(wx.ID_SAVE, '&Save View\tCtrl+S', 'Save the current view')
        file_menu.AppendSeparator()
        exit_argos = file_menu.Append(wx.ID_EXIT, 'E&xit\tAlt+F4', 'Exit Argos')

        self.Bind(wx.EVT_MENU, self.__OnNewView, new_view)
        self.Bind(wx.EVT_MENU, self.__OnOpenView, open_view)
        self.Bind(wx.EVT_MENU, self.__OnSaveView, save_view)
        self.Bind(wx.EVT_MENU, lambda event: self.Close(), exit_argos)

        self.menu_bar.Append(file_menu, '&File')
        self.SetMenuBar(self.menu_bar)

        # Layout
        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(self.inspector, 1, wx.EXPAND)
        sizer.Add(self.playback_bar, 0, wx.EXPAND)
        self.SetSizer(sizer)
        self.Layout()
        self.Maximize()

    def PostLoad(self, view_file):
        self.view_settings.PostLoad(self, view_file)

    def CreateResourceBitmap(self, filename, size=(16, 16)):
        w,h = size
        resources_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'resources'))
        filepath = os.path.join(resources_dir, filename)
        bitmap = wx.Bitmap(filepath, wx.BITMAP_TYPE_PNG)
        bitmap = bitmap.ConvertToImage().Rescale(w,h).ConvertToBitmap()
        return bitmap

    def CreateWidgetStandardButtons(self, widget, settings_callback, settings_tooltip):
        std_size = (36,22)

        settings_btn = wx.Button(widget, label=">>", size=std_size)
        settings_btn.Bind(wx.EVT_BUTTON, settings_callback)
        settings_btn.SetToolTip(settings_tooltip)

        def ClearWidget(evt, widget):
            if isinstance(widget, WidgetContainer):
                widget.DestroyAllWidgets()
                self.view_settings.SetDirty(reason=DirtyReasons.WidgetRemoved)
            else:
                ClearWidget(evt, widget.GetParent())

        clear_btn = wx.Button(widget, label="X", size=std_size)
        clear_btn.Bind(wx.EVT_BUTTON, partial(ClearWidget, widget=widget))
        clear_btn.SetToolTip("Clear widget")

        canvas_grid = widget.GetParent()
        while canvas_grid is not None and not isinstance(canvas_grid, CanvasGrid):
            canvas_grid = canvas_grid.GetParent()

        assert canvas_grid is not None
        splitters = []
        canvas_grid.AddQuickLinks(splitters)

        split_lr = wx.Button(widget, label="|", size=std_size)
        split_lr.Bind(wx.EVT_BUTTON, splitters[0][1])
        split_lr.SetToolTip("Split left/right")

        split_tb = wx.Button(widget, label="-", size=std_size)
        split_tb.Bind(wx.EVT_BUTTON, splitters[1][1])
        split_tb.SetToolTip("Split top/bottom")

        maximize_btn = wx.Button(widget, label="@", size=std_size)
        maximize_btn.Bind(wx.EVT_BUTTON, splitters[2][1])
        maximize_btn.SetToolTip("Clear all widgets except this one")

        return settings_btn, clear_btn, split_lr, split_tb, maximize_btn

    def __OnNewView(self, event):
        self.view_settings.CreateNewView()

    def __OnOpenView(self, event):
        self.view_settings.OpenView()

    def __OnSaveView(self, event):
        self.view_settings.SaveView()
