#include <Python.h>

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION

#include <numpy/arrayobject.h>
#include <stdio.h>

#include "healpix_geom.h"
#include "hpgeom_stack.h"
#include "hpgeom_utils.h"

static PyObject *angle_to_pixel(PyObject *dummy, PyObject *args,
                                PyObject *kwargs) {
  int64_t nside;
  PyObject *a_obj = NULL, *b_obj = NULL;
  PyObject *a_arr = NULL, *b_arr = NULL;
  PyObject *pix_obj = NULL;
  int lonlat = 1;
  int nest = 1;
  int degrees = 1;
  static char *kwlist[] = {"nside", "a",       "b", "lonlat",
                           "nest",  "degrees", NULL};

  int64_t *pixels = NULL;
  int i;
  double *a_data, *b_data;
  double theta, phi;
  healpix_info hpx;
  char err[ERR_SIZE];

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "LOO|ppp", kwlist, &nside,
                                   &a_obj, &b_obj, &lonlat, &nest, &degrees))
    return NULL;

  a_arr = PyArray_FROM_OTF(a_obj, NPY_DOUBLE,
                           NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
  if (a_arr == NULL)
    return NULL;
  b_arr = PyArray_FROM_OTF(b_obj, NPY_DOUBLE,
                           NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
  if (b_arr == NULL)
    goto fail;

  int ndim_a = PyArray_NDIM((PyArrayObject *)a_arr);
  int ndim_b = PyArray_NDIM((PyArrayObject *)b_arr);

  if (ndim_a != ndim_b) {
    PyErr_SetString(PyExc_ValueError,
                    "a and b arrays have mismatched dimensions.");
    goto fail;
  }
  bool is_scalar = (ndim_a == 0);

  npy_intp a_size = PyArray_SIZE((PyArrayObject *)a_arr);
  npy_intp b_size = PyArray_SIZE((PyArrayObject *)b_arr);

  if (a_size != b_size) {
    PyErr_SetString(PyExc_ValueError, "a and b arrays have mismatched sizes.");
    goto fail;
  }

  if ((pixels = (int64_t *)calloc(a_size, sizeof(int64_t))) == NULL) {
    PyErr_SetString(PyExc_RuntimeError,
                    "Could not allocate memory for pixels.");
    goto fail;
  }

  a_data = (double *)PyArray_DATA((PyArrayObject *)a_arr);
  b_data = (double *)PyArray_DATA((PyArrayObject *)b_arr);

  enum Scheme scheme;
  if (nest) {
    scheme = NEST;
  } else {
    scheme = RING;
  }
  if (!hpgeom_check_nside(nside, scheme, err)) {
    PyErr_SetString(PyExc_ValueError, err);
    goto fail;
  }
  hpx = healpix_info_from_nside(nside, scheme);

  for (i = 0; i < a_size; i++) {
    if (lonlat) {
      if (!hpgeom_lonlat_to_thetaphi(a_data[i], b_data[i], &theta, &phi,
                                     (bool)degrees, err)) {
        PyErr_SetString(PyExc_ValueError, err);
        goto fail;
      }
    } else {
      if (!hpgeom_check_theta_phi(a_data[i], b_data[i], err)) {
        PyErr_SetString(PyExc_ValueError, err);
        goto fail;
      }
      theta = a_data[i];
      phi = b_data[i];
    }
    pixels[i] = ang2pix(hpx, theta, phi);
  }

  if (is_scalar) {
    pix_obj = PyLong_FromLongLong(pixels[0]);
  } else {
    npy_intp *dims = PyArray_DIMS((PyArrayObject *)a_arr);
    pix_obj = PyArray_SimpleNewFromData(1, dims, NPY_INT64, pixels);
  }

  /* do I free the memory from pixels? */

  Py_DECREF(a_arr);
  Py_DECREF(b_arr);

  return pix_obj;

fail:
  // free memory from pixels if it's set
  if (pixels != NULL) {
    free(pixels);
  }
  Py_XDECREF(a_arr);
  Py_XDECREF(b_arr);

  return NULL;
}

static PyObject *pixel_to_angle(PyObject *dummy, PyObject *args,
                                PyObject *kwargs) {
  int64_t nside;
  PyObject *pix_obj = NULL;
  PyObject *pix_arr = NULL;
  PyObject *a_obj = NULL, *b_obj = NULL;
  int lonlat = 1;
  int nest = 1;
  int degrees = 1;
  static char *kwlist[] = {"nside", "pix", "lonlat", "nest", "degrees", NULL};

  int64_t *pix_data;
  int i;
  double *as = NULL, *bs = NULL;
  double theta, phi;
  healpix_info hpx;
  char err[ERR_SIZE];

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "LO|ppp", kwlist, &nside,
                                   &pix_obj, &lonlat, &nest, &degrees))
    return NULL;

  pix_arr = PyArray_FROM_OTF(pix_obj, NPY_INT64,
                             NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
  if (pix_arr == NULL)
    return NULL;

  int ndim_pix = PyArray_NDIM((PyArrayObject *)pix_arr);

  bool is_scalar = (ndim_pix == 0);

  npy_intp pix_size = PyArray_SIZE((PyArrayObject *)pix_arr);

  if ((as = (double *)calloc(pix_size, sizeof(double))) == NULL) {
    PyErr_SetString(PyExc_RuntimeError,
                    "Could not allocate memory for a array.");
    goto fail;
  }
  if ((bs = (double *)calloc(pix_size, sizeof(double))) == NULL) {
    PyErr_SetString(PyExc_RuntimeError,
                    "Could not allocate memory for b array.");
    goto fail;
  }

  pix_data = (int64_t *)PyArray_DATA((PyArrayObject *)pix_arr);

  enum Scheme scheme;
  if (nest) {
    scheme = NEST;
  } else {
    scheme = RING;
  }
  if (!hpgeom_check_nside(nside, scheme, err)) {
    PyErr_SetString(PyExc_ValueError, err);
    goto fail;
  }
  hpx = healpix_info_from_nside(nside, scheme);

  for (i = 0; i < pix_size; i++) {
    if (!hpgeom_check_pixel(hpx, pix_data[i], err)) {
      PyErr_SetString(PyExc_ValueError, err);
      goto fail;
    }
    pix2ang(hpx, pix_data[i], &theta, &phi);
    if (lonlat) {
      if (!hpgeom_thetaphi_to_lonlat(theta, phi, &as[i], &bs[i], (bool)degrees,
                                     err)) {
        PyErr_SetString(PyExc_ValueError, err);
        goto fail;
      }
    } else {
      as[i] = theta;
      bs[i] = phi;
    }
  }

  if (is_scalar) {
    a_obj = PyFloat_FromDouble(as[0]);
    b_obj = PyFloat_FromDouble(bs[0]);
  } else {
    npy_intp *dims = PyArray_DIMS((PyArrayObject *)pix_arr);
    a_obj = PyArray_SimpleNewFromData(1, dims, NPY_FLOAT64, as);
    b_obj = PyArray_SimpleNewFromData(1, dims, NPY_FLOAT64, bs);
  }

  /* do I free the memory from as, bs? Or is that grabbed by the object?*/
  Py_DECREF(pix_arr);

  PyObject *retval = PyTuple_New(2);
  PyTuple_SET_ITEM(retval, 0, a_obj);
  PyTuple_SET_ITEM(retval, 1, b_obj);

  return retval;

fail:
  if (as != NULL) {
    free(as);
  }
  if (bs != NULL) {
    free(bs);
  }
  Py_XDECREF(pix_arr);

  return NULL;
}

