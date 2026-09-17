import re

import wx
from functools import partial


class CaptionsEditDlg(wx.Dialog):
    def __init__(self, parent, custom_captions):
        super().__init__(parent, title='Edit Captions', size=(1000, 450))

        self._preserved_captions = {
            path: caption
            for path, caption in custom_captions.items()
            if self.__IsRangeKey(path)
        }
        self._custom_captions = {}
        self._edit_ctrl = None
        self._edit_item = None
        self._edit_col = None

        self.captions_list = wx.ListCtrl(
            self, style=wx.LC_REPORT | wx.LC_SINGLE_SEL,
        )
        self.captions_list.InsertColumn(0, 'Elem Path', width=400)
        self.captions_list.InsertColumn(1, 'Caption', width=150)

        for path, caption in custom_captions.items():
            if self.__IsRangeKey(path) or caption in (None, '', '<default>'):
                continue
            item = self.captions_list.InsertItem(
                self.captions_list.GetItemCount(), path,
            )
            self.captions_list.SetItem(item, 1, caption)

        add_btn = wx.Button(self, label='+')
        self.remove_btn = wx.Button(self, label='X', size=add_btn.GetSize())
        add_btn.Bind(wx.EVT_BUTTON, self.__OnAddRow)
        self.remove_btn.Bind(wx.EVT_BUTTON, self.__OnRemoveRow)

        row_buttons = wx.BoxSizer(wx.VERTICAL)
        row_buttons.Add(add_btn)
        row_buttons.Add(self.remove_btn, 0, wx.TOP, 5)

        list_sizer = wx.BoxSizer(wx.HORIZONTAL)
        list_sizer.Add(self.captions_list, 1, wx.EXPAND)
        list_sizer.Add(row_buttons, 0, wx.LEFT, 5)

        dialog_buttons = wx.StdDialogButtonSizer()
        ok_btn = wx.Button(self, wx.ID_OK)
        dialog_buttons.AddButton(ok_btn)
        dialog_buttons.AddButton(wx.Button(self, wx.ID_CANCEL))
        dialog_buttons.Realize()

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(list_sizer, 1, wx.ALL | wx.EXPAND, 10)
        sizer.Add(dialog_buttons, 0, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.ALIGN_RIGHT, 10)
        self.SetSizer(sizer)

        self.captions_list.Bind(wx.EVT_LEFT_DCLICK, self.__OnCellDoubleClick)
        self.captions_list.Bind(wx.EVT_LIST_ITEM_SELECTED, self.__UpdateButtonStates)
        self.captions_list.Bind(wx.EVT_LIST_ITEM_DESELECTED, self.__UpdateButtonStates)
        ok_btn.Bind(wx.EVT_BUTTON, self.__OnOk)
        self.__UpdateButtonStates()

    def GetCustomCaptions(self):
        captions = dict(self._preserved_captions)
        captions.update(self._custom_captions)
        return captions

    def __OnAddRow(self, evt):
        self.__CommitCellEdit()
        item = self.captions_list.InsertItem(self.captions_list.GetItemCount(), '')
        self.captions_list.SetItem(item, 1, '')
        self.captions_list.Select(item)
        self.captions_list.EnsureVisible(item)
        self.__BeginCellEdit(item, 0)

    def __OnRemoveRow(self, evt):
        self.__CommitCellEdit()
        item = self.captions_list.GetFirstSelected()
        if item != wx.NOT_FOUND:
            self.captions_list.DeleteItem(item)
        self.__UpdateButtonStates()

    def __OnCellDoubleClick(self, evt):
        item, _, col = self.captions_list.HitTestSubItem(evt.GetPosition())
        if item == wx.NOT_FOUND or col not in (0, 1):
            evt.Skip()
            return
        self.__BeginCellEdit(item, col)

    def __BeginCellEdit(self, item, col):
        self.__CommitCellEdit()

        rect = wx.Rect()
        self.captions_list.GetSubItemRect(item, col, rect)
        self._edit_item = item
        self._edit_col = col
        self._edit_ctrl = wx.TextCtrl(
            self.captions_list,
            value=self.captions_list.GetItemText(item, col),
            style=wx.TE_PROCESS_ENTER,
            pos=rect.GetTopLeft(),
            size=rect.GetSize(),
        )
        self._edit_ctrl.Bind(wx.EVT_TEXT_ENTER, self.__OnCellEditEnter)
        self._edit_ctrl.Bind(wx.EVT_KILL_FOCUS, self.__OnCellEditKillFocus)
        self._edit_ctrl.Bind(wx.EVT_CHAR_HOOK, self.__OnCellEditCharHook)
        self._edit_ctrl.SetFocus()
        self._edit_ctrl.SelectAll()

    def __OnCellEditEnter(self, evt):
        self.__CommitCellEdit()

    def __OnCellEditKillFocus(self, evt):
        self.__CommitCellEdit()
        evt.Skip()

    def __OnCellEditCharHook(self, evt):
        if evt.GetKeyCode() == wx.WXK_ESCAPE:
            self.__DestroyCellEditCtrl()
        else:
            evt.Skip()

    def __CommitCellEdit(self):
        if self._edit_ctrl is None:
            return

        self.captions_list.SetItem(
            self._edit_item, self._edit_col, self._edit_ctrl.GetValue().strip(),
        )
        self.__DestroyCellEditCtrl()

    def __DestroyCellEditCtrl(self):
        ctrl = self._edit_ctrl
        self._edit_ctrl = None
        self._edit_item = None
        self._edit_col = None
        if ctrl is not None:
            wx.CallAfter(ctrl.Destroy)

    def __OnOk(self, evt):
        self.__CommitCellEdit()

        captions = {}
        seen_paths = set()
        for item in range(self.captions_list.GetItemCount()):
            path = self.captions_list.GetItemText(item, 0).strip()
            caption = self.captions_list.GetItemText(item, 1).strip()
            if not path and not caption:
                continue
            if not path:
                wx.MessageBox(
                    'Enter an element path for every caption.',
                    'Invalid Path', wx.OK | wx.ICON_ERROR,
                )
                return
            if not self.__IsValidCaptionKey(path):
                wx.MessageBox(
                    "'{}' is not a valid caption path. Use a dot-delimited path "
                    "with an optional single bin, e.g. 'top.foo' or 'top.foo[4]'.".format(path),
                    'Invalid Path', wx.OK | wx.ICON_ERROR,
                )
                return
            if path in seen_paths:
                wx.MessageBox(
                    "'{}' appears more than once.".format(path),
                    'Duplicate Path', wx.OK | wx.ICON_ERROR,
                )
                return
            seen_paths.add(path)
            if caption:
                captions[path] = caption

        self._custom_captions = captions
        self.EndModal(wx.ID_OK)

    def __UpdateButtonStates(self, *args):
        self.remove_btn.Enable(self.captions_list.GetFirstSelected() != wx.NOT_FOUND)

    @staticmethod
    def __IsRangeKey(path):
        return re.search(r'\[\d+-\d+\]$', path) is not None

    @classmethod
    def __IsValidCaptionKey(cls, path):
        if cls.__IsRangeKey(path):
            return False

        base_path = re.sub(r'\[\d+\]$', '', path)
        if '[' in base_path or ']' in base_path:
            return False
        return bool(base_path) and all(base_path.split('.'))

