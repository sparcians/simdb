import wx, copy, re
from collections import OrderedDict
from functools import partial
from viewer.model.data_deserializers import StructDeserializer
from viewer.gui.view_settings import DirtyReasons

class SummaryViews(wx.Panel):
    def __init__(self, parent, frame):
        super().__init__(parent)
        self.frame = frame
        self.info = None
        self.summary = None
        self._summary_grid_dirty = True
        self._elem_paths_by_cid = {}
        self.__ShowUsageInfo()

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
        return settings

    def GetCurrentUserSettings(self):
        return {}

    def ApplyViewSettings(self, settings):
        dirty = self.elem_paths != settings['elem_paths'] or self._summary_grid_dirty
        if not dirty:
            return

        self._elem_paths_by_cid = {
            self.frame.simhier.GetCollectionID(path):path
            for path in settings['elem_paths']
        }

        self.frame.view_settings.SetDirty(reason=DirtyReasons.SummaryViewsWidgetChanged)
        self._summary_grid_dirty = True
        self.__Refresh()

    def AddElement(self, elem_path):
        if elem_path in self.elem_paths:
            return

        cid = self.frame.simhier.GetCollectionID(elem_path)
        self._elem_paths_by_cid[cid] = elem_path
        self._summary_grid_dirty = True
        self.__Refresh()

    def __Refresh(self):
        if len(self.elem_paths):
            if self.info:
                self.info.Hide()

            self.SetBackgroundColour('white')
            self.__RegenerateSummary()

    def __RegenerateSummary(self):
        assert len(self.elem_paths)
        if self._summary_grid_dirty:
            if self.summary:
                self.summary.Destroy()
            self.summary = SummaryGrid(self, self.frame, self.elem_paths)

            if not self.GetSizer():
                self.SetSizer(wx.BoxSizer(wx.HORIZONTAL))
            self.GetSizer().Clear()
            self.GetSizer().Add(self.summary)

            self._summary_grid_dirty = False

        self.summary.UpdateWidgetData()
        self.Layout()

    def __ShowUsageInfo(self):
        if self.info:
            self.info.Destroy()
            self.info = None

        if self.GetSizer():
            self.GetSizer().Clear()

        self.SetBackgroundColour('light gray')

        self.info = wx.StaticText(self, label='Drag collectables from the Queues/Scalars tree to create summary views.')#, size=(750,18))
        self.info.SetFont(wx.Font(14, wx.FONTFAMILY_DEFAULT, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL))

        vsizer = wx.BoxSizer(wx.VERTICAL)
        vsizer.AddStretchSpacer()
        vsizer.Add(self.info, 1, wx.ALL | wx.CENTER | wx.EXPAND, 5)
        vsizer.AddStretchSpacer()

        hsizer = wx.BoxSizer(wx.HORIZONTAL)
        hsizer.AddStretchSpacer()
        hsizer.Add(vsizer, 0, wx.CENTER | wx.EXPAND)
        hsizer.AddStretchSpacer()

        self.SetSizer(hsizer)
        self.Layout()

class SummaryGrid(wx.Panel):
    def __init__(self, parent, frame, elem_paths):
        wx.Panel.__init__(self, parent)
        self.frame = frame
        self.value_handlers = {}

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
        sizer = wx.GridBagSizer(vgap=5, hgap=5)
        mono10 = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        mono10_bold = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_BOLD)

        max_parent_len = 0
        for parent, leaves in collectable_grps.items():
            max_parent_len = max(max_parent_len, len(parent))

        row = 0
        for parent, leaves in collectable_grps.items():
            parent_label = wx.StaticText(self, label=parent)
            parent_label.SetFont(mono10_bold)
            sizer.Add(parent_label, pos=(row,0))
            row += 1

            for leaf in leaves:
                full_path = parent + '.' + leaf
                num_dashes = max_parent_len - len(leaf) - 1
                if num_dashes > 0:
                    label = '-'*num_dashes + ' '
                else:
                    label = ''
                label += leaf
                leaf_label = wx.StaticText(self, label=label)
                leaf_label.SetFont(mono10)
                sizer.Add(leaf_label, pos=(row,0))

                leaf_cid = self.frame.simhier.GetCollectionID(full_path)
                if leaf_cid in self.frame.simhier.GetContainerIDs():
                    capacity = self.frame.simhier.GetCapacityByCollectionID(leaf_cid)
                    summary_handler = SummaryGrid.ContainerSummary(self, self.frame, capacity)
                else:
                    dtype_name = self.frame.dtype_inspector.GetDataTypeForCollectionID(leaf_cid)
                    if dtype_name in ('char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned int', 'long', 'unsigned long'):
                        summary_handler = SummaryGrid.IntegerSummary(self, self.frame)
                    elif isinstance(self.frame.dtype_inspector.GetDeserializer(dtype_name), StructDeserializer):
                        summary_handler = SummaryGrid.StructSummary(self, self.frame)
                    else:
                        summary_handler = SummaryGrid.SimpleSummary(self, self.frame)

                summary_handler.SetFont(mono10)
                sizer.Add(summary_handler, pos=(row,2))
                sizer.AddGrowableRow(row)
                self.value_handlers[full_path] = summary_handler
                row += 1

        self.SetSizer(sizer)
        self.Layout()

    def UpdateWidgetData(self):
        if not self.value_handlers:
            return

        current_tick = self.frame.widget_renderer.tick
        elem_paths = list(self.value_handlers.keys())
        all_data = self.frame.data_retriever.UnpackRange(current_tick, current_tick, elem_paths)

        #import pdb; pdb.set_trace()
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
        def __init__(self, parent, frame):
            wx.StaticText.__init__(self, parent, label='TODO')
            self.frame = frame
            self.auto_color = True

        def UpdateValue(self, value):
            label = []
            if value is not None:
                for field_name, field_value in value:
                    field_value = str(field_value)
                    field_value = re.sub(r'\s+', ' ', field_value)
                    label.append(f'{field_name}({field_value})')
                self.SetLabel(' '.join(label))
            else:
                self.SetLabel('(none)')

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
                self.SetLabel('(empty)')
