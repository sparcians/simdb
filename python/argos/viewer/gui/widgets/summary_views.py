import wx, copy, re
from collections import OrderedDict
from functools import partial
from viewer.model.data_deserializers import StructDeserializer
from viewer.gui.view_settings import DirtyReasons

class SummaryViews(wx.Panel):
    def __init__(self, parent, frame):
        super().__init__(parent)
        self.frame = frame
        self.gear_btn = None
        self.placeholder_label = None
        self.summary_scroller = None
        self.summary = None
        self._summary_grid_dirty = True
        self._elem_paths_by_cid = {}
        self.__ShowEmptyPlaceholder()

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
            self.__DestroyEmptyPlaceholder()

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
            self.__ShowEmptyPlaceholder()

    def __DestroyEmptyPlaceholder(self):
        if self.gear_btn:
            self.gear_btn.Destroy()
            self.gear_btn = None

        if self.placeholder_label:
            self.placeholder_label.Destroy()
            self.placeholder_label = None

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

    def __ShowEmptyPlaceholder(self):
        self.__DestroyEmptyPlaceholder()

        if self.GetSizer():
            self.GetSizer().Clear()

        self.SetBackgroundColour('light gray')

        self.gear_btn = self.frame.CreateSettingsButton(self)
        self.gear_btn.Bind(wx.EVT_BUTTON, self.EditWidget)
        self.placeholder_label = wx.StaticText(self, label='Make selections')

        hsizer = wx.BoxSizer(wx.HORIZONTAL)
        hsizer.AddSpacer(5)
        hsizer.Add(self.gear_btn, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)
        hsizer.Add(self.placeholder_label, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)

        vsizer = wx.BoxSizer(wx.VERTICAL)
        vsizer.AddSpacer(5)
        vsizer.Add(hsizer, 0, wx.EXPAND)

        self.SetSizer(vsizer)
        self.Layout()

    def EditWidget(self, evt):
        dlg = SummaryViewsEditDialog(self, self.frame, self.elem_paths)
        result = dlg.ShowModal()
        if result == wx.ID_OK:
            elem_paths = dlg.GetSelectedElemPaths()
        dlg.Destroy()
        if result == wx.ID_OK:
            self.ApplyViewSettings({'elem_paths': elem_paths})

class SummaryGrid(wx.Panel):
    def __init__(self, parent, frame, elem_paths, summary_views):
        wx.Panel.__init__(self, parent)
        self.frame = frame
        self.summary_views = summary_views
        self.value_handlers = {}
        self.gear_btn = None

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

        self.gear_btn = frame.CreateSettingsButton(self)
        self.gear_btn.Bind(wx.EVT_BUTTON, self.summary_views.EditWidget)
        self.gear_btn.SetToolTip('Edit widget settings')

        row = 0
        sizer.Add(self.gear_btn, pos=(row,0))

        max_label_len = 0
        for parent, leaves in collectable_grps.items():
            max_label_len = max(max_label_len, len(parent))
            for leaf in leaves:
                max_label_len = max(max_label_len, len(leaf))

        row += 1
        for parent, leaves in collectable_grps.items():
            parent_label = wx.StaticText(self, label=parent)
            parent_label.SetFont(mono10_bold)
            sizer.Add(parent_label, pos=(row,0))
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
                sizer.Add(leaf_label, pos=(row,0))

                leaf_cid = frame.simhier.GetCollectionID(full_path)
                if leaf_cid in frame.simhier.GetContainerIDs():
                    capacity = frame.simhier.GetCapacityByCollectionID(leaf_cid)
                    summary_handler = SummaryGrid.ContainerSummary(self, frame, capacity)
                else:
                    dtype_name = frame.dtype_inspector.GetDataTypeForCollectionID(leaf_cid)
                    if dtype_name in ('char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned int', 'long', 'unsigned long'):
                        summary_handler = SummaryGrid.IntegerSummary(self, frame)
                    elif isinstance(frame.dtype_inspector.GetDeserializer(dtype_name), StructDeserializer):
                        summary_handler = SummaryGrid.StructSummary(self, frame)
                    else:
                        summary_handler = SummaryGrid.SimpleSummary(self, frame)

                summary_handler.SetFont(mono10)
                sizer.Add(summary_handler, pos=(row,4))
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
                label = ' '.join(label)
                self.SetLabel(label)
                self.SetToolTip(label)
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