class WidgetDataSelectionsDlg(wx.Dialog):
    def __init__(
        self, parent, frame, elem_paths, queues_only=False, single_selection=False,
        settings_chkboxes=None, title="Edit Data Selections",
        editable_captions=False, initial_captions=None,
    ):
        assert not editable_captions or not single_selection

        _, screen_h = wx.GetDisplaySize()
        super().__init__(parent, title=title, size=(1000, int(screen_h * 0.75)))

        self.frame = frame
        self.simhier = frame.simhier
        self._settings_chkboxes = settings_chkboxes or []
        self._settings_chkboxes_by_label = {}
        self._editable_captions = editable_captions
        self._captions_by_path = dict(initial_captions or {})
        self._caption_edit_ctrl = None
        self._caption_edit_item = None
        self._path_edit_ctrl = None
        self._path_edit_item = None
        if queues_only:
            self._all_leaf_paths = sorted(self.simhier.GetContainerElemPaths())
        else:
            self._all_leaf_paths = sorted(self.simhier.GetElemPaths(True))

        # Keep every given elem_path, including ones not found in the simhier
        # tree (e.g. manually typed "bad" paths), so they still show up in the
        # ListCtrl rather than silently disappearing on dialog reopen.
        self._selected_paths = []
        seen = set()
        for p in elem_paths:
            if p not in seen:
                self._selected_paths.append(p)
                seen.add(p)

        self._initial_paths = list(self._selected_paths)
        self._single_selection = single_selection
        self._single_selected_path = None

        self._tree_items_by_id = {}
        self._leaf_paths_by_tree_item = {}
        self._list_indices_by_path = {}

        if single_selection and queues_only:
            instruction_text = 'Select a leaf queue from the tree'
        elif single_selection:
            instruction_text = 'Select a leaf node from the tree'
        else:
            instruction_text = 'Right-click nodes to add/remove from widget'

        instruction_label = wx.StaticText(self, label=instruction_text)
        tree_style = wx.TR_DEFAULT_STYLE | wx.TR_HIDE_ROOT | wx.TR_LINES_AT_ROOT
        if not single_selection:
            tree_style = tree_style | wx.TR_MULTIPLE
        self.hier_tree = wx.TreeCtrl(self, style=tree_style)

        if not single_selection:
            self.selections_list = wx.ListCtrl(self, style=wx.LC_REPORT | wx.LC_SINGLE_SEL)
            if self._editable_captions:
                self.selections_list.InsertColumn(0, 'Current Selections', width=400)
                self.selections_list.InsertColumn(1, 'Caption', width=150)
            else:
                self.selections_list.InsertColumn(0, 'Current Selections', width=550)

            self.move_up_btn = wx.BitmapButton(self, bitmap=wx.ArtProvider.GetBitmap(wx.ART_GO_UP, wx.ART_BUTTON))
            self.move_up_btn.Bind(wx.EVT_BUTTON, self.__MoveSelectedElemUp)

            self.move_down_btn = wx.BitmapButton(self, bitmap=wx.ArtProvider.GetBitmap(wx.ART_GO_DOWN, wx.ART_BUTTON))
            self.move_down_btn.Bind(wx.EVT_BUTTON, self.__MoveSelectedElemDown)

            self.add_row_btn = wx.Button(self, label='+', size=self.move_down_btn.GetSize())
            self.add_row_btn.Bind(wx.EVT_BUTTON, self.__OnAddNewRow)

            self.remove_row_btn = wx.Button(self, label='X', size=self.move_down_btn.GetSize())
            self.remove_row_btn.Bind(wx.EVT_BUTTON, self.__OnRemoveSelectedRow)

        btn_sizer = wx.StdDialogButtonSizer()
        self.ok_btn = wx.Button(self, wx.ID_OK)
        btn_sizer.AddButton(self.ok_btn)
        btn_sizer.AddButton(wx.Button(self, wx.ID_CANCEL))
        btn_sizer.Realize()

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(instruction_label, 0, wx.ALL, 5)
        sizer.Add(self.hier_tree, 1, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)
        if not single_selection:
            list_sizer = wx.BoxSizer(wx.HORIZONTAL)
            list_sizer.Add(self.selections_list, 1, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)

            arrow_btns_sizer = wx.BoxSizer(wx.VERTICAL)
            arrow_btns_sizer.Add(self.move_up_btn)
            arrow_btns_sizer.Add(self.move_down_btn)
            arrow_btns_sizer.Add(self.add_row_btn, 0, wx.TOP, 5)
            arrow_btns_sizer.Add(self.remove_row_btn, 0, wx.TOP, 5)
            list_sizer.Add(arrow_btns_sizer)
            sizer.Add(list_sizer, 1, wx.EXPAND)

        self._BuildSettingsArea(sizer)

        sizer.Add(btn_sizer, 0, wx.ALL | wx.ALIGN_RIGHT, 10)
        self.SetSizer(sizer)

        if not single_selection:
            self.hier_tree.Bind(wx.EVT_RIGHT_DOWN, partial(self.__OnTreeRightClick, tree=self.hier_tree))
            self.selections_list.Bind(wx.EVT_LIST_ITEM_SELECTED, self.__UpdateButtonStates)
            self.selections_list.Bind(wx.EVT_LIST_ITEM_DESELECTED, self.__UpdateButtonStates)
            if self._editable_captions:
                self.selections_list.Bind(wx.EVT_LEFT_DCLICK, self.__OnCaptionCellDoubleClick)
        else:
            self.hier_tree.Bind(wx.EVT_TREE_SEL_CHANGED, self.__OnTreeSelectionChanged)
            self.hier_tree.Bind(wx.EVT_RIGHT_DOWN, partial(self.__OnTreeRightClick, tree=self.hier_tree))

        self.__BuildTree()
        self.__BuildSelectionsList()
        self.__UpdateButtonStates()

    def _OnWidgetCheckbox(self, label, checked):
        raise RuntimeError("Not implemented")

    def GetSettingCheckbox(self, label):
        return self._settings_chkboxes_by_label[label].IsChecked()

    def GetCustomCaption(self, elem_path):
        caption = self._captions_by_path.get(elem_path)
        if caption in (None, '', '<default>'):
            return None
        return caption

    def GetCustomCaptions(self):
        return {
            path: caption
            for path, caption in self._captions_by_path.items()
            if caption not in (None, '', '<default>')
        }

    def _SetCustomCaptions(self, custom_captions):
        self._captions_by_path = dict(custom_captions)
        if not self._editable_captions or self._single_selection:
            return

        for item in range(self.selections_list.GetItemCount()):
            path = self.selections_list.GetItemText(item, 0)
            caption = self._captions_by_path.get(path, '<default>')
            self.selections_list.SetItem(item, 1, caption)

    def _BuildSettingsArea(self, sizer):
        if not self._settings_chkboxes:
            return

        for i, (label, checked) in enumerate(self._settings_chkboxes):
            chkbox = wx.CheckBox(self, label=label)
            chkbox.SetValue(checked)
            if i == 0:
                sizer.Add(chkbox)
            else:
                sizer.Add(chkbox, 0, wx.TOP, 5)
            chkbox.Bind(wx.EVT_CHECKBOX, partial(self.__OnSettingsCheckbox, label=label))
            self._settings_chkboxes_by_label[label] = chkbox

    def __OnSettingsCheckbox(self, evt, label):
        if self._settings_chkboxes:
            self._OnWidgetCheckbox(label, evt.IsChecked())
        evt.Skip()

    def GetSelectedElemPaths(self):
        if self._single_selection:
            return [self._single_selected_path] if self._single_selected_path else []

        return list(self._selected_paths)

    def __OnTreeSelectionChanged(self, evt):
        item = self.hier_tree.GetSelection()
        if item.IsOk() and item in self._leaf_paths_by_tree_item:
            self._single_selected_path = self._leaf_paths_by_tree_item[item]
        else:
            self._single_selected_path = None

        self.__UpdateButtonStates()
        evt.Skip()

    def __OnTreeRightClick(self, evt, tree):
        item = tree.HitTest(evt.GetPosition())
        if not item:
            return

        item = item[0]
        if not item.IsOk():
            return

        if self._single_selection:
            tree.SelectItem(item)
        else:
            selections = tree.GetSelections()
            if item not in selections:
                tree.SelectItem(item)
        self.__PopupTreeContextMenu(tree, item)

    def __MoveSelectedElemUp(self, evt):
        selected_rows = self.__GetListCtrlSelectedRows()
        assert len(selected_rows) == 1
        src_row = selected_rows[0]
        assert src_row > 0
        dst_row = src_row - 1
        self.__SwapListCtrlItems(src_row, dst_row)

    def __MoveSelectedElemDown(self, evt):
        selected_rows = self.__GetListCtrlSelectedRows()
        assert len(selected_rows) == 1
        src_row = selected_rows[0]
        dst_row = src_row + 1
        assert dst_row < self.selections_list.GetItemCount()
        self.__SwapListCtrlItems(src_row, dst_row)

    def __SwapListCtrlItems(self, src_row, dst_row):
        src_text = self.selections_list.GetItemText(src_row)
        dst_text = self.selections_list.GetItemText(dst_row)
        self.selections_list.SetItemText(dst_row, src_text)
        self.selections_list.SetItemText(src_row, dst_text)

        src_selected = self.selections_list.IsSelected(src_row)
        dst_selected = self.selections_list.IsSelected(dst_row)
        self.selections_list.Select(src_row, dst_selected)
        self.selections_list.Select(dst_row, src_selected)

        if self._editable_captions:
            src_caption = self.selections_list.GetItemText(src_row, 1)
            dst_caption = self.selections_list.GetItemText(dst_row, 1)
            self.selections_list.SetItem(dst_row, 1, src_caption)
            self.selections_list.SetItem(src_row, 1, dst_caption)

        self.selections_list.EnsureVisible(dst_row)

        self._selected_paths[src_row], self._selected_paths[dst_row] = (
            self._selected_paths[dst_row], self._selected_paths[src_row]
        )
        self._list_indices_by_path[self._selected_paths[src_row]] = src_row
        self._list_indices_by_path[self._selected_paths[dst_row]] = dst_row

    def __GetSelectedLeafPaths(self, tree):
        paths = []
        for selected_item in tree.GetSelections():
            if selected_item.IsOk() and selected_item in self._leaf_paths_by_tree_item:
                paths.append(self._leaf_paths_by_tree_item[selected_item])
        return paths

    def __GetTargetLeafPathsForMenu(self, tree, item):
        selected_leaf_paths = self.__GetSelectedLeafPaths(tree)
        if item in tree.GetSelections() and selected_leaf_paths:
            return selected_leaf_paths
        return [self._leaf_paths_by_tree_item[item]]

    def __PopupTreeContextMenu(self, tree, item):
        menu = wx.Menu()
        if not self._single_selection:
            if item in self._leaf_paths_by_tree_item:
                target_paths = self.__GetTargetLeafPathsForMenu(tree, item)
                in_widget = [path for path in target_paths if path in self._selected_paths]
                not_in_widget = [path for path in target_paths if path not in self._selected_paths]
                if not_in_widget:
                    add_item = menu.Append(-1, 'Add to Widget')
                    self.Bind(
                        wx.EVT_MENU,
                        partial(self.__OnAddLeavesFromBranch, paths=not_in_widget),
                        add_item,
                    )
                if in_widget:
                    remove_item = menu.Append(-1, 'Remove from Widget')
                    self.Bind(
                        wx.EVT_MENU,
                        partial(self.__OnRemoveLeavesFromBranch, paths=in_widget),
                        remove_item,
                    )
            else:
                leaves = self.__CollectLeavesFromItem(tree, item)
                selected_leaves = [path for path in leaves if path in self._selected_paths]
                if len(selected_leaves) < len(leaves):
                    add_leaves = menu.Append(-1, 'Add leaves to widget')
                    self.Bind(
                        wx.EVT_MENU,
                        partial(self.__OnAddLeavesFromBranch, paths=leaves),
                        add_leaves,
                    )
                if selected_leaves:
                    remove_leaves = menu.Append(-1, 'Remove leaves from widget')
                    self.Bind(
                        wx.EVT_MENU,
                        partial(self.__OnRemoveLeavesFromBranch, paths=selected_leaves),
                        remove_leaves,
                    )
            menu.AppendSeparator()
        self.__AppendExpandCollapseSubmenu(menu, tree)
        tree.PopupMenu(menu)
        menu.Destroy()

    def __AppendExpandCollapseSubmenu(self, menu, tree):
        expand_submenu = wx.Menu()
        all_expanded, all_collapsed = self.__GetTreeExpandCollapseState(tree)
        def ExpandAll(evt, **kwargs):
            kwargs['tree'].ExpandAll()
            evt.Skip()
        def CollapseAll(evt, **kwargs):
            kwargs['tree'].CollapseAll()
            evt.Skip()
        if not all_expanded:
            expand_all = expand_submenu.Append(-1, 'Expand All')
            self.Bind(wx.EVT_MENU, partial(ExpandAll, tree=tree), expand_all)
        if not all_collapsed:
            collapse_all = expand_submenu.Append(-1, 'Collapse All')
            self.Bind(wx.EVT_MENU, partial(CollapseAll, tree=tree), collapse_all)
        menu.AppendSubMenu(expand_submenu, 'Expand / Collapse')

    def __OnAddLeavesFromBranch(self, evt, paths):
        self.__SetPathsSelected(paths, True)
        evt.Skip()

    def __OnRemoveLeavesFromBranch(self, evt, paths):
        self.__SetPathsSelected(paths, False)
        evt.Skip()

    def __SetPathsSelected(self, paths, selected):
        changed = False
        for path in paths:
            if selected:
                if path not in self._selected_paths:
                    self._selected_paths.append(path)
                    changed = True
            elif path in self._selected_paths:
                self._selected_paths.remove(path)
                changed = True

        if changed:
            self.__BuildSelectionsList()
            self.__UpdateButtonStates()

    def __UpdateButtonStates(self, *args):
        if self._single_selection:
            return

        list_ctrl_count = self.selections_list.GetItemCount()
        self.ok_btn.Enable(list_ctrl_count > 0)

        selected_rows = self.__GetListCtrlSelectedRows()

        if len(selected_rows) == 1:
            self.remove_row_btn.Enable()
        else:
            self.remove_row_btn.Disable()

        if list_ctrl_count <= 1 or len(selected_rows) > 1 or len(selected_rows) == 0:
            self.move_up_btn.Disable()
            self.move_down_btn.Disable()
        else:
            selected_row = selected_rows[0]
            if selected_row == 0:
                self.move_up_btn.Disable()
                self.move_down_btn.Enable()
            elif selected_row == list_ctrl_count - 1:
                self.move_up_btn.Enable()
                self.move_down_btn.Disable()
            else:
                self.move_up_btn.Enable()
                self.move_down_btn.Enable()

    def __GetListCtrlSelectedRows(self):
        rows = []
        for idx in range(self.selections_list.GetItemCount()):
            if self.selections_list.IsSelected(idx):
                rows.append(idx)

        return rows

    def __BuildTree(self):
        self._tree_items_by_id = {}
        self._leaf_paths_by_tree_item = {}

        self.hier_tree.DeleteAllItems()
        root = self.hier_tree.AddRoot('root')
        self._tree_items_by_id[0] = root

        visible_paths = self.__BuildVisibleElemPaths(self._all_leaf_paths)
        self.__RecurseBuildTree(
            self.hier_tree, self.simhier.GetTree().GetRoot(), visible_paths,
        )

    def __RecurseBuildTree(self, tree_ctrl, node, visible_paths):
        if node is self.simhier.GetTree().GetRoot():
            for child in node.GetChildren():
                self.__RecurseBuildTree(tree_ctrl, child, visible_paths)
            return

        elem_path = node.GetPath()
        if elem_path not in visible_paths:
            return

        if node.GetParent():
            parent_id = node.GetParent().GetID()
        else:
            parent_id = 0

        tree_item = tree_ctrl.AppendItem(self._tree_items_by_id[parent_id], node.GetName())
        node_id = node.GetID()
        self._tree_items_by_id[node_id] = tree_item

        if not node.children:
            self._leaf_paths_by_tree_item[tree_item] = elem_path

        for child in node.GetChildren():
            self.__RecurseBuildTree(tree_ctrl, child, visible_paths)

    def __ApplySelectionToList(self):
        self.__BuildSelectionsList()

    def __BuildSelectionsList(self):
        if self._single_selection:
            return

        self.selections_list.Freeze()
        try:
            self.selections_list.DeleteAllItems()
            self._list_indices_by_path = {}

            for path in self._selected_paths:
                idx = self.selections_list.InsertItem(self.selections_list.GetItemCount(), path)
                if self._editable_captions:
                    self.selections_list.SetItem(idx, 1, self._captions_by_path.get(path, '<default>'))
                self._list_indices_by_path[path] = idx
        finally:
            self.selections_list.Thaw()

    def __OnCaptionCellDoubleClick(self, evt):
        hit = self.selections_list.HitTestSubItem(evt.GetPosition())
        item, _, col = hit[0], hit[1], hit[2]
        if item == wx.NOT_FOUND or col != 1:
            evt.Skip()
            return

        self.__CommitPathEdit()
        self.__CommitCaptionEdit()

        rect = wx.Rect()
        self.selections_list.GetSubItemRect(item, 1, rect)
        path = self.selections_list.GetItemText(item, 0)
        current = self._captions_by_path.get(path, '<default>')
        initial_text = '' if current == '<default>' else current

        self._caption_edit_item = item
        self._caption_edit_ctrl = wx.TextCtrl(
            self.selections_list, value=initial_text, style=wx.TE_PROCESS_ENTER,
            pos=rect.GetTopLeft(), size=rect.GetSize(),
        )
        self._caption_edit_ctrl.Bind(wx.EVT_TEXT_ENTER, self.__OnCaptionEditEnter)
        self._caption_edit_ctrl.Bind(wx.EVT_KILL_FOCUS, self.__OnCaptionEditKillFocus)
        self._caption_edit_ctrl.Bind(wx.EVT_CHAR_HOOK, self.__OnCaptionEditCharHook)
        self._caption_edit_ctrl.SetFocus()
        self._caption_edit_ctrl.SelectAll()

    def __OnCaptionEditEnter(self, evt):
        self.__CommitCaptionEdit()

    def __OnCaptionEditKillFocus(self, evt):
        self.__CommitCaptionEdit()
        evt.Skip()

    def __OnCaptionEditCharHook(self, evt):
        if evt.GetKeyCode() == wx.WXK_ESCAPE:
            self.__CancelCaptionEdit()
        else:
            evt.Skip()

    def __CommitCaptionEdit(self):
        if self._caption_edit_ctrl is None:
            return

        item = self._caption_edit_item
        value = self._caption_edit_ctrl.GetValue().strip() or '<default>'
        path = self.selections_list.GetItemText(item, 0)
        self._captions_by_path[path] = value
        self.selections_list.SetItem(item, 1, value)

        self.__DestroyCaptionEditCtrl()

    def __CancelCaptionEdit(self):
        self.__DestroyCaptionEditCtrl()

    def __DestroyCaptionEditCtrl(self):
        ctrl = self._caption_edit_ctrl
        self._caption_edit_ctrl = None
        self._caption_edit_item = None
        if ctrl is not None:
            wx.CallAfter(ctrl.Destroy)

    def __OnAddNewRow(self, evt):
        self.__CommitCaptionEdit()
        self.__CommitPathEdit()

        idx = self.selections_list.InsertItem(self.selections_list.GetItemCount(), '')
        if self._editable_captions:
            self.selections_list.SetItem(idx, 1, '<default>')
        self.selections_list.EnsureVisible(idx)
        self.__BeginPathCellEdit(idx)

    def __OnRemoveSelectedRow(self, evt):
        self.__CommitCaptionEdit()
        self.__CommitPathEdit()

        selected_rows = self.__GetListCtrlSelectedRows()
        if len(selected_rows) != 1:
            return

        self.__RemoveListRow(selected_rows[0])

    def __BeginPathCellEdit(self, item):
        self.__CommitCaptionEdit()
        self.__CommitPathEdit()

        rect = wx.Rect()
        self.selections_list.GetSubItemRect(item, 0, rect)
        current = self.selections_list.GetItemText(item, 0)

        self._path_edit_item = item
        self._path_edit_ctrl = wx.TextCtrl(
            self.selections_list, value=current, style=wx.TE_PROCESS_ENTER,
            pos=rect.GetTopLeft(), size=rect.GetSize(),
        )
        self._path_edit_ctrl.Bind(wx.EVT_TEXT_ENTER, self.__OnPathEditEnter)
        self._path_edit_ctrl.Bind(wx.EVT_KILL_FOCUS, self.__OnPathEditKillFocus)
        self._path_edit_ctrl.Bind(wx.EVT_CHAR_HOOK, self.__OnPathEditCharHook)
        self._path_edit_ctrl.SetFocus()
        self._path_edit_ctrl.SelectAll()

    def __OnPathEditEnter(self, evt):
        self.__CommitPathEdit()

    def __OnPathEditKillFocus(self, evt):
        self.__CommitPathEdit()
        evt.Skip()

    def __OnPathEditCharHook(self, evt):
        if evt.GetKeyCode() == wx.WXK_ESCAPE:
            self.__CancelPathEdit()
        else:
            evt.Skip()

    def __CommitPathEdit(self):
        if self._path_edit_ctrl is None:
            return

        item = self._path_edit_item
        value = self._path_edit_ctrl.GetValue().strip()

        if not value:
            self.__DestroyPathEditCtrl()
            self.__RemoveListRow(item)
            return

        if not self.__IsValidElemPath(value):
            wx.MessageBox(
                "'{}' is not a valid element path.\nPaths must be dot-delimited with no empty segments, e.g. 'top.foo.bar'.".format(value),
                'Invalid Path', wx.OK | wx.ICON_ERROR,
            )
            return

        for i in range(self.selections_list.GetItemCount()):
            if i != item and self.selections_list.GetItemText(i, 0) == value:
                wx.MessageBox(
                    "'{}' is already in the list.".format(value),
                    'Duplicate Path', wx.OK | wx.ICON_ERROR,
                )
                return

        self.selections_list.SetItemText(item, value)
        self._list_indices_by_path[value] = item
        if value not in self._selected_paths:
            self._selected_paths.append(value)

        self.__DestroyPathEditCtrl()
        self.__UpdateButtonStates()

    def __CancelPathEdit(self):
        item = self._path_edit_item
        self.__DestroyPathEditCtrl()
        if item is not None and self.selections_list.GetItemText(item, 0) == '':
            self.__RemoveListRow(item)

    def __DestroyPathEditCtrl(self):
        ctrl = self._path_edit_ctrl
        self._path_edit_ctrl = None
        self._path_edit_item = None
        if ctrl is not None:
            wx.CallAfter(ctrl.Destroy)

    def __RemoveListRow(self, item):
        if item is None or item < 0 or item >= self.selections_list.GetItemCount():
            return
        path = self.selections_list.GetItemText(item, 0)
        if path and path in self._selected_paths:
            self._selected_paths.remove(path)
        self._list_indices_by_path.pop(path, None)
        self.selections_list.DeleteItem(item)
        self.__UpdateButtonStates()

    @staticmethod
    def __IsValidElemPath(value):
        if not value or value.startswith('.') or value.endswith('.'):
            return False
        return all(len(part) > 0 for part in value.split('.'))

    def __CollectLeavesFromItem(self, tree, item):
        if item in self._leaf_paths_by_tree_item:
            return [self._leaf_paths_by_tree_item[item]]

        paths = []
        child, cookie = tree.GetFirstChild(item)
        while child.IsOk():
            paths.extend(self.__CollectLeavesFromItem(tree, child))
            child = tree.GetNextSibling(child)

        return paths

    def __GetTreeExpandCollapseState(self, tree):
        all_expanded = True
        all_collapsed = True
        has_expandable = False

        child, cookie = tree.GetFirstChild(tree.GetRootItem())
        while child.IsOk():
            branch_expanded, branch_collapsed, branch_has_expandable = (
                self.__BranchExpandCollapseState(tree, child)
            )
            if branch_has_expandable:
                has_expandable = True
                if not branch_expanded:
                    all_expanded = False
                if not branch_collapsed:
                    all_collapsed = False
            child = tree.GetNextSibling(child)

        if not has_expandable:
            return False, False

        return all_expanded, all_collapsed

    def __BranchExpandCollapseState(self, tree, item):
        has_expandable = tree.ItemHasChildren(item)
        all_expanded = True
        all_collapsed = True

        if has_expandable:
            if tree.IsExpanded(item):
                all_collapsed = False
            else:
                all_expanded = False

        child, cookie = tree.GetFirstChild(item)
        while child.IsOk():
            child_expanded, child_collapsed, child_has_expandable = (
                self.__BranchExpandCollapseState(tree, child)
            )
            if child_has_expandable:
                has_expandable = True
                if not child_expanded:
                    all_expanded = False
                if not child_collapsed:
                    all_collapsed = False
            child = tree.GetNextSibling(child)

        return all_expanded, all_collapsed, has_expandable

    @staticmethod
    def __BuildVisibleElemPaths(leaf_elem_paths):
        visible_paths = set()
        for leaf_path in leaf_elem_paths:
            parts = leaf_path.split('.')
            for i in range(1, len(parts) + 1):
                visible_paths.add('.'.join(parts[:i]))
        return visible_paths

