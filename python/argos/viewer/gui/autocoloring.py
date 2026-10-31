# @package Argos auto-coloring color map

from __future__ import annotations
import wx
import colorsys  # For easy HSL to RGB conversion
import numpy as np
from typing import Dict, Set, Tuple

try:
    from daltonize import daltonize
    HAS_DALTONIZE = True
except ModuleNotFoundError:
    HAS_DALTONIZE = False


class BrushRepository:
    _COLORBLIND_MODES = set(('d', 'p', 't'))
    _PALETTES = set(('default',)) | _COLORBLIND_MODES
    _SHUFFLE_MODES = set(('default', 'shuffled'))

    def __init__(self) -> None:
        self.palette = 'default'
        self.shuffle_mode = 'default'
        self.brushes: Dict[str, Dict[str, Dict[int, wx.Brush]]] = {}
        for shuffle_mode in self._SHUFFLE_MODES:
            self.brushes[shuffle_mode] = {}

    @classmethod
    def get_supported_palettes(cls) -> Set[str]:
        return cls._PALETTES

    @classmethod
    def get_supported_shuffle_modes(cls) -> Set[str]:
        return cls._SHUFFLE_MODES

    def __getitem__(self, idx: int) -> wx.Brush:
        return self.as_dict()[idx]

    def as_dict(self) -> Dict[int, wx.Brush]:
        return self.brushes[self.shuffle_mode][self.palette]

    def __len__(self) -> int:
        return len(self.as_dict())

    def validate_shuffle_mode(self, shuffle_mode: str) -> None:
        if shuffle_mode not in self._SHUFFLE_MODES:
            raise ValueError(
                'Shuffle mode shoud be one of the following values: '
                f'{self._SHUFFLE_MODES}'
            )

    def validate_palette(self, palette: str) -> None:
        if not HAS_DALTONIZE and palette in self._COLORBLIND_MODES:
            raise NotImplementedError(
                'Daltonize is not installed.'
                '`pip install daltonize` to enable colorblindness modes.'
            )
        if palette not in self._PALETTES:
            raise ValueError(
                'Palette mode should be one of the following values: '
                f'{self._PALETTES}'
            )

    def generate_shuffled_palette(
        self,
        shuffle_mode: str,
        color_dict: Dict[int, Tuple[float, float, float]]
    ) -> Dict[int, Tuple[float, float, float]]:
        self.validate_shuffle_mode(shuffle_mode)
        if shuffle_mode == 'default':
            return color_dict
        elif shuffle_mode == 'shuffled':
            # Shuffle the colors around
            new_dict = {
                (3 * key) % len(color_dict): value
                for key, value in color_dict.items()
            }
            # Make sure the shuffle didn't leave out any of the original keys
            assert set(new_dict.keys()) == \
                   set(range(min(color_dict.keys()),
                             max(color_dict.keys()) + 1))
            return new_dict
        else:
            raise NotImplementedError(
                f'Shuffle mode {shuffle_mode} has not been implemented.'
            )

    def create_brush_dict(
        self,
        color_dict: Dict[int, Tuple[float, float, float]]
    ) -> Dict[int, wx.Brush]:
        return {
            key: wx.Brush([int(255 * x) for x in value])
            for key, value in color_dict.items()
        }

    def generate_all_brushes(
        self,
        color_dict: Dict[int, Tuple[float, float, float]]
    ) -> None:
        shuffle_idx = 0
        shuffle_idx_to_mode = {}
        shuffled_color_dicts = {}

        for shuffle_mode in self._SHUFFLE_MODES:
            # Keep track of which y-index maps to which shuffle mode
            shuffle_idx_to_mode[shuffle_idx] = shuffle_mode

            shuffled_color_dicts[shuffle_mode] = \
                self.generate_shuffled_palette(shuffle_mode, color_dict)

            # The default palettes don't get daltonized, so we can go ahead and
            # create brushes for them here
            self.brushes[shuffle_mode]['default'] = \
                self.create_brush_dict(shuffled_color_dicts[shuffle_mode])

            shuffle_idx += 1

        if HAS_DALTONIZE:
            for palette in self._COLORBLIND_MODES:
                # Create an image containing 1 RGB pixel for each color in each
                # shuffled dict
                colors = np.zeros(
                    (len(self._SHUFFLE_MODES), len(color_dict), 3)
                )
                for shf_idx, shf_mode in enumerate(self._SHUFFLE_MODES):
                    for p_idx, color in shuffled_color_dicts[shf_mode].items():
                        colors[shf_idx, p_idx] = color

                # Convert the colors to the requested colorblindness mode
                daltonized_colors = daltonize.daltonize(colors, palette)

                # Go back through the image and get the new pixel colors
                new_dict: Dict[str, Dict[int, Tuple[float, float, float]]] = {}

                for shuffle_idx in range(len(self._SHUFFLE_MODES)):
                    for color_idx in range(len(color_dict)):
                        shuffle_mode = shuffle_idx_to_mode[shuffle_idx]
                        if shuffle_mode not in new_dict:
                            new_dict[shuffle_mode] = {}
                        new_dict[shuffle_mode][color_idx] = \
                            daltonized_colors[shuffle_idx, color_idx]

                for shuffle_mode in self._SHUFFLE_MODES:
                    self.brushes[shuffle_mode][palette] = \
                        self.create_brush_dict(new_dict[shuffle_mode])

    def set_shuffle_mode(self, shuffle_mode: str) -> None:
        self.validate_shuffle_mode(shuffle_mode)
        self.shuffle_mode = shuffle_mode

    def set_palette(self, palette: str) -> None:
        self.validate_palette(palette)
        self.palette = palette


BACKGROUND_BRUSHES = None

def SetPalettes(palette: str) -> None:
    BACKGROUND_BRUSHES.set_palette(palette)

def SetShuffleModes(mode: str) -> None:
    BACKGROUND_BRUSHES.set_shuffle_mode(mode)

def BuildBackgroundColors(count: int):
    assert (count > 0)
    colors = {}
    for i in range(count):
        colors[i] = colorsys.hls_to_rgb(i / float(count), 0.75, 1)

    return colors

# call after wx.App init
def BuildBrushes(colorblindness_mode: str, shuffle_mode: str, count: int = 52) -> None:
    global BACKGROUND_BRUSHES
    BACKGROUND_BRUSHES = BrushRepository()
    BACKGROUND_COLORS = BuildBackgroundColors(count)
    BACKGROUND_BRUSHES.generate_all_brushes(BACKGROUND_COLORS)
    SetPalettes(colorblindness_mode)
    SetShuffleModes(shuffle_mode)