class SummaryViewsEditDialog(wx.Dialog):
    _TREE_STYLE = wx.TR_DEFAULT_STYLE | wx.TR_HIDE_ROOT | wx.TR_LINES_AT_ROOT | wx.TR_MULTIPLE

    def __init__(self, parent, frame, elem_paths):
        _, screen_h = wx.GetDisplaySize()
        super().__init__(parent, title='Edit Summary Views', size=(600, int(screen_h * 0.75)))

        self.frame = frame
        self.simhier = frame.simhier
        self._all_leaf_paths = list(self.simhier.GetElemPaths(True))

        initial_paths = set(elem_paths)
        self._selected_paths = [path for path in self._all_leaf_paths if path in initial_paths]
        self._initial_paths = list(self._selected_paths)

        self._avail_tree_items_by_id = {}
        self._avail_leaf_paths_by_tree_item = {}
        self._avail_placeholder_item = None
        self._sel_tree_items_by_id = {}
        self._sel_leaf_paths_by_tree_item = {}
        self._sel_placeholder_item = None

        available_label = wx.StaticText(self, label='Select collectables to add:')
        self.available_tree = wx.TreeCtrl(self, style=self._TREE_STYLE)

        self.add_btn = wx.Button(self, label='Add Selections')
        self.add_btn.Bind(wx.EVT_BUTTON, self.__OnAddSelections)
        self.add_btn.Disable()

        add_action_sizer = wx.BoxSizer(wx.HORIZONTAL)
        add_action_sizer.Add(self.add_btn, 0, wx.ALL, 5)

        selections_label = wx.StaticText(self, label='Displayed collectables:')
        self.selections_tree = wx.TreeCtrl(self, style=self._TREE_STYLE)

        self.remove_btn = wx.Button(self, label='Remove Selections')
        self.remove_btn.Bind(wx.EVT_BUTTON, self.__OnRemoveSelections)
        self.remove_btn.Disable()

        remove_action_sizer = wx.BoxSizer(wx.HORIZONTAL)
        remove_action_sizer.Add(self.remove_btn, 0, wx.ALL, 5)

        btn_sizer = wx.StdDialogButtonSizer()
        self.ok_btn = wx.Button(self, wx.ID_OK)
        btn_sizer.AddButton(self.ok_btn)
        btn_sizer.AddButton(wx.Button(self, wx.ID_CANCEL))
        btn_sizer.Realize()

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(available_label, 0, wx.ALL, 5)
        sizer.Add(self.available_tree, 1, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)
        sizer.Add(add_action_sizer, 0, wx.LEFT | wx.RIGHT | wx.BOTTOM, 5)
        sizer.Add(selections_label, 0, wx.ALL, 5)
        sizer.Add(self.selections_tree, 1, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)
        sizer.Add(remove_action_sizer, 0, wx.LEFT | wx.RIGHT | wx.BOTTOM, 5)
        sizer.Add(btn_sizer, 0, wx.ALL | wx.ALIGN_RIGHT, 10)
        self.SetSizer(sizer)

        self.available_tree.Bind(wx.EVT_TREE_SEL_CHANGING, self.__OnAvailableTreeSelectionChanging)
        self.available_tree.Bind(wx.EVT_TREE_SEL_CHANGED, self.__OnAvailableTreeSelectionChanged)
        self.available_tree.Bind(wx.EVT_RIGHT_DOWN, partial(self.__OnTreeRightClick, tree=self.available_tree))
        self.selections_tree.Bind(wx.EVT_TREE_SEL_CHANGING, self.__OnSelectionsTreeSelectionChanging)
        self.selections_tree.Bind(wx.EVT_TREE_SEL_CHANGED, self.__OnSelectionsTreeSelectionChanged)
        self.selections_tree.Bind(wx.EVT_RIGHT_DOWN, partial(self.__OnTreeRightClick, tree=self.selections_tree))

        self.__RebuildBothTrees()
        self.__UpdateButtonStates()

    def GetSelectedElemPaths(self):
        return list(self._selected_paths)

    def __OnAvailableTreeSelectionChanging(self, evt):
        if self.__IsPlaceholderItem(self.available_tree, evt.GetItem()):
            evt.Veto()
            return

        evt.Skip()

    def __OnSelectionsTreeSelectionChanging(self, evt):
        if self.__IsPlaceholderItem(self.selections_tree, evt.GetItem()):
            evt.Veto()
            return

        evt.Skip()

    def __OnAvailableTreeSelectionChanged(self, evt):
        self.__UpdateButtonStates()
        evt.Skip()

    def __OnSelectionsTreeSelectionChanged(self, evt):
        self.__UpdateButtonStates()
        evt.Skip()

    def __OnTreeRightClick(self, event, tree):
        item = tree.HitTest(event.GetPosition())
        if not item:
            return

        item = item[0]
        if not item.IsOk():
            return

        if self.__IsPlaceholderItem(tree, item):
            return

        tree.SelectItem(item)
        self.__PopupTreeExpandCollapseMenu(tree)

    def __PopupTreeExpandCollapseMenu(self, tree):
        menu = wx.Menu()

        def ExpandAll(evt, **kwargs):
            kwargs['tree'].ExpandAll()
            evt.Skip()

        def CollapseAll(evt, **kwargs):
            kwargs['tree'].CollapseAll()
            evt.Skip()

        expand_all = menu.Append(-1, 'Expand All')
        self.Bind(wx.EVT_MENU, partial(ExpandAll, tree=tree), expand_all)

        collapse_all = menu.Append(-1, 'Collapse All')
        self.Bind(wx.EVT_MENU, partial(CollapseAll, tree=tree), collapse_all)

        tree.PopupMenu(menu)
        menu.Destroy()

    def __OnAddSelections(self, evt):
        paths_to_add = []
        for item in self.available_tree.GetSelections():
            if item.IsOk():
                paths_to_add.extend(
                    self.__CollectLeavesFromItem(
                        self.available_tree, self._avail_leaf_paths_by_tree_item, item,
                    )
                )

        self.__AddSelectedPaths(paths_to_add)

    def __OnRemoveSelections(self, evt):
        paths_to_remove = set()
        for item in self.selections_tree.GetSelections():
            if item.IsOk():
                paths_to_remove.update(
                    self.__CollectLeavesFromItem(
                        self.selections_tree, self._sel_leaf_paths_by_tree_item, item,
                    )
                )

        if not paths_to_remove:
            return

        self._selected_paths = [path for path in self._selected_paths if path not in paths_to_remove]
        self.__RebuildBothTrees()
        self.__UpdateButtonStates()

    def __AddSelectedPaths(self, paths):
        selected = set(self._selected_paths)
        changed = False
        for path in paths:
            if path not in selected:
                selected.add(path)
                changed = True

        if changed:
            self._selected_paths = [path for path in self._all_leaf_paths if path in selected]
            self.__RebuildBothTrees()
            self.__UpdateButtonStates()

    def __UpdateButtonStates(self):
        self.add_btn.Enable(
            bool(self.available_tree.GetSelections()) and bool(self._avail_leaf_paths_by_tree_item)
        )
        self.remove_btn.Enable(
            bool(self.selections_tree.GetSelections()) and bool(self._sel_leaf_paths_by_tree_item)
        )
        self.ok_btn.Enable(self._selected_paths != self._initial_paths)

    def __RebuildBothTrees(self):
        self.__RebuildAvailableTree()
        self.__RebuildSelectionsTree()

    def __RebuildAvailableTree(self):
        hidden = set(self._selected_paths)
        visible_leaves = [path for path in self._all_leaf_paths if path not in hidden]
        (
            self._avail_tree_items_by_id,
            self._avail_leaf_paths_by_tree_item,
            self._avail_placeholder_item,
        ) = self.__BuildTree(
            self.available_tree, visible_leaves, empty_label='(everything selected)',
        )

    def __RebuildSelectionsTree(self):
        (
            self._sel_tree_items_by_id,
            self._sel_leaf_paths_by_tree_item,
            self._sel_placeholder_item,
        ) = self.__BuildTree(
            self.selections_tree, self._selected_paths, empty_label='(nothing selected)',
        )

    def __IsPlaceholderItem(self, tree, item):
        if not item or not item.IsOk():
            return False

        if tree is self.available_tree:
            placeholder_item = self._avail_placeholder_item
        else:
            placeholder_item = self._sel_placeholder_item

        return placeholder_item is not None and item == placeholder_item

    def __BuildTree(self, tree_ctrl, leaf_elem_paths, empty_label=None):
        tree_items_by_id = {}
        leaf_paths_by_tree_item = {}
        placeholder_item = None

        tree_ctrl.DeleteAllItems()
        root = tree_ctrl.AddRoot('root')
        tree_items_by_id[0] = root

        if not leaf_elem_paths:
            if empty_label:
                placeholder_item = tree_ctrl.AppendItem(root, empty_label)
            return tree_items_by_id, leaf_paths_by_tree_item, placeholder_item

        visible_paths = self.__BuildVisibleElemPaths(leaf_elem_paths)
        self.__RecurseBuildTree(
            tree_ctrl, self.simhier.GetTree().GetRoot(), visible_paths,
            tree_items_by_id, leaf_paths_by_tree_item,
        )

        return tree_items_by_id, leaf_paths_by_tree_item, placeholder_item

    def __RecurseBuildTree(self, tree_ctrl, node, visible_paths, tree_items_by_id, leaf_paths_by_tree_item):
        if node is self.simhier.GetTree().GetRoot():
            for child in node.GetChildren():
                self.__RecurseBuildTree(
                    tree_ctrl, child, visible_paths, tree_items_by_id, leaf_paths_by_tree_item,
                )
            return

        elem_path = node.GetPath()
        if elem_path not in visible_paths:
            return

        if node.GetParent():
            parent_id = node.GetParent().GetID()
        else:
            parent_id = 0

        tree_item = tree_ctrl.AppendItem(tree_items_by_id[parent_id], node.GetName())
        node_id = node.GetID()
        tree_items_by_id[node_id] = tree_item

        if not node.children:
            leaf_paths_by_tree_item[tree_item] = elem_path

        for child in node.GetChildren():
            self.__RecurseBuildTree(
                tree_ctrl, child, visible_paths, tree_items_by_id, leaf_paths_by_tree_item,
            )

    def __CollectLeavesFromItem(self, tree, leaf_paths_by_tree_item, item):
        if item in leaf_paths_by_tree_item:
            return [leaf_paths_by_tree_item[item]]

        paths = []
        child, cookie = tree.GetFirstChild(item)
        while child.IsOk():
            paths.extend(self.__CollectLeavesFromItem(tree, leaf_paths_by_tree_item, child))
            child = tree.GetNextSibling(child)

        return paths

    @staticmethod
    def __BuildVisibleElemPaths(leaf_elem_paths):
        visible_paths = set()
        for leaf_path in leaf_elem_paths:
            parts = leaf_path.split('.')
            for i in range(1, len(parts) + 1):
                visible_paths.add('.'.join(parts[:i]))
        return visible_paths