class QueueUtilizEditDlg(WidgetDataSelectionsDlg):
    SHOW_FULL_PATHS_LABEL = 'Show full paths'

    def __init__(self, parent, frame, elem_paths, show_full_paths, title="Edit Data Selections"):
        chkboxes = [(self.SHOW_FULL_PATHS_LABEL, show_full_paths)]
        WidgetDataSelectionsDlg.__init__(
            self, parent, frame, elem_paths, queues_only=True,
            settings_chkboxes=chkboxes, title=title,
        )

    def _OnWidgetCheckbox(self, label, checked):
        pass

    @property
    def show_full_paths(self):
        return self.GetSettingCheckbox(self.SHOW_FULL_PATHS_LABEL)

class SummaryViewsEditDlg(WidgetDataSelectionsDlg):
    SHOW_FULL_PATHS_LABEL = 'Show full paths'
    SHOW_DID_LABEL = 'Show DID'

    def __init__(self, parent, frame, elem_paths, show_full_paths, show_did, title="Edit Data Selections"):
        chkboxes = [
            (self.SHOW_FULL_PATHS_LABEL, show_full_paths),
            (self.SHOW_DID_LABEL, show_did),
        ]
        WidgetDataSelectionsDlg.__init__(
            self, parent, frame, elem_paths,
            settings_chkboxes=chkboxes, title=title,
        )

    def _OnWidgetCheckbox(self, label, checked):
        pass

    @property
    def show_full_paths(self):
        return self.GetSettingCheckbox(self.SHOW_FULL_PATHS_LABEL)

    @property
    def show_did(self):
        return self.GetSettingCheckbox(self.SHOW_DID_LABEL)

