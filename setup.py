from setuptools import Extension, setup

setup(
    ext_modules=[
        Extension("npu_render._pixels", ["src/npu_render/_pixels.c"], optional=True),
    ],
)