static PyObject *query_circle(PyObject *dummy, PyObject *args,
                              PyObject *kwargs) {
  int64_t nside;
  double a, b, radius;
  int inclusive = 0;
  long fact = 4;
  int nest = 1;
  int lonlat = 1;
  int degrees = 1;
  static char *kwlist[] = {"nside", "a",    "b",      "radius",  "inclusive",
                           "fact",  "nest", "lonlat", "degrees", NULL};

  char err[ERR_SIZE];
  int status = 1;
  struct i64rangeset *pixset = NULL;

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Lddd|plppp", kwlist, &nside,
                                   &a, &b, &radius, &inclusive, &fact, &nest,
                                   &lonlat, &degrees))
    return NULL;

  double theta, phi;
  if (lonlat) {
    if (!hpgeom_lonlat_to_thetaphi(a, b, &theta, &phi, (bool)degrees, err)) {
      PyErr_SetString(PyExc_ValueError, err);
      goto fail;
    }
    if (degrees) {
      radius *= D2R;
    }
  } else {
    if (!hpgeom_check_theta_phi(a, b, err)) {
      PyErr_SetString(PyExc_ValueError, err);
      goto fail;
    }
    theta = a;
    phi = b;
  }

  if (!hpgeom_check_radius(radius, err)) {
    PyErr_SetString(PyExc_ValueError, err);
    goto fail;
  }

  enum Scheme scheme;
  if (nest) {
    scheme = NEST;
  } else {
    scheme = RING;
  }
  if (!hpgeom_check_nside(nside, scheme, err)) {
    PyErr_SetString(PyExc_ValueError, err);
    goto fail;
  }
  healpix_info hpx = healpix_info_from_nside(nside, scheme);

  pixset = i64rangeset_new(&status, err);
  if (!status) {
    PyErr_SetString(PyExc_RuntimeError, err);
    goto fail;
  }

  if (!inclusive) {
    fact = 0;
  } else {
    if (!hpgeom_check_fact(hpx, fact, err)) {
      PyErr_SetString(PyExc_ValueError, err);
      goto fail;
    }
  }
  query_disc(hpx, theta, phi, radius, fact, pixset, &status, err);

  if (!status) {
    PyErr_SetString(PyExc_RuntimeError, err);
    goto fail;
  }

  size_t npix = i64rangeset_npix(pixset);
  npy_intp dims[1];
  dims[0] = (npy_intp)npix;

  PyObject *pix_arr = PyArray_SimpleNew(1, dims, NPY_INT64);
  int64_t *pix_data = (int64_t *)PyArray_DATA((PyArrayObject *)pix_arr);

  i64rangeset_fill_buffer(pixset, npix, pix_data);

  i64rangeset_delete(pixset);

  return pix_arr;