class SchedulingLinesEditDlg(WidgetDataSelectionsDlg):
    SHOW_DETAILS_LABEL = 'Show detailed queue packets'
    HIDE_EMPTY_ROWS_LABEL = 'Hide empty rows'
    ENABLE_TOOLTIPS_LABEL = 'Enable tooltips'
    SHOW_DID_LABEL = 'Show DID'

    def __init__(
        self, parent, frame, elem_paths, num_samples_before, num_samples_after, show_details, hide_empty_rows, enable_tooltips, show_did, title="Edit Data Selections",
        initial_captions=None,
    ):
        self._num_samples_before = num_samples_before
        self._num_samples_after = num_samples_after
        chkboxes = [
            (self.SHOW_DETAILS_LABEL, show_details),
            (self.HIDE_EMPTY_ROWS_LABEL, hide_empty_rows),
            (self.ENABLE_TOOLTIPS_LABEL, enable_tooltips),
            (self.SHOW_DID_LABEL, show_did),
        ]
        WidgetDataSelectionsDlg.__init__(
            self, parent, frame, elem_paths, queues_only=False, settings_chkboxes=chkboxes,
            editable_captions=True, initial_captions=initial_captions,
        )

    def _BuildSettingsArea(self, sizer):
        assert self._num_samples_before >= 1 and self._num_samples_before <= 25
        info_ticks_before = wx.StaticText(self, label='Num samples before current cycle:')
        self._label_ticks_before = wx.StaticText(self, label=f'({self._num_samples_before})')
        self._slider_ticks_before = wx.Slider(
            self, value=self._num_samples_before, minValue=1, maxValue=25)
        self._slider_ticks_before.Bind(wx.EVT_SLIDER, self.__SyncWithSliderTicks)

        assert self._num_samples_after >= 1 and self._num_samples_after <= 25
        info_ticks_after = wx.StaticText(self, label='Num samples after current cycle:')
        self._label_ticks_after = wx.StaticText(self, label=f'({self._num_samples_after})')
        self._slider_ticks_after = wx.Slider(
            self, value=self._num_samples_after, minValue=1, maxValue=25)
        self._slider_ticks_after.Bind(wx.EVT_SLIDER, self.__SyncWithSliderTicks)

        gb_sizer = wx.GridBagSizer(vgap=10, hgap=12)
        gb_sizer.Add(info_ticks_before, pos=(0, 0))
        gb_sizer.Add(self._slider_ticks_before, pos=(0, 1), flag=wx.EXPAND)
        gb_sizer.Add(self._label_ticks_before, pos=(0, 2))

        gb_sizer.Add(info_ticks_after, pos=(1, 0))
        gb_sizer.Add(self._slider_ticks_after, pos=(1, 1), flag=wx.EXPAND)
        gb_sizer.Add(self._label_ticks_after, pos=(1, 2))

        gb_sizer.AddGrowableCol(1)
        sizer.Add(gb_sizer, 0, wx.EXPAND | wx.LEFT | wx.RIGHT, 5)

        WidgetDataSelectionsDlg._BuildSettingsArea(self, sizer)

        edit_captions_btn = wx.Button(self, label='Edit Captions')
        edit_captions_btn.Bind(wx.EVT_BUTTON, self.__OnEditCaptions)
        sizer.AddSpacer(5)
        sizer.Add(edit_captions_btn, 0, wx.LEFT, 5)

    def __OnEditCaptions(self, evt):
        dlg = CaptionsEditDlg(self, self.GetCustomCaptions())
        if dlg.ShowModal() == wx.ID_OK:
            self._SetCustomCaptions(dlg.GetCustomCaptions())
        dlg.Destroy()

    def _OnWidgetCheckbox(self, label, checked):
        pass

    @property
    def num_samples_before(self):
        return self._slider_ticks_before.GetValue()

    @property
    def num_samples_after(self):
        return self._slider_ticks_after.GetValue()

    @property
    def show_details(self):
        return self.GetSettingCheckbox(self.SHOW_DETAILS_LABEL)

    @property
    def hide_empty_rows(self):
        return self.GetSettingCheckbox(self.HIDE_EMPTY_ROWS_LABEL)

    @property
    def enable_tooltips(self):
        return self.GetSettingCheckbox(self.ENABLE_TOOLTIPS_LABEL)

    @property
    def show_did(self):
        return self.GetSettingCheckbox(self.SHOW_DID_LABEL)

    def __UpdateButtonStates(self, *args):
        WidgetDataSelectionsDlg.__UpdateButtonStates(self, *args)

    def __SyncWithSliderTicks(self, evt):
        value = self._slider_ticks_before.GetValue()
        self._label_ticks_before.SetLabel(f'({value})')

        value = self._slider_ticks_after.GetValue()
        self._label_ticks_after.SetLabel(f'({value})')

        evt.Skip()
