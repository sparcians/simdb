import wx, re
from collections import OrderedDict
from viewer.model.data_deserializers import StructDeserializer
from viewer.gui.view_settings import DirtyReasons
from viewer.gui.dialogs.widget_data_selections import SummaryViewsEditDlg
from viewer.gui.widgets.scheduling_lines import CaptionManager

class SummaryViews(wx.Panel):
    DEFAULT_SHOW_FULL_PATHS = True
    DEFAULT_SHOW_DID = False

    def __init__(self, parent, frame, show_full_paths=DEFAULT_SHOW_FULL_PATHS, show_did=DEFAULT_SHOW_DID):
        super().__init__(parent)
        self.frame = frame
        self.show_full_paths = show_full_paths
        self.show_did = show_did
        self.summary_scroller = None
        self.summary = None
        self._summary_grid_dirty = True
        self._elem_paths_by_cid = {}

    @property
    def elem_paths(self):
        return list(self._elem_paths_by_cid.values())

    @property
    def cids(self):
        return list(self._elem_paths_by_cid.keys())

    def GetWidgetCreationString(self):
        return 'Summary Views'

    def GetErrorIfDroppedNodeIncompatible(self, elem_path):
        if elem_path in self.elem_paths:
            return 'This collectable is already being displayed.', 'Duplicate Collectable'

        return None

    def UpdateWidgetData(self):
        self.__Refresh()

    def GetCurrentViewSettings(self):
        settings = {}
        settings['elem_paths'] = self.elem_paths
        settings['show_full_paths'] = self.show_full_paths
        settings['show_did'] = self.show_did
        return settings

    def GetCurrentUserSettings(self):
        return {}

    def ApplyViewSettings(self, settings):
        show_full_paths = settings.get('show_full_paths', self.DEFAULT_SHOW_FULL_PATHS)
        show_did = settings.get('show_did', self.DEFAULT_SHOW_DID)
        paths_changed = self.elem_paths != settings['elem_paths']
        dirty = (
            paths_changed
            or self.show_full_paths != show_full_paths
            or self.show_did != show_did
            or self._summary_grid_dirty
        )
        if not dirty:
            return

        if paths_changed or self.show_full_paths != show_full_paths:
            self._summary_grid_dirty = True

        self._elem_paths_by_cid = {
            self.frame.simhier.GetCollectionID(path):path
            for path in settings['elem_paths']
        }
        self.show_full_paths = show_full_paths
        self.show_did = show_did

        self.frame.view_settings.SetDirty(reason=DirtyReasons.SummaryViewsWidgetChanged)
        self.__Refresh()

    def AddElement(self, elem_path):
        if elem_path in self.elem_paths:
            return

        cid = self.frame.simhier.GetCollectionID(elem_path)
        self._elem_paths_by_cid[cid] = elem_path
        self._summary_grid_dirty = True
        self.__Refresh()

    def EditWidget(self, evt, title="Edit Data Selections"):
        dlg = SummaryViewsEditDlg(
            self, self.frame, self.elem_paths, self.show_full_paths, self.show_did, title=title,
        )
        result = dlg.ShowModal()
        if result == wx.ID_OK:
            elem_paths = dlg.GetSelectedElemPaths()
            show_full_paths = dlg.show_full_paths
            show_did = dlg.show_did
        dlg.Destroy()
        if result == wx.ID_OK:
            self.ApplyViewSettings({
                'elem_paths': elem_paths,
                'show_full_paths': show_full_paths,
                'show_did': show_did,
            })
            return True
        else:
            return False

    def __Refresh(self):
        if len(self.elem_paths):
            self.SetBackgroundColour('white')
            self.__RegenerateSummary()
        else:
            if self.summary_scroller:
                self.summary_scroller.Destroy()
                self.summary_scroller = None
                self.summary = None
            elif self.summary:
                self.summary.Destroy()
                self.summary = None

            self._summary_grid_dirty = True

    def __RegenerateSummary(self):
        assert len(self.elem_paths)
        if self._summary_grid_dirty:
            if self.summary_scroller:
                self.summary_scroller.Destroy()
                self.summary_scroller = None
                self.summary = None

            self.summary_scroller = wx.ScrolledWindow(self, style=wx.VSCROLL | wx.HSCROLL)
            self.summary_scroller.SetScrollRate(10, 10)
            self.summary = SummaryGrid(self.summary_scroller, self.frame, self.elem_paths, self)

            scroller_sizer = wx.BoxSizer(wx.VERTICAL)
            scroller_sizer.Add(self.summary, 0)
            self.summary_scroller.SetSizer(scroller_sizer)

            if not self.GetSizer():
                self.SetSizer(wx.BoxSizer(wx.HORIZONTAL))
            hsizer = self.GetSizer()
            hsizer.Clear()
            hsizer.AddSpacer(5)
            hsizer.Add(self.summary_scroller, 1, wx.EXPAND | wx.ALL, 5)

            self.summary.Layout()
            self.summary_scroller.Layout()
            self.summary_scroller.FitInside()

            self._summary_grid_dirty = False

        self.summary.UpdateWidgetData()
        self.Layout()

