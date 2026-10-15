"""Software-window presentation, separate from graphics operation execution."""

import os

import numpy as np


class WindowPresenter:
    def __init__(self, width: int, height: int):
        os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
        try:
            import pygame
        except ImportError as error:
            raise RuntimeError("install the live extra: pip install 'npu-render[live]'") from error
        self.pygame = pygame
        pygame.display.init()
        self.surface = pygame.display.set_mode((width, height))
        self.metadata = {
            "kind": "sdl_software_window",
            "driver": pygame.display.get_driver(),
            "sdl_version": pygame.get_sdl_version(),
            "pygame_version": pygame.version.ver,
            "scanout_measured": False,
            "desktop_compositor_offloaded": False,
        }
        if self.metadata["driver"] in {"dummy", "offscreen"}:
            self.metadata["kind"] = "offscreen_test_window"

    def events(self) -> list[str]:
        pygame = self.pygame
        actions = []
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                actions.append("quit")
            elif event.type == pygame.KEYDOWN:
                action = {
                    pygame.K_ESCAPE: "quit",
                    pygame.K_1: "GPU",
                    pygame.K_2: "NPU",
                    pygame.K_3: "CPU",
                    pygame.K_r: "reload",
                }.get(event.key)
                if action:
                    actions.append(action)
        return actions

    def submit(self, rgb: np.ndarray) -> None:
        self.pygame.surfarray.blit_array(self.surface, rgb.transpose(1, 0, 2))
        self.pygame.display.flip()

    def caption(self, text: str) -> None:
        self.pygame.display.set_caption(text)

    def close(self) -> None:
        self.pygame.display.quit()


class NullPresenter:
    """Explicit processing-only measurement; never claims display delivery."""

    metadata = {"kind": "none", "scanout_measured": False, "desktop_compositor_offloaded": False}

    def events(self) -> list[str]:
        return []

    def submit(self, rgb: np.ndarray) -> None:
        pass

    def caption(self, text: str) -> None:
        pass

    def close(self) -> None:
        pass