fail:
  if (pixset != NULL) {
    pixset = i64rangeset_delete(pixset);
  }
  return NULL;
}

static PyObject *nest_to_ring(PyObject *dummy, PyObject *args,
                              PyObject *kwargs) {
    int64_t nside;
    PyObject *nest_pix_obj;
    PyObject *nest_pix_arr;
    static char *kwlist[] = {"nside", "pix", NULL};

    int64_t *nest_pix_data = NULL, *ring_pix_data = NULL;
    healpix_info hpx;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "LO", kwlist, &nside, &nest_pix_obj))
        return NULL;

    nest_pix_arr = PyArray_FROM_OTF(nest_pix_obj, NPY_INT64,
                                    NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nest_pix_arr == NULL)
        return NULL;

    int ndim_pix = PyArray_NDIM((PyArrayObject *)nest_pix_arr);

    bool is_scalar = (ndim_pix == 0);

    npy_intp pix_size = PyArray_SIZE((PyArrayObject *)nest_pix_arr);

    if ((ring_pix_data = (int64_t *)calloc(pix_size, sizeof(int64_t))) == NULL) {
        PyErr_SetString(PyExc_RuntimeError,
                    "Could not allocate memory for a array.");
        goto fail;
    }

    nest_pix_data = (int64_t *)PyArray_DATA((PyArrayObject *)nest_pix_arr);

    if (!hpgeom_check_nside(nside, NEST, err)) {
        PyErr_SetString(PyExc_ValueError, err);
        goto fail;
    }
    hpx = healpix_info_from_nside(nside, NEST);

    for (npy_intp i = 0; i < pix_size; i++) {
        if (!hpgeom_check_pixel(hpx, nest_pix_data[i], err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        ring_pix_data[i] = nest2ring(hpx, nest_pix_data[i]);
    }

    PyObject *ring_pix_obj;
    if (is_scalar) {
        ring_pix_obj = PyLong_FromLongLong(ring_pix_data[0]);
    } else {
        npy_intp *dims = PyArray_DIMS((PyArrayObject *)nest_pix_arr);
        ring_pix_obj = PyArray_SimpleNewFromData(1, dims, NPY_INT64, ring_pix_data);
    }

    Py_DECREF(nest_pix_arr);

    return ring_pix_obj;

 fail:
    if (ring_pix_data != NULL) {
        free(ring_pix_data);
    }
    Py_XDECREF(nest_pix_arr);

    return NULL;
}

static PyObject *ring_to_nest(PyObject *dummy, PyObject *args,
                              PyObject *kwargs) {
    int64_t nside;
    PyObject *ring_pix_obj;
    PyObject *ring_pix_arr;
    static char *kwlist[] = {"nside", "pix", NULL};

    int64_t *ring_pix_data = NULL, *nest_pix_data = NULL;
    healpix_info hpx;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "LO", kwlist, &nside, &ring_pix_obj))
        return NULL;

    ring_pix_arr = PyArray_FROM_OTF(ring_pix_obj, NPY_INT64,
                                    NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (ring_pix_arr == NULL)
        return NULL;

    int ndim_pix = PyArray_NDIM((PyArrayObject *)ring_pix_arr);

    bool is_scalar = (ndim_pix == 0);

    npy_intp pix_size = PyArray_SIZE((PyArrayObject *)ring_pix_arr);

    if ((nest_pix_data = (int64_t *)calloc(pix_size, sizeof(int64_t))) == NULL) {
        PyErr_SetString(PyExc_RuntimeError,
                    "Could not allocate memory for a array.");
        goto fail;
    }

    ring_pix_data = (int64_t *)PyArray_DATA((PyArrayObject *)ring_pix_arr);

    if (!hpgeom_check_nside(nside, NEST, err)) {
        PyErr_SetString(PyExc_ValueError, err);
        goto fail;
    }
    hpx = healpix_info_from_nside(nside, NEST);

    for (npy_intp i = 0; i < pix_size; i++) {
        if (!hpgeom_check_pixel(hpx, ring_pix_data[i], err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        nest_pix_data[i] = ring2nest(hpx, ring_pix_data[i]);
    }

    PyObject *nest_pix_obj;
    if (is_scalar) {
        nest_pix_obj = PyLong_FromLongLong(nest_pix_data[0]);
    } else {
        npy_intp *dims = PyArray_DIMS((PyArrayObject *)ring_pix_arr);
        nest_pix_obj = PyArray_SimpleNewFromData(1, dims, NPY_INT64, nest_pix_data);
    }

    Py_DECREF(ring_pix_arr);

    return nest_pix_obj;

 fail:
    if (nest_pix_data != NULL) {
        free(nest_pix_data);
    }
    Py_XDECREF(ring_pix_arr);

    return NULL;
}

PyDoc_STRVAR(
    angle_to_pixel_doc,
    "angle_to_pixel(nside, a, b, nest=True, lonlat=True, degrees=True)\n"
    "--\n\n"
    "Convert angles to pixels.\n"
    "\n"
    "Parameters\n"
    "----------\n"
    "nside : `int`\n"
    "    HEALPix nside.  Must be power of 2 for nest ordering.\n"
    "a : `np.ndarray` (N,)\n"
    "    Longitude or theta (radians if lonlat=False, degrees if lonlat=True "
    "and degrees=True)\n"
    "b : `np.ndarray` (N,)\n"
    "    Latitude or phi (radians if lonlat=False, degrees if lonlat=True "
    "and degrees=True)\n"
    "nest : `bool`, optional\n"
    "    Use nest ordering scheme?\n"
    "lonlat : `bool`, optional\n"
    "    Use longitude/latitude instead of longitude/co-latitude (radians).\n"
    "degrees : `bool`, optional\n"
    "    If lonlat is True then this sets if the units are degrees or "
    "radians.\n"
    "\n"
    "Returns\n"
    "-------\n"
    "pix : `np.ndarray` (N,)\n"
    "    HEALPix pixel numbers.\n");

PyDoc_STRVAR(
    pixel_to_angle_doc,
    "pixel_to_angle(nside, pix, nest=True, lonlat=True, degrees=True)\n"
    "--\n\n"
    "Convert pixels to angles.\n"
    "\n"
    "Parameters\n"
    "----------\n"
    "nside : `int`\n"
    "    HEALPix nside.  Must be power of 2 for nest ordering.\n"
    "pix : `np.ndarray` (N,)\n"
    "    Pixel numbers\n"
    "nest : `bool`, optional\n"
    "    Use nest ordering scheme?\n"
    "lonlat : `bool`, optional\n"
    "    Output longitude/latitude instead of longitude/co-latitude "
    "(radians).\n"
    "degrees : `bool`, optional\n"
    "    If lonlat is True then this sets if the units are degrees or "
    "radians.\n"
    "\n"
    "Returns\n"
    "-------\n"
    "a : `np.ndarray` (N,)\n"
    "    Longitude or theta (radians if lonlat=False, degrees if lonlat=True "
    "and degrees=True)\n"
    "b : `np.ndarray` (N,)\n"
    "    Latitude or phi\n");

PyDoc_STRVAR(
    query_circle_doc,
    "query_circle(nside, a, b, radius, inclusive=False, fact=4, nest=True, "
    "lonlat=True, degrees=True)\n"
    "--\n\n"
    "Returns pixels whose centers lie within the circle defined by a, b\n"
    "([lon, lat] if lonlat=True otherwise [theta, phi]) and radius (in \n"
    "degrees if lonlat=True and degrees=True, otherwise radians) if\n"
    "inclusive is False, or which overlap with this circle (if inclusive\n"
    "is True)."
    "\n"
    "Parameters\n"
    "----------\n"
    "nside : `int`\n"
    "    HEALPix nside. Must be power of 2 for nest ordering.\n"
    "a : `float`\n"
    "    Longitude or theta (radians if lonlat=False, degrees if lonlat=True "
    "and degrees=True)\n"
    "b : `float`\n"
    "    Latitude or phi (radians if lonlat=False, degrees if lonlat=True "
    "and degrees=True)\n"
    "radius : `float`\n"
    "    The radius (in radians) of the circle.\n"
    "inclusive : `bool`, optional\n"
    "    If False, return the exact set of pixels whose pixel centers lie\n"
    "    within the circle. If True, return all pixels that overlap with\n"
    "    the circle. This is an approximation and may return a few extra\n"
    "    pixels.\n"
    "fact : `int`, optional\n"
    "    Only used when inclusive=True. The overlap test is performed at\n"
    "    a resolution fact*nside. For nest ordering, fact must be a power\n"
    "    of 2, and nside*fact must always be <= 2**29.  For ring ordering\n"
    "    fact may be any positive integer.\n"
    "nest : `bool`, optional\n"
    "    If True, use nest ordering.\n"
    "\n"
    "Returns\n"
    "-------\n"
    "pixels : `np.ndarray` (N,)\n"
    "    Array of pixels (`np.int64`) which cover the circle.\n");

PyDoc_STRVAR(
    nest_to_ring_doc,
    "nest_to_ring(nside, pix)\n"
    "--\n\n"
    "Convert pixel number from nest to ring ordering.\n"
    "\n"
    "Parameters\n"
    "----------\n"
    "nside : `int`, scalar\n"
    "    The healpix nside parameter.  Must be power of 2.\n"
    "pix : `int` or `np.ndarray` (N,)\n"
    "    The pixel numbers in nest scheme.\n"
    "\n"
    "Returns\n"
    "-------\n"
    "pix : `int` or `np.ndarray` (N,)\n"
    "    The pixel numbers in ring scheme.\n");

PyDoc_STRVAR(
    ring_to_nest_doc,
    "ring_to_nest(nside, pix)\n"
    "--\n\n"
    "Convert pixel number from ring to nest ordering.\n"
    "\n"
    "Parameters\n"
    "----------\n"
    "nside : `int`, scalar\n"
    "    The healpix nside parameter.  Must be power of 2.\n"
    "pix : `int` or `np.ndarray` (N,)\n"
    "    The pixel numbers in ring scheme.\n"
    "\n"
    "Returns\n"
    "-------\n"
    "pix : `int` or `np.ndarray` (N,)\n"
    "    The pixel numbers in nest scheme.\n");

static PyMethodDef hpgeom_methods[] = {
    {"angle_to_pixel", (PyCFunction)(void (*)(void))angle_to_pixel,
     METH_VARARGS | METH_KEYWORDS, angle_to_pixel_doc},
    {"pixel_to_angle", (PyCFunction)(void (*)(void))pixel_to_angle,
     METH_VARARGS | METH_KEYWORDS, pixel_to_angle_doc},
    {"query_circle", (PyCFunction)(void (*)(void))query_circle,
     METH_VARARGS | METH_KEYWORDS, query_circle_doc},
    {"nest_to_ring", (PyCFunction)(void (*)(void))nest_to_ring,
     METH_VARARGS | METH_KEYWORDS, nest_to_ring_doc},
    {"ring_to_nest", (PyCFunction)(void (*)(void))ring_to_nest,
     METH_VARARGS | METH_KEYWORDS, ring_to_nest_doc},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef hpgeom_module = {PyModuleDef_HEAD_INIT, "_hpgeom",
                                           NULL, -1, hpgeom_methods};

PyMODINIT_FUNC PyInit__hpgeom(void) {
  import_array();
  return PyModule_Create(&hpgeom_module);
}
