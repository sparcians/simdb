import wx
import wx.lib.scrolledpanel as scrolled

class CollectionLogs(scrolled.ScrolledPanel):
    NOTIF_TYPE_LABELS = {0: 'WARNING', 1: 'ERROR', 2: 'MESSAGE'}

    def __init__(self, parent, frame):
        super().__init__(parent)
        self.frame = frame
        self.simhier = frame.simhier

        cursor = frame.db.cursor()
        cursor.execute("SELECT Timestamp, NotifStr, NotifType "
                       "FROM Notifications ORDER BY Timestamp ASC")
        rows = cursor.fetchall()

        cursor.execute("SELECT CID, FullPath FROM CollectableTreeNodes")
        paths_by_cid = {cid: fp.replace('root.', '') for cid, fp in cursor.fetchall()}

        mono10 = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)

        sizer = wx.BoxSizer(wx.VERTICAL)

        grid = wx.GridBagSizer(vgap=2, hgap=10)

        row = 0
        for timestamp, notif_str, notif_type in rows:
            tick = str(timestamp) if timestamp is not None else '0'
            type_label = self.NOTIF_TYPE_LABELS.get(notif_type, str(notif_type))

            tick_text = wx.StaticText(self, label='Tick:{}'.format(int(tick)))
            tick_text.SetFont(mono10)
            grid.Add(tick_text, pos=(row, 0))

            type_text = wx.StaticText(self, label='Type:{}'.format(type_label))
            type_text.SetFont(mono10)
            grid.Add(type_text, pos=(row, 1))

            body = wx.StaticText(self, label=notif_str)
            body.SetFont(mono10)
            grid.Add(body, pos=(row + 1, 0), span=(1, 3),
                     flag=wx.EXPAND | wx.BOTTOM, border=6)

            row += 2

        if row > 0:
            grid.AddGrowableCol(2, 1)

        sizer.Add(grid, 1, wx.EXPAND | wx.ALL, 5)
        self.SetSizer(sizer)
        self.SetupScrolling()
        self.Layout()

    def UpdateWidgets(self):
        pass