class SummaryGrid(wx.Panel):
    def __init__(self, parent, frame, elem_paths, summary_views):
        wx.Panel.__init__(self, parent)
        self.frame = frame
        self.summary_views = summary_views
        self.value_handlers = {}

        gear_btn, clear_btn, split_lr, split_tb, maximize_btn = frame.CreateWidgetStandardButtons(
            self, summary_views.EditWidget, 'Edit data selections')

        btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
        btn_sizer.Add(gear_btn, 0, wx.TOP | wx.RIGHT, 5)
        btn_sizer.Add(clear_btn, 0, wx.TOP | wx.RIGHT, 5)
        btn_sizer.Add(split_lr, 0, wx.TOP | wx.RIGHT, 5)
        btn_sizer.Add(split_tb, 0, wx.TOP | wx.RIGHT, 5)
        btn_sizer.Add(maximize_btn, 0, wx.TOP, 5)

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(btn_sizer, 0, wx.BOTTOM, 5)

        # Group all element leaf paths by their parents
        collectable_grps = OrderedDict()
        for p in elem_paths:
            idx = p.rfind('.')
            assert idx != -1
            assert idx + 1 < len(p)
            parent = p[:idx]
            if parent not in collectable_grps:
                collectable_grps[parent] = []
            collectable_grps[parent].append(p[idx+1:])

        # Now we have a dict that looks like this:
        #
        #   top.cpu.core0.decode
        #     foo
        #     bar
        #   top.cpu.core0.retire
        #     baz
        grid_sizer = wx.GridBagSizer(vgap=5, hgap=5)
        mono10 = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        mono10_bold = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_BOLD)

        max_label_len = 0
        parent_paths = list(collectable_grps.keys())
        for parent, leaves in collectable_grps.items():
            if summary_views.show_full_paths:
                parent_label_text = parent
            else:
                parent_label_text = CaptionManager.GetMinimumUniqueSuffix(parent, parent_paths)
            max_label_len = max(max_label_len, len(parent_label_text))
            for leaf in leaves:
                max_label_len = max(max_label_len, len(leaf))

        row = 0
        for parent, leaves in collectable_grps.items():
            if summary_views.show_full_paths:
                parent_label_text = parent
            else:
                parent_label_text = CaptionManager.GetMinimumUniqueSuffix(parent, parent_paths)
            parent_label = wx.StaticText(self, label=parent_label_text)
            parent_label.SetFont(mono10_bold)
            CaptionManager.ApplyPartialPathTooltip(
                parent_label, parent, parent_label_text, summary_views.show_full_paths)
            grid_sizer.Add(parent_label, pos=(row,0))
            row += 1

            for leaf in leaves:
                full_path = parent + '.' + leaf
                num_dashes = max_label_len - len(leaf) + 1
                if num_dashes > 0:
                    label = '-'*num_dashes + ' '
                else:
                    label = ''
                label += leaf
                leaf_label = wx.StaticText(self, label=label)
                leaf_label.SetFont(mono10)
                CaptionManager.ApplyPartialPathTooltip(
                    leaf_label, full_path, label, summary_views.show_full_paths)
                grid_sizer.Add(leaf_label, pos=(row,0))

                leaf_cid = frame.simhier.GetCollectionID(full_path)
                if leaf_cid in frame.simhier.GetContainerIDs():
                    capacity = frame.simhier.GetCapacityByCollectionID(leaf_cid)
                    summary_handler = SummaryGrid.ContainerSummary(self, frame, capacity)
                else:
                    dtype_name = frame.dtype_inspector.GetDataTypeForCollectionID(leaf_cid)
                    if dtype_name in ('char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned int', 'long', 'unsigned long'):
                        summary_handler = SummaryGrid.IntegerSummary(self, frame)
                    elif isinstance(frame.dtype_inspector.GetDeserializer(dtype_name), StructDeserializer):
                        summary_handler = SummaryGrid.StructSummary(self, frame, summary_views)
                    else:
                        summary_handler = SummaryGrid.SimpleSummary(self, frame)

                summary_handler.SetFont(mono10)
                grid_sizer.Add(summary_handler, pos=(row,4))
                grid_sizer.AddGrowableRow(row)
                self.value_handlers[full_path] = summary_handler
                row += 1

        sizer.Add(grid_sizer)
        self.SetSizer(sizer)
        self.Layout()

    def UpdateWidgetData(self):
        if not self.value_handlers:
            return

        current_tick = self.frame.widget_renderer.tick
        elem_paths = list(self.value_handlers.keys())
        all_data = self.frame.data_retriever.UnpackRange(current_tick, current_tick, elem_paths)

        for elem_path, elem_data in all_data.items():
            handler = self.value_handlers[elem_path]
            if len(elem_data['DataVals']) == 1:
                value = elem_data['DataVals'][0]
            else:
                value = None

            handler.UpdateValue(value)

    class SimpleSummary(wx.StaticText):
        def __init__(self, parent, frame):
            wx.StaticText.__init__(self, parent, label='TODO')
            self.frame = frame

        def UpdateValue(self, value):
            self.SetLabel(str(value))

    class IntegerSummary(wx.StaticText):
        def __init__(self, parent, frame):
            wx.StaticText.__init__(self, parent, label='TODO')
            self.frame = frame
            self.hex = False

        def UpdateValue(self, value):
            value = str(value)
            if self.hex:
                value = hex(value)
            self.SetLabel(value)

    class StructSummary(wx.StaticText):
        def __init__(self, parent, frame, summary_views):
            wx.StaticText.__init__(self, parent, label='TODO')
            self.frame = frame
            self.summary_views = summary_views
            self.auto_color = True

        def UpdateValue(self, value):
            label = []
            if value is not None:
                for field_name, field_value in value:
                    if field_name == 'DID' and not self.summary_views.show_did:
                        continue
                    field_value = str(field_value)
                    field_value = re.sub(r'\s+', ' ', field_value)
                    label.append(f'{field_name}({field_value})')
                label = ' '.join(label)
                self.SetLabel(label)
                self.SetToolTip(label)
            else:
                self.SetLabel('(no data)')
                self.UnsetToolTip()

        def __HandleContextMenu(self, evt, auto_color):
            self.auto_color = auto_color

    class ContainerSummary(wx.StaticText):
        def __init__(self, parent, frame, capacity):
            wx.StaticText.__init__(self, parent, label='TODO')
            self.frame = frame
            self.capacity = capacity
            assert self.capacity > 0

        def UpdateValue(self, value):
            if value:
                size = len(value)
                pct = f"{100.0 * size / self.capacity:.1f}%"
                label = f'{pct} full ({size}/{self.capacity})'
                self.SetLabel(label)
            else:
                self.SetLabel('(no data)')
