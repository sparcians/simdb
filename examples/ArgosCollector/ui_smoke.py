#!/usr/bin/env python3
import argparse
import os
import sys


def _bootstrap_viewer_import_path():
    # ui_smoke.py lives in examples/ArgosCollector; viewer package is under python/argos.
    script_dir = os.path.dirname(os.path.realpath(__file__))
    repo_root = os.path.dirname(os.path.dirname(script_dir))
    argos_python_dir = os.path.join(repo_root, "python", "argos")
    if os.path.isdir(argos_python_dir) and argos_python_dir not in sys.path:
        sys.path.insert(0, argos_python_dir)


def _has_display():
    return bool(os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--db-file", required=True, help="Path to database file")
    parser.add_argument("--timeout-ms", type=int, default=800, help="Milliseconds to keep UI open")
    args = parser.parse_args()

    if not _has_display():
        print("ui_smoke: skip (no DISPLAY/WAYLAND_DISPLAY set)")
        return 0

    _bootstrap_viewer_import_path()

    try:
        import wx
    except ModuleNotFoundError as ex:
        if ex.name == "wx":
            print(f"ui_smoke: skip ({ex})")
            return 0
        raise

    from viewer.model.workspace import Workspace

    app = wx.App(False)
    workspace = Workspace(args.db_file, None, False)

    # Open frame and verify basic startup path, then close automatically.
    def _shutdown():
        try:
            workspace._frame.Destroy()
        finally:
            app.ExitMainLoop()

    wx.CallLater(max(100, int(args.timeout_ms)), _shutdown)
    app.MainLoop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
