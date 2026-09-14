import wx, sys, argparse
from viewer.model.workspace import Workspace

class MyApp(wx.App):
    def OnInit(self):
        return True

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", required=True, help="Path to the database file")
    parser.add_argument("--layout-file", help="Path to the layout file (*.alf) to load")
    parser.add_argument("--read-only", action="store_true", default=False, help="Open in read-only mode")
    args = parser.parse_args()

    app = MyApp()
    workspace = Workspace(args.database, args.layout_file, read_only=args.read_only)
    app.MainLoop()
