import wx, sys, argparse
from viewer.model.workspace import Workspace

class MyApp(wx.App):
    def OnInit(self):
        return True

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", required=True, help="Path to the database file")
    parser.add_argument("--layout-file", help="Path to the layout file (*.alf) to load")
    args = parser.parse_args()

    app = MyApp()
    workspace = Workspace(args.database, args.layout_file)
    app.MainLoop()
