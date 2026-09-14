from viewer.gui.widgets.scheduling_lines import CaptionManager


class DummySimHier:
    def __init__(self):
        self._container_paths = ["top.queue"]

    def GetContainerElemPaths(self):
        return list(self._container_paths)

    def GetCapacityByElemPath(self, elem_path):
        if elem_path == "top.queue":
            return 4
        return None


def test_caption_manager_scalar_has_no_bin_suffix():
    simhier = DummySimHier()
    manager = CaptionManager(simhier)
    manager.SetElemPathRegexReplacement("top.scalar", "top.scalar")

    caption = manager.GetCaption("top.scalar", 0, ["top.scalar"], False)
    assert caption == "scalar"

    prefix = manager.GetCaptionPrefix("top.scalar", ["top.scalar"], False)
    assert prefix == "scalar"


def test_caption_manager_custom_captions():
    simhier = DummySimHier()
    manager = CaptionManager(simhier)
    manager.SetElemPathRegexReplacement("top.queue", "top.queue")

    # Initially empty
    assert manager.GetCustomCaptions() == {}
    assert manager.GetCustomCaption("top.queue[0]") is None

    # Set custom captions (bin-level and scalar)
    manager.SetCustomCaption("top.queue[0]", "CustomQueue0")
    manager.SetCustomCaption("top.scalar", "CustomScalar")

    assert manager.GetCustomCaption("top.queue[0]") == "CustomQueue0"
    assert manager.GetCustomCaption("top.scalar") == "CustomScalar"
    assert manager.GetCustomCaptions() == {
        "top.queue[0]": "CustomQueue0",
        "top.scalar": "CustomScalar",
    }

    # Set container group caption
    manager.SetCustomCaption("top.queue", "Foo Queue")
    assert manager.GetCaptionPrefix("top.queue") == "Foo Queue"
    assert manager.GetCaption("top.queue", 2, ["top.queue"], False) == "Foo Queue[2]"
    assert manager.GetCaption("top.queue", 0, ["top.queue"], False) == "Foo Queue[0]"

    # Remove custom caption
    manager.RemoveCustomCaption("top.scalar")
    assert manager.GetCustomCaption("top.scalar") is None

    # Set custom captions from dict
    manager.SetCustomCaptions({"top.queue[1]": "CustomQueue1"})
    assert manager.GetCustomCaption("top.queue[0]") is None
    assert manager.GetCustomCaption("top.queue[1]") == "CustomQueue1"

    # Reset container captions to default
    manager.SetCustomCaption("top.queue", "CustomGroup")
    manager.SetCustomCaption("top.queue[0]", "Bin0")
    manager.SetCustomCaption("top.queue[1]", "Bin1")
    manager.RemoveContainerCustomCaptions("top.queue")
    assert manager.GetCustomCaption("top.queue") is None
    assert manager.GetCustomCaption("top.queue[0]") is None
    assert manager.GetCustomCaption("top.queue[1]") is None
    assert manager.GetCaption("top.queue", 0, ["top.queue"], False) == "queue[0]"

    # Clear custom captions
    manager.ClearCustomCaptions()
    assert manager.GetCustomCaptions() == {}


