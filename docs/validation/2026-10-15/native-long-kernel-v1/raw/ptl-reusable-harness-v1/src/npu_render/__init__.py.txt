"""Graphics operations with explicit, replaceable execution backends."""

from .contracts import Composition, RenderedFrame
from .renderer import Renderer

__all__ = ["Composition", "RenderedFrame", "Renderer"]
