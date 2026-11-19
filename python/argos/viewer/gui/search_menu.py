import wx
from collections import OrderedDict


def ShowSearchMenu(window, frame, elem_path, nvps, clicked_field=None):
    """Pops up a context menu with a 'Search' submenu that has one entry per name/value
    pair of a struct. Picking an entry searches for that single field/value in elem_path.
    If clicked_field is given, its entry is also shown first, followed by a separator."""
    nvps = [(str(name), str(value)) for name, value in nvps]
    if not nvps:
        return

    menu = wx.Menu()
    search_menu = wx.Menu()

    def AddSearchItem(name, value):
        # '&' would otherwise be taken as a menu mnemonic.
        label = f'{name}:{value}'.replace('&', '&&')
        item = search_menu.Append(wx.ID_ANY, label)

        def RunSearch(event):
            frame.RunSearch(OrderedDict([(name, value)]), elem_path)

        search_menu.Bind(wx.EVT_MENU, RunSearch, item)

    if clicked_field is not None:
        clicked = [(name, value) for name, value in nvps if name == clicked_field]
        if clicked:
            AddSearchItem(*clicked[0])
            search_menu.AppendSeparator()

    for name, value in nvps:
        AddSearchItem(name, value)

    menu.AppendSubMenu(search_menu, 'Search')
    try:
        window.PopupMenu(menu)
    finally:
        menu.Destroy()


def ShowDisabledSearchMenu(window, reason):
    """Pops up a context menu whose 'Search' entry is disabled. The reason it is
    unavailable is set as the item's help text and shown as a tooltip over the
    window while the menu is up (native popup menus have no tooltips of their own)."""
    menu = wx.Menu()
    item = menu.Append(wx.ID_ANY, 'Search')
    item.SetHelp(reason)
    item.Enable(False)

    def OnHighlight(event):
        if event.GetId() == item.GetId():
            window.SetToolTip(reason)
        else:
            window.UnsetToolTip()
        event.Skip()

    menu.Bind(wx.EVT_MENU_HIGHLIGHT, OnHighlight)
    window.SetToolTip(reason)
    try:
        window.PopupMenu(menu)
    finally:
        window.UnsetToolTip()
        menu.Destroy()
