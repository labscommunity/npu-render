#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>

/* Fuse FP16 lookup, planar-to-packed conversion, and output allocation.
 * This is CPU presentation plumbing, not NPU execution. No NumPy C ABI.
 */
static PyObject *
rgba16_to_rgb8(PyObject *self, PyObject *args)
{
    PyObject *pixels, *table, *output = NULL;
    Py_buffer src = {0}, lut = {0};
    Py_ssize_t width, height, count;
    (void)self;
    if (!PyArg_ParseTuple(args, "OOnn", &pixels, &table, &width, &height))
        return NULL;
    if (width < 1 || height < 1 || width > PY_SSIZE_T_MAX / height / 8) {
        PyErr_SetString(PyExc_ValueError, "invalid image extent");
        return NULL;
    }
    count = width * height;
    if (PyObject_GetBuffer(pixels, &src, PyBUF_C_CONTIGUOUS | PyBUF_FORMAT) < 0)
        return NULL;
    if (PyObject_GetBuffer(table, &lut, PyBUF_C_CONTIGUOUS | PyBUF_FORMAT) < 0)
        goto done;
    if (src.itemsize != 2 || src.len != count * 8 ||
        !src.format || strcmp(src.format, "e") != 0 ||
        lut.itemsize != 1 || lut.len != 65536 ||
        !lut.format || strcmp(lut.format, "B") != 0) {
        PyErr_SetString(PyExc_ValueError, "expected native FP16 RGBA planes and a uint8 table");
        goto done;
    }
    output = PyBytes_FromStringAndSize(NULL, count * 3);
    if (output) {
        const unsigned char *data = src.buf;
        const uint8_t *lookup = lut.buf;
        uint8_t *dst = (uint8_t *)PyBytes_AS_STRING(output);
        Py_BEGIN_ALLOW_THREADS
        for (Py_ssize_t i = 0; i < count; ++i) {
            for (Py_ssize_t channel = 0; channel < 3; ++channel) {
                uint16_t bits;
                /* memcpy also handles an unaligned Python buffer safely. */
                memcpy(&bits, data + 2 * (channel * count + i), sizeof(bits));
                dst[3 * i + channel] = lookup[bits];
            }
        }
        Py_END_ALLOW_THREADS
    }
done:
    if (lut.obj)
        PyBuffer_Release(&lut);
    PyBuffer_Release(&src);
    return output;
}

static PyMethodDef methods[] = {
    {"rgba16_to_rgb8", rgba16_to_rgb8, METH_VARARGS,
     "Convert contiguous native FP16 RGBA planes through a uint8 lookup table."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_pixels", NULL, -1, methods
};

PyMODINIT_FUNC PyInit__pixels(void) { return PyModule_Create(&module); }
