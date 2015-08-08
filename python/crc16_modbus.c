/**
 * Python crc16_modus
 * version 0.1
 *
 * Copyright (C) 2014 by Daniel Lenski <lenski@umd.edu>
 * Time-stamp: <2008-09-19 00:18:51 dlenski>
 *
 * Released under the terms of the
 * GNU General Public License version 3 or later
 */

#include <Python.h>
#include <structmember.h>

#include <stdlib.h>
#include <stdint.h>

static uint
_crc16_modbus(uint8_t *buf, size_t len, uint start)
{
    uint crc = start; // should be 0xFFFF
    for (size_t pos = 0; pos < len; pos++) {
        crc ^= (uint)buf[pos];          // XOR byte into least sig. byte of crc

        for (int i = 8; i != 0; i--) {  // Loop over each bit
            if ((crc & 0x0001) != 0) {  // If the LSB is set
                crc >>= 1;              // Shift right and XOR 0xA001
                crc ^= 0xA001;
            }
            else                        // Else LSB is not set
                crc >>= 1;              // Just shift right
        }
    }
    return crc;
}

static PyObject *
_py_crc16_modbus(PyObject *self, PyObject *arg)
{
    Py_buffer buf = {.buf=NULL};
    if (PyObject_GetBuffer(arg, &buf, PyBUF_SIMPLE)<0)
        return NULL;
    return PyInt_FromLong( _crc16_modbus(buf.buf, buf.len, 0xffff) );
}

//////////////////////////////////////////////////////////////////////

typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    uint digest;
} CRC16m;

// crc16m.__init__(args='')
static PyObject *
CRC16m_init(CRC16m *self, PyObject *args, PyObject *kwlist)
{
    Py_buffer buf = {.buf=NULL};
    if(!PyArg_ParseTuple(args, "|s*:__init__", &buf))
        return NULL;
    self->digest = 0xFFFF;
    if(buf.buf!=NULL)
        self->digest = _crc16_modbus(buf.buf, buf.len, self->digest);
        PyBuffer_Release(&buf);

    return NULL;
}

// crc16m.digest()
static PyObject *
CRC16m_digest(CRC16m *self)
{
    return PyInt_FromLong(self->digest);
}

// crc16m.reset()
static PyObject *
CRC16m_reset(CRC16m *self, PyObject *args)
{
    uint16_t digest = 0xffff;
    if(!PyArg_ParseTuple(args, "|H:reset", &digest))
       return NULL;
    self->digest = (uint)digest;
    Py_RETURN_NONE;
}

// crc16m.hexdigest()
static PyObject *
CRC16m_hexdigest(CRC16m *self)
{
    char str[5];
    sprintf(str, "%04x", self->digest);
    return PyString_FromString(str);
}

// crc16m.copy()
static PyTypeObject CRC16m_pytype; // faux forward-declaration
static PyObject *
CRC16m_copy(CRC16m *self)
{
    CRC16m *copy = PyObject_New(CRC16m, &CRC16m_pytype);
    copy->digest = self->digest;
    return (PyObject *)copy;
}

// crc16m.update(buf)
static PyObject *
CRC16m_update(CRC16m *self, PyObject *arg)
{
    Py_buffer buf = {.buf=NULL};
    if (PyObject_GetBuffer(arg, &buf, PyBUF_SIMPLE)<0)
        return NULL;
    self->digest = _crc16_modbus(buf.buf, buf.len, self->digest);
    PyBuffer_Release(&buf);
    Py_RETURN_NONE;
}

static PyMethodDef CRC16m_methods[] = {
    {"digest", (PyCFunction)CRC16m_digest, METH_NOARGS, ""},
    {"hexdigest", (PyCFunction)CRC16m_hexdigest, METH_NOARGS, ""},
    {"copy", (PyCFunction)CRC16m_copy, METH_NOARGS, ""},
    {"update", (PyCFunction)CRC16m_update, METH_O, ""},
    {"reset", (PyCFunction)CRC16m_reset, METH_VARARGS, ""},
    {NULL},
};

static PyMemberDef CRC16m_members[] = {
    {"__digest", T_UINT, offsetof(CRC16m, digest), 0, NULL},
    {NULL},
};

static PyTypeObject CRC16m_pytype = {
    PyObject_HEAD_INIT(NULL)
    0,                           /*ob_size*/
    "crc16_modbus.crc16_modbus", /*tp_name*/
    sizeof(CRC16m),              /*tp_basicsize*/
    0,                           /*tp_itemsize*/
    0,                           /*tp_dealloc*/
    0,                           /*tp_print*/
    0,                           /*tp_getattr*/
    0,                           /*tp_setattr*/
    0,                           /*tp_compare*/
    0,                           /*tp_repr*/
    0,                           /*tp_as_number*/
    0,                           /*tp_as_sequence*/
    0,                           /*tp_as_mapping*/
    0,                           /*tp_hash */
    0,                           /*tp_call*/
    0,                           /*tp_str*/
    0,                           /*tp_getattro*/
    0,                           /*tp_setattro*/
    0,                           /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,          /*tp_flags*/
    "hashlib-compatible object to compute crc16_modbus", /* tp_doc */
    .tp_methods = CRC16m_methods,
    .tp_members = CRC16m_members,
    .tp_init = (initproc)CRC16m_init,
    .tp_new = PyType_GenericNew,
};

//////////////////////////////////////////////////////////////////////

static PyMethodDef module_methods[] = {
    {"_crc16_modbus", _py_crc16_modbus, METH_O, "function to compute crc16_modbus for a buffer" },
    {NULL},
};

PyDoc_STRVAR(module__doc__,
             "hashlib-compatible CRC16_modbus.\n"
             "algorithm: http://stackoverflow.com/q/19347685/20789\n"
             "hashlib def'n: http://stackoverflow.com/a/5061842/20789");

#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif

PyMODINIT_FUNC
initcrc16_modbus(void)
{
    if (PyType_Ready(&CRC16m_pytype) < 0)
        return;
    PyDict_SetItemString(CRC16m_pytype.tp_dict, "name", PyString_FromString("crc16_modbus"));
    PyDict_SetItemString(CRC16m_pytype.tp_dict, "digest_size", PyInt_FromLong(4));
    PyDict_SetItemString(CRC16m_pytype.tp_dict, "block_size", PyInt_FromLong(1));

    PyObject *mod = Py_InitModule3("crc16_modbus", module_methods, module__doc__);
    if (!mod)
        return;

    Py_INCREF(&CRC16m_pytype);
    PyModule_AddObject(mod, "crc16_modbus", (PyObject *)&CRC16m_pytype);
}
