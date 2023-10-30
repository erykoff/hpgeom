/*
 *  Copyright (C) 2022 LSST DESC
 *  Author: Eli Rykoff
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <Python.h>

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION

#include <numpy/arrayobject.h>
#include <stdio.h>

#include "healpix_geom.h"
#include "hpgeom_stack.h"
#include "hpgeom_utils.h"

#define NSIDE_DOC_PAR                      \
    "nside : `int` or `np.ndarray` (N,)\n" \
    "    HEALPix nside.  Must be power of 2 for nest ordering.\n"
#define AB_DOC_DESCR                                                                 \
    "    Longitude/latitude (if lonlat=True) or Co-latitude(theta)/longitude(phi)\n" \
    "    (if lonlat=False). Longitude/latitude will be in degrees if degrees=True\n" \
    "    and in radians if degrees=False. Theta/phi are always in radians.\n"

#define AB_DOC_PAR "a, b : `float` or `np.ndarray` (N,)\n" AB_DOC_DESCR
#define NEST_DOC_PAR            \
    "nest : `bool`, optional\n" \
    "    Use nest ordering scheme?\n"
#define LONLAT_DOC_PAR            \
    "lonlat : `bool`, optional\n" \
    "    Use longitude/latitude for a, b instead of co-latitude/longitude.\n"
#define DEGREES_DOC_PAR            \
    "degrees : `bool`, optional\n" \
    "    If lonlat=True then this sets if the units are degrees or radians.\n"
#define PIX_DOC_PAR                         \
    "pixels : `int` or `np.ndarray` (N,)\n" \
    "    HEALPix pixel numbers.\n"
#define FACT_DOC_PAR                                                         \
    "fact : `int`, optional\n"                                               \
    "    Only used when inclusive=True. The overlap test is performed at\n"  \
    "    a resolution fact*nside. For nest ordering, fact must be a power\n" \
    "    of 2, and nside*fact must always be <= 2**29.  For ring ordering\n" \
    "    fact may be any positive integer.\n"

PyDoc_STRVAR(angle_to_pixel_doc,
             "angle_to_pixel(nside, a, b, nest=True, lonlat=True, degrees=True)\n"
             "--\n\n"
             "Convert angles to pixels.\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR AB_DOC_PAR NEST_DOC_PAR LONLAT_DOC_PAR
                 DEGREES_DOC_PAR
             "\n"
             "Returns\n"
             "-------\n" PIX_DOC_PAR
             "\n"
             "Raises\n"
             "------\n"
             "ValueError\n"
             "    If angles are out of range, or arrays cannot be broadcast"
             "    together.\n");

static PyObject *angle_to_pixel(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    PyObject *nside_obj = NULL, *a_obj = NULL, *b_obj = NULL;
    PyObject *nside_arr = NULL, *a_arr = NULL, *b_arr = NULL;
    PyObject *pix_arr = NULL;
    PyArrayMultiIterObject *itr = NULL;
    int lonlat = 1;
    int nest = 1;
    int degrees = 1;
    static char *kwlist[] = {"nside", "a", "b", "lonlat", "nest", "degrees", NULL};

    int64_t *pixels = NULL;
    double theta, phi;
    healpix_info hpx;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOO|ppp", kwlist, &nside_obj, &a_obj,
                                     &b_obj, &lonlat, &nest, &degrees))
        goto fail;

    nside_arr =
        PyArray_FROM_OTF(nside_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nside_arr == NULL) goto fail;
    a_arr = PyArray_FROM_OTF(a_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (a_arr == NULL) goto fail;
    b_arr = PyArray_FROM_OTF(b_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (b_arr == NULL) goto fail;

    itr = (PyArrayMultiIterObject *)PyArray_MultiIterNew(3, nside_arr, a_arr, b_arr);
    if (itr == NULL) {
        PyErr_SetString(PyExc_ValueError,
                        "nside, a, b arrays could not be broadcast together.");
        goto fail;
    }

    pix_arr = PyArray_SimpleNew(itr->nd, itr->dimensions, NPY_INT64);
    if (pix_arr == NULL) goto fail;
    pixels = (int64_t *)PyArray_DATA((PyArrayObject *)pix_arr);

    enum Scheme scheme;
    if (nest) {
        scheme = NEST;
    } else {
        scheme = RING;
    }

    int64_t *nside;
    double *a, *b;
    int64_t last_nside = -1;
    bool started = false;
    while (PyArray_MultiIter_NOTDONE(itr)) {
        nside = (int64_t *)PyArray_MultiIter_DATA(itr, 0);
        a = (double *)PyArray_MultiIter_DATA(itr, 1);
        b = (double *)PyArray_MultiIter_DATA(itr, 2);

        if ((!started) || (*nside != last_nside)) {
            if (!hpgeom_check_nside(*nside, scheme, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            hpx = healpix_info_from_nside(*nside, scheme);
            started = true;
        }
        if (lonlat) {
            if (!hpgeom_lonlat_to_thetaphi(*a, *b, &theta, &phi, (bool)degrees, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
        } else {
            if (!hpgeom_check_theta_phi(*a, *b, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            theta = *a;
            phi = *b;
        }
        pixels[itr->index] = ang2pix(&hpx, theta, phi);
        PyArray_MultiIter_NEXT(itr);
    }

    Py_DECREF(nside_arr);
    Py_DECREF(a_arr);
    Py_DECREF(b_arr);
    Py_DECREF(itr);

    return PyArray_Return((PyArrayObject *)pix_arr);

fail:
    Py_XDECREF(nside_arr);
    Py_XDECREF(a_arr);
    Py_XDECREF(b_arr);
    Py_XDECREF(pix_arr);
    Py_XDECREF(itr);

    return NULL;
}

PyDoc_STRVAR(pixel_to_angle_doc,
             "pixel_to_angle(nside, pix, nest=True, lonlat=True, degrees=True)\n"
             "--\n\n"
             "Convert pixels to angles.\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR PIX_DOC_PAR NEST_DOC_PAR LONLAT_DOC_PAR
                 DEGREES_DOC_PAR
             "\n"
             "Returns\n"
             "-------\n" AB_DOC_PAR
             "\n"
             "Raises\n"
             "------\n"
             "ValueError\n"
             "    If pixel values are out of range, or arrays cannot be broadcast"
             "    together.\n");

static PyObject *pixel_to_angle(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    PyObject *nside_obj = NULL, *pix_obj = NULL;
    PyObject *nside_arr = NULL, *pix_arr = NULL;
    PyObject *a_arr = NULL, *b_arr = NULL;
    PyArrayMultiIterObject *itr = NULL;
    int lonlat = 1;
    int nest = 1;
    int degrees = 1;
    static char *kwlist[] = {"nside", "pix", "lonlat", "nest", "degrees", NULL};

    double *as = NULL, *bs = NULL;
    double theta, phi;
    healpix_info hpx;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|ppp", kwlist, &nside_obj, &pix_obj,
                                     &lonlat, &nest, &degrees))
        goto fail;

    nside_arr =
        PyArray_FROM_OTF(nside_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nside_arr == NULL) goto fail;
    pix_arr = PyArray_FROM_OTF(pix_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (pix_arr == NULL) goto fail;

    itr = (PyArrayMultiIterObject *)PyArray_MultiIterNew(2, nside_arr, pix_arr);
    if (itr == NULL) {
        PyErr_SetString(PyExc_ValueError,
                        "nside, pix arrays could not be broadcast together.");
        goto fail;
    }

    a_arr = PyArray_SimpleNew(itr->nd, itr->dimensions, NPY_FLOAT64);
    if (a_arr == NULL) goto fail;
    as = (double *)PyArray_DATA((PyArrayObject *)a_arr);

    b_arr = PyArray_SimpleNew(itr->nd, itr->dimensions, NPY_FLOAT64);
    if (b_arr == NULL) goto fail;
    bs = (double *)PyArray_DATA((PyArrayObject *)b_arr);

    enum Scheme scheme;
    if (nest) {
        scheme = NEST;
    } else {
        scheme = RING;
    }

    int64_t *nside;
    int64_t *pix;
    int64_t last_nside = -1;
    bool started = false;
    while (PyArray_MultiIter_NOTDONE(itr)) {
        nside = (int64_t *)PyArray_MultiIter_DATA(itr, 0);
        pix = (int64_t *)PyArray_MultiIter_DATA(itr, 1);

        if ((!started) || (*nside != last_nside)) {
            if (!hpgeom_check_nside(*nside, scheme, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            hpx = healpix_info_from_nside(*nside, scheme);
            started = true;
        }
        if (!hpgeom_check_pixel(&hpx, *pix, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        pix2ang(&hpx, *pix, &theta, &phi);
        if (lonlat) {
            // We can skip error checking since theta/phi will always be
            // within range on output.
            hpgeom_thetaphi_to_lonlat(theta, phi, &as[itr->index], &bs[itr->index],
                                      (bool)degrees, false, err);
        } else {
            as[itr->index] = theta;
            bs[itr->index] = phi;
        }
        PyArray_MultiIter_NEXT(itr);
    }

    Py_DECREF(nside_arr);
    Py_DECREF(pix_arr);
    Py_DECREF(itr);

    PyObject *retval = PyTuple_New(2);
    PyTuple_SET_ITEM(retval, 0, PyArray_Return((PyArrayObject *)a_arr));
    PyTuple_SET_ITEM(retval, 1, PyArray_Return((PyArrayObject *)b_arr));

    return retval;

fail:
    Py_XDECREF(nside_arr);
    Py_XDECREF(pix_arr);
    Py_XDECREF(a_arr);
    Py_XDECREF(b_arr);
    Py_XDECREF(itr);

    return NULL;
}

PyDoc_STRVAR(query_circle_doc,
             "query_circle(nside, a, b, radius, inclusive=False, fact=4, nest=True, "
             "lonlat=True, degrees=True)\n"
             "--\n\n"
             "Returns pixels whose centers lie within the circle defined by a, b\n"
             "([lon, lat] if lonlat=True otherwise [theta, phi]) and radius (in \n"
             "degrees if lonlat=True and degrees=True, otherwise radians) if\n"
             "inclusive is False, or which overlap with this circle (if inclusive\n"
             "is True).\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR "a, b : `float`\n" AB_DOC_DESCR
             "radius : `float`\n"
             "    The radius of the circle. Degrees if degrees=True otherwise radians.\n"
             "inclusive : `bool`, optional\n"
             "    If False, return the exact set of pixels whose pixel centers lie\n"
             "    within the circle. If True, return all pixels that overlap with\n"
             "    the circle. This is an approximation and may return a few extra\n"
             "    pixels.\n" FACT_DOC_PAR NEST_DOC_PAR LONLAT_DOC_PAR DEGREES_DOC_PAR
             "\n"
             "Returns\n"
             "-------\n"
             "pixels : `np.ndarray` (N,)\n"
             "    Array of pixels (`np.int64`) which cover the circle.\n"
             "\n"
             "Raises\n"
             "------\n"
             "ValueError\n"
             "    If position or radius are out of range, or fact is not allowed.\n"
             "RuntimeError\n"
             "    If query_circle has an internal error.\n"
             "\n"
             "Notes\n"
             "-----\n"
             "This method is more efficient with ring ordering.\n"
             "For inclusive=True, the algorithm may return some pixels which do not overlap\n"
             "with the circle. Higher fact values result in fewer false positives at the\n"
             "expense of increased run time.\n");

static PyObject *query_circle(PyObject *dummy, PyObject *args, PyObject *kwargs) {
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
    i64rangeset *pixset = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Lddd|plppp", kwlist, &nside, &a, &b,
                                     &radius, &inclusive, &fact, &nest, &lonlat, &degrees))
        goto fail;

    double theta, phi;
    if (lonlat) {
        if (!hpgeom_lonlat_to_thetaphi(a, b, &theta, &phi, (bool)degrees, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        if (degrees) {
            radius *= HPG_D2R;
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
        if (!hpgeom_check_fact(&hpx, fact, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
    }
    query_disc(&hpx, theta, phi, radius, fact, pixset, &status, err);

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

    return PyArray_Return((PyArrayObject *)pix_arr);

fail:
    i64rangeset_delete(pixset);

    return NULL;
}

PyDoc_STRVAR(query_polygon_doc,
             "query_polygon(nside, a, b, inclusive=False, fact=4, nest=True, lonlat=True, "
             "degrees=True)\n"
             "--\n\n"
             "Returns pixels whose centers lie within the convex polygon defined by the "
             "points in a, b\n"
             "([lon, lat] if lonlat=True, otherwise [theta, phi]) if inclusive is False, or "
             "which overlap\n"
             "with this polygon (if inclusive is True).\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR "a, b : `np.ndarray` (N,)\n" AB_DOC_DESCR
             "inclusive : `bool`, optional\n"
             "    If False, return the exact set of pixels whose pixel centers lie\n"
             "    within the polygon. If True, return all pixels that overlap with\n"
             "    the polygon. This is an approximation and may return a few extra\n"
             "    pixels.\n" FACT_DOC_PAR NEST_DOC_PAR LONLAT_DOC_PAR DEGREES_DOC_PAR
             "\n"
             "Returns\n"
             "-------\n"
             "pixels : `np.ndarray` (N,)\n"
             "    Array of pixels (`np.int64`) which cover the polygon.\n"
             "\n"
             "Raises\n"
             "------\n"
             "ValueError\n"
             "    If vertices are out of range.\n"
             "RuntimeError\n"
             "    If polygon does not have at least 3 vertices, or polygon is not convex,\n"
             "    or polygon has degenerate corners, or there is an internal error.\n"
             "\n"
             "Notes\n"
             "-----\n"
             "This method is more efficient with nest ordering.\n"
             "For inclusive=True, the algorithm may return some pixels which do not overlap\n"
             "with the polygon. Higher fact values result in fewer false positives at the\n"
             "expense of increased run time.\n");

static PyObject *query_polygon_meth(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    int64_t nside;
    PyObject *a_obj = NULL, *b_obj = NULL;
    PyObject *a_arr = NULL, *b_arr = NULL;
    int inclusive = 0;
    long fact = 4;
    int nest = 1;
    int lonlat = 1;
    int degrees = 1;
    static char *kwlist[] = {"nside", "a",      "b",       "inclusive", "fact",
                             "nest",  "lonlat", "degrees", NULL};
    char err[ERR_SIZE];
    int status = 1;
    i64rangeset *pixset = NULL;
    pointingarr *vertices = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "LOO|plppp", kwlist, &nside, &a_obj, &b_obj,
                                     &inclusive, &fact, &nest, &lonlat, &degrees))
        goto fail;

    a_arr = PyArray_FROM_OTF(a_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (a_arr == NULL) goto fail;
    b_arr = PyArray_FROM_OTF(b_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (b_arr == NULL) goto fail;

    if (PyArray_NDIM((PyArrayObject *)a_arr) != 1) {
        PyErr_SetString(PyExc_ValueError, "a array must be 1D.");
        goto fail;
    }
    if (PyArray_NDIM((PyArrayObject *)b_arr) != 1) {
        PyErr_SetString(PyExc_ValueError, "b array must be 1D.");
        goto fail;
    }

    npy_intp nvert = PyArray_DIM((PyArrayObject *)a_arr, 0);
    if (PyArray_DIM((PyArrayObject *)b_arr, 0) != nvert) {
        PyErr_SetString(PyExc_ValueError, "a and b arrays must be the same length.");
        goto fail;
    }
    if (nvert < 3) {
        PyErr_SetString(PyExc_RuntimeError, "Polygon must have at least 3 vertices.");
        goto fail;
    }

    vertices = pointingarr_new(nvert, &status, err);
    if (!status) {
        PyErr_SetString(PyExc_RuntimeError, err);
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
        if (!hpgeom_check_fact(&hpx, fact, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
    }

    double *a_data = (double *)PyArray_DATA((PyArrayObject *)a_arr);
    double *b_data = (double *)PyArray_DATA((PyArrayObject *)b_arr);

    double theta, phi;
    for (npy_intp i = 0; i < nvert; i++) {
        if (lonlat) {
            if (!hpgeom_lonlat_to_thetaphi(a_data[i], b_data[i], &theta, &phi, (bool)degrees,
                                           err)) {
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
        vertices->data[i].theta = theta;
        vertices->data[i].phi = phi;
    }

    // Check for a closed polygon with a small double-precision delta.
    double delta_theta = fabs(vertices->data[nvert - 1].theta - vertices->data[0].theta);
    double delta_phi = fabs(vertices->data[nvert - 1].phi - vertices->data[0].phi);
    if ((delta_theta < 1e-14) && (delta_phi < 1e-14)) {
        // Skip last coord
        vertices->size--;
    }

    query_polygon(&hpx, vertices, fact, pixset, &status, err);
    if (!status) {
        PyErr_SetString(PyExc_RuntimeError, err);
        goto fail;
    }

    size_t npix = i64rangeset_npix(pixset);
    npy_intp dims[1];
    dims[0] = (npy_intp)npix;

    PyObject *pix_arr = PyArray_SimpleNew(1, dims, NPY_INT64);
    if (pix_arr == NULL) goto fail;
    int64_t *pix_data = (int64_t *)PyArray_DATA((PyArrayObject *)pix_arr);

    i64rangeset_fill_buffer(pixset, npix, pix_data);

    Py_DECREF(a_arr);
    Py_DECREF(b_arr);
    i64rangeset_delete(pixset);
    pointingarr_delete(vertices);

    return PyArray_Return((PyArrayObject *)pix_arr);

fail:
    Py_XDECREF(a_arr);
    Py_XDECREF(b_arr);
    i64rangeset_delete(pixset);
    pointingarr_delete(vertices);

    return NULL;
}

PyDoc_STRVAR(query_ellipse_doc,
             "query_ellipse(nside, a, b, semi_major, semi_minor, alpha, inclusive=False, "
             "fact=4, nest=True, lonlat=True, degrees=True)\n"
             "--\n\n"
             "Returns pixels whose centers lie within an ellipse if inclusive is False,\n"
             "or which overlap with this ellipse if inclusive is True. The ellipse is\n"
             "defined by a center a, b ([lon, lat] if lonlat=True otherwise [theta, phi]\n"
             "and semi-major and semi-minor axes (in degrees if lonlat=True and\n"
             "degrees=True, otherwise radians). The inclination angle alpha is defined\n"
             "East of North, and is in degrees if lonlat=True and degrees=True,\n"
             "otherwise radians. The shape of the ellipse is defined by the set\n"
             "of points where the sum of the distances from a point to each of the\n"
             "foci add up to less than twice the semi-major axis.\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR "a, b : `float`\n" AB_DOC_DESCR
             "semi_major, semi_minor : `float`\n"
             "    The semi-major and semi-minor axes of the ellipse. Degrees if degrees=True\n"
             "    and lonlat=True, otherwise radians. The semi-major axis must be >= the\n"
             "    semi-minor axis.\n"
             "alpha : `float`\n"
             "    Inclination angle, counterclockwise with respect to North. Degrees if\n"
             "    degrees=True and lonlat=True, otherwise radians.\n"
             "inclusive : `bool`, optional\n"
             "    If False, return the exact set of pixels whose pixel centers lie\n"
             "    within the ellipse. If True, return all pixels that overlap with\n"
             "    the ellipse. This is an approximation and may return a few extra\n"
             "    pixels.\n" FACT_DOC_PAR NEST_DOC_PAR LONLAT_DOC_PAR DEGREES_DOC_PAR
             "\n"
             "Returns\n"
             "-------\n"
             "pixels : `np.ndarray` (N,)\n"
             "    Array of pixels (`np.int64`) which cover the ellipse.\n"
             "\n"
             "Raises\n"
             "------\n"
             "ValueError\n"
             "    If position or semi-major/minor axes are out of range, or fact is \n"
             "    not allowed.\n"
             "RuntimeError\n"
             "    If query_ellipse has an internal error.\n"
             "\n"
             "Notes\n"
             "-----\n"
             "This method runs natively only with nest ordering. If called with ring\n"
             "ordering then a ResourceWarning is emitted and the pixel numbers will be\n"
             "converted to ring and sorted before output.\n"
             "For inclusive=True, the algorithm may return some pixels which do not overlap\n"
             "with the ellipse. Higher fact values result in fewer false positives at the\n"
             "expense of increased run time.\n");

static PyObject *query_ellipse_meth(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    int64_t nside;
    double a, b, semi_major, semi_minor, alpha;
    int inclusive = 0;
    long fact = 4;
    int nest = 1;
    int lonlat = 1;
    int degrees = 1;
    static char *kwlist[] = {"nside",     "a",    "b",    "semi_major", "semi_minor", "alpha",
                             "inclusive", "fact", "nest", "lonlat",     "degrees",    NULL};

    char err[ERR_SIZE];
    int status = 1;
    i64rangeset *pixset = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Lddddd|plppp", kwlist, &nside, &a, &b,
                                     &semi_major, &semi_minor, &alpha, &inclusive, &fact,
                                     &nest, &lonlat, &degrees))
        goto fail;

    double theta, phi;
    if (lonlat) {
        if (!hpgeom_lonlat_to_thetaphi(a, b, &theta, &phi, (bool)degrees, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        if (degrees) {
            semi_major *= HPG_D2R;
            semi_minor *= HPG_D2R;
            alpha *= HPG_D2R;
        }
    } else {
        if (!hpgeom_check_theta_phi(a, b, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        theta = a;
        phi = b;
    }

    if (!hpgeom_check_semi(semi_major, semi_minor, err)) {
        PyErr_SetString(PyExc_ValueError, err);
        goto fail;
    }

    if (!nest) {
        PyErr_WarnEx(PyExc_ResourceWarning,
                     "query_ellipse natively supports nest ordering.  Result will be "
                     "converted from nest->ring and sorted",
                     0);
    }

    if (!hpgeom_check_nside(nside, NEST, err)) {
        PyErr_SetString(PyExc_ValueError, err);
        goto fail;
    }
    healpix_info hpx = healpix_info_from_nside(nside, NEST);

    pixset = i64rangeset_new(&status, err);
    if (!status) {
        PyErr_SetString(PyExc_RuntimeError, err);
        goto fail;
    }

    if (!inclusive) {
        fact = 0;
    } else {
        if (!hpgeom_check_fact(&hpx, fact, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
    }
    query_ellipse(&hpx, theta, phi, semi_major, semi_minor, alpha, fact, pixset, &status, err);

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

    if (!nest) {
        // Convert from nest to ring
        for (size_t i = 0; i < npix; i++) pix_data[i] = nest2ring(&hpx, pix_data[i]);

        // And sort the pixels, as is expected.
        PyArray_Sort((PyArrayObject *)pix_arr, 0, NPY_QUICKSORT);
    }

    return PyArray_Return((PyArrayObject *)pix_arr);

fail:
    i64rangeset_delete(pixset);

    return NULL;
}

PyDoc_STRVAR(
    query_box_doc,
    "query_box(nside, a0, a1, b0, b1, inclusive=False, fact=4, nest=True, lonlat=True, "
    "degrees=True)\n"
    "--\n\n"
    "Returns pixels whose centers lie within a box if inclusive is False,\n"
    "or which overlap with this box if inclusive is True. The box is defined\n"
    "by all the points within [a0, a1] and [b0, b1] ([lon, lat] if lonlat=True\n"
    "otherwise [theta, phi] (in degrees if lonlat=True and degrees=True, otherwise\n"
    "radians). The box will have boundaries in constant longitude/latitude, rather\n"
    "than great circle boundaries as with query_polygon. If a0 > a1 then the box will\n"
    "wrap around 360 degrees. If a0 == 0.0 and a1 == 360.0 then the box will contain\n"
    "points at all longitudes. If b0 == 90.0 or -90.0 then the box will be an arc\n"
    "of a circle with the center at the north/south pole.\n"
    "\n"
    "Parameters\n"
    "----------\n" NSIDE_DOC_PAR "a0, a1, b0, b1 : `float`\n" AB_DOC_DESCR
    "inclusive : `bool`, optional\n"
    "    If False, return the exact set of pixels whose pixel centers lie\n"
    "    within the box. If True, return all pixels that overlap with\n"
    "    the box. This is an approximation and may return a few extra\n"
    "    pixels.\n" FACT_DOC_PAR NEST_DOC_PAR LONLAT_DOC_PAR DEGREES_DOC_PAR
    "\n"
    "Returns\n"
    "-------\n"
    "pixels : `np.ndarray` (N,)\n"
    "    Array of pixels (`np.int64`) which cover the box.\n"
    "\n"
    "Raises\n"
    "------\n"
    "ValueError\n"
    "    If positions are out of range, or fact is not allowed.\n"
    "RuntimeError\n"
    "    If query_box has an internal error.\n"
    "\n"
    "Notes\n"
    "-----\n"
    "This method runs natively only with nest ordering. If called with ring\n"
    "ordering then a ResourceWarning is emitted and the pixel numbers will be\n"
    "converted to ring and sorted before output.\n"
    "For inclusive=True, the algorithm may return some pixels which do not overlap\n"
    "with the box. Higher fact values result in fewer false positives at the\n"
    "expense of increased run time.\n");

static PyObject *query_box_meth(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    int64_t nside;
    double a0, a1, b0, b1;
    int inclusive = 0;
    long fact = 4;
    int nest = 1;
    int lonlat = 1;
    int degrees = 1;
    static char *kwlist[] = {"nside", "a0",   "a1",     "b0",      "b1", "inclusive",
                             "fact",  "nest", "lonlat", "degrees", NULL};

    char err[ERR_SIZE];
    int status = 1;
    i64rangeset *pixset = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Ldddd|plppp", kwlist, &nside, &a0, &a1,
                                     &b0, &b1, &inclusive, &fact, &nest, &lonlat, &degrees))
        goto fail;

    double theta0, theta1, phi0, phi1;
    bool full_lon = false;
    if (lonlat) {
        if (a0 == 0.0 && a1 == 360.0) full_lon = true;
        if (b0 > b1) {
            PyErr_SetString(PyExc_ValueError, "b1/lat1 must be >= b0/lat0.");
            goto fail;
        }
        // Swap theta ordering.
        if (!hpgeom_lonlat_to_thetaphi(a0, b0, &theta1, &phi0, (bool)degrees, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        if (!hpgeom_lonlat_to_thetaphi(a1, b1, &theta0, &phi1, (bool)degrees, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
    } else {
        if (b0 == 0.0 && b1 == HPG_TWO_PI) full_lon = true;
        if (a0 > a1) {
            PyErr_SetString(PyExc_ValueError, "a1/colatitude1 must be <= a0/colatitude0.");
            goto fail;
        }
        if (!hpgeom_check_theta_phi(a0, b0, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        theta0 = a0;
        phi0 = b0;
        if (!hpgeom_check_theta_phi(a1, b1, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        theta1 = a1;
        phi1 = b1;
    }

    if (!nest) {
        PyErr_WarnEx(PyExc_ResourceWarning,
                     "query_box natively supports nest ordering.  Result will be "
                     "converted from nest->ring and sorted",
                     0);
    }

    if (!hpgeom_check_nside(nside, NEST, err)) {
        PyErr_SetString(PyExc_ValueError, err);
        goto fail;
    }
    healpix_info hpx = healpix_info_from_nside(nside, NEST);

    pixset = i64rangeset_new(&status, err);
    if (!status) {
        PyErr_SetString(PyExc_RuntimeError, err);
        goto fail;
    }

    if (!inclusive) {
        fact = 0;
    } else {
        if (!hpgeom_check_fact(&hpx, fact, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
    }
    query_box(&hpx, theta0, theta1, phi0, phi1, full_lon, fact, pixset, &status, err);

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

    if (!nest) {
        // Convert from nest to ring
        for (size_t i = 0; i < npix; i++) pix_data[i] = nest2ring(&hpx, pix_data[i]);

        // And sort the pixels, as is expected.
        PyArray_Sort((PyArrayObject *)pix_arr, 0, NPY_QUICKSORT);
    }

    return PyArray_Return((PyArrayObject *)pix_arr);

fail:
    i64rangeset_delete(pixset);

    return NULL;
}

PyDoc_STRVAR(nest_to_ring_doc,
             "nest_to_ring(nside, pix)\n"
             "--\n\n"
             "Convert pixel number from nest to ring ordering.\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR
             "pix : `int` or `np.ndarray` (N,)\n"
             "    The pixel numbers in nest scheme.\n"
             "\n"
             "Returns\n"
             "-------\n"
             "pix : `int` or `np.ndarray` (N,)\n"
             "    The pixel numbers in ring scheme.\n"
             "\n"
             "Raises\n"
             "------\n"
             "ValueError\n"
             "    Pixel or nside values are out of range.\n");

static PyObject *nest_to_ring(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    PyObject *nside_obj = NULL, *nest_pix_obj = NULL;
    PyObject *nside_arr = NULL, *nest_pix_arr = NULL;
    PyObject *ring_pix_arr = NULL;
    PyArrayMultiIterObject *itr = NULL;
    static char *kwlist[] = {"nside", "pix", NULL};

    int64_t *ring_pix_data = NULL;
    healpix_info hpx;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO", kwlist, &nside_obj, &nest_pix_obj))
        goto fail;

    nside_arr =
        PyArray_FROM_OTF(nside_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nside_arr == NULL) goto fail;
    nest_pix_arr =
        PyArray_FROM_OTF(nest_pix_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nest_pix_arr == NULL) goto fail;

    itr = (PyArrayMultiIterObject *)PyArray_MultiIterNew(2, nside_arr, nest_pix_arr);
    if (itr == NULL) {
        PyErr_SetString(PyExc_ValueError,
                        "nside, pix arrays could not be broadcast together.");
        goto fail;
    }

    ring_pix_arr = PyArray_SimpleNew(itr->nd, itr->dimensions, NPY_INT64);
    if (ring_pix_arr == NULL) goto fail;
    ring_pix_data = (int64_t *)PyArray_DATA((PyArrayObject *)ring_pix_arr);

    int64_t *nside;
    int64_t *nest_pix;
    int64_t last_nside = -1;
    bool started = false;
    while (PyArray_MultiIter_NOTDONE(itr)) {
        nside = (int64_t *)PyArray_MultiIter_DATA(itr, 0);
        nest_pix = (int64_t *)PyArray_MultiIter_DATA(itr, 1);

        if ((!started) || (*nside != last_nside)) {
            if (!hpgeom_check_nside(*nside, NEST, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            hpx = healpix_info_from_nside(*nside, NEST);
            started = true;
        }
        if (!hpgeom_check_pixel(&hpx, *nest_pix, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        ring_pix_data[itr->index] = nest2ring(&hpx, *nest_pix);
        PyArray_MultiIter_NEXT(itr);
    }

    Py_DECREF(nside_arr);
    Py_DECREF(nest_pix_arr);
    Py_DECREF(itr);

    return PyArray_Return((PyArrayObject *)ring_pix_arr);

fail:
    Py_XDECREF(nside_arr);
    Py_XDECREF(nest_pix_arr);
    Py_XDECREF(ring_pix_arr);
    Py_XDECREF(itr);

    return NULL;
}

PyDoc_STRVAR(ring_to_nest_doc,
             "ring_to_nest(nside, pix)\n"
             "--\n\n"
             "Convert pixel number from ring to nest ordering.\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR
             "pix : `int` or `np.ndarray` (N,)\n"
             "    The pixel numbers in ring scheme.\n"
             "\n"
             "Returns\n"
             "-------\n"
             "pix : `int` or `np.ndarray` (N,)\n"
             "    The pixel numbers in nest scheme.\n"
             "\n"
             "Raises\n"
             "------\n"
             "ValueError\n"
             "    Pixel or nside values are out of range.\n");

static PyObject *ring_to_nest(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    PyObject *nside_obj = NULL, *ring_pix_obj = NULL;
    PyObject *nside_arr = NULL, *ring_pix_arr = NULL;
    PyObject *nest_pix_arr = NULL;
    PyArrayMultiIterObject *itr = NULL;
    static char *kwlist[] = {"nside", "pix", NULL};

    int64_t *nest_pix_data = NULL;
    healpix_info hpx;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO", kwlist, &nside_obj, &ring_pix_obj))
        goto fail;

    nside_arr =
        PyArray_FROM_OTF(nside_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nside_arr == NULL) goto fail;
    ring_pix_arr =
        PyArray_FROM_OTF(ring_pix_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (ring_pix_arr == NULL) goto fail;

    itr = (PyArrayMultiIterObject *)PyArray_MultiIterNew(2, nside_arr, ring_pix_arr);
    if (itr == NULL) {
        PyErr_SetString(PyExc_ValueError,
                        "nside, pix arrays could not be broadcast together.");
        goto fail;
    }

    nest_pix_arr = PyArray_SimpleNew(itr->nd, itr->dimensions, NPY_INT64);
    if (nest_pix_arr == NULL) goto fail;
    nest_pix_data = (int64_t *)PyArray_DATA((PyArrayObject *)nest_pix_arr);

    int64_t *nside;
    int64_t *ring_pix;
    int64_t last_nside = -1;
    bool started = false;
    while (PyArray_MultiIter_NOTDONE(itr)) {
        nside = (int64_t *)PyArray_MultiIter_DATA(itr, 0);
        ring_pix = (int64_t *)PyArray_MultiIter_DATA(itr, 1);

        if ((!started) || (*nside != last_nside)) {
            if (!hpgeom_check_nside(*nside, NEST, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            hpx = healpix_info_from_nside(*nside, NEST);
            started = true;
        }
        if (!hpgeom_check_pixel(&hpx, *ring_pix, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        nest_pix_data[itr->index] = ring2nest(&hpx, *ring_pix);
        PyArray_MultiIter_NEXT(itr);
    }

    Py_DECREF(nside_arr);
    Py_DECREF(ring_pix_arr);
    Py_DECREF(itr);

    return PyArray_Return((PyArrayObject *)nest_pix_arr);

fail:
    Py_XDECREF(nside_arr);
    Py_XDECREF(ring_pix_arr);
    Py_XDECREF(nest_pix_arr);
    Py_XDECREF(itr);

    return NULL;
}

PyDoc_STRVAR(boundaries_doc,
             "boundaries(nside, pix, step=1, nest=True, lonlat=True, degrees=True)\n"
             "--\n\n"
             "Returns an array containing lon/lat or colatitude/longitude to the\n"
             "boundary of the given pixel(s).\n"
             "\n"
             "The returned arrays have the shape (4*step) or (npixel, 4*step).\n"
             "In order to get coordinates for just the corners, specify step=1.\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR PIX_DOC_PAR
             "step : `int`, optional\n"
             "    Number of steps for each side of the pixel.\n" NEST_DOC_PAR LONLAT_DOC_PAR
                 DEGREES_DOC_PAR
             "\n"
             "Returns\n"
             "-------\n"
             "a, b : `np.ndarray` (4*step,) or (N, 4*step,)\n" AB_DOC_DESCR
             "\n"
             "Raises\n"
             "------\n"
             "ValueError\n"
             "    If pixel values are out of range, or nside, pix arrays are not\n"
             "    compatible, or step is not positive.\n");

static PyObject *boundaries_meth(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    PyObject *nside_obj = NULL, *pix_obj = NULL;
    PyObject *nside_arr = NULL, *pix_arr = NULL;
    PyObject *a_arr = NULL, *b_arr = NULL;
    PyArrayMultiIterObject *itr = NULL;
    int lonlat = 1;
    int nest = 1;
    int degrees = 1;
    long step = 1;
    static char *kwlist[] = {"nside", "pix", "step", "lonlat", "nest", "degrees", NULL};

    double *as = NULL, *bs = NULL;
    pointingarr *ptg_arr = NULL;
    healpix_info hpx;
    int status;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|Lppp", kwlist, &nside_obj, &pix_obj,
                                     &step, &lonlat, &nest, &degrees))
        goto fail;

    nside_arr =
        PyArray_FROM_OTF(nside_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nside_arr == NULL) goto fail;
    pix_arr = PyArray_FROM_OTF(pix_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (pix_arr == NULL) goto fail;

    if (step < 1) {
        PyErr_SetString(PyExc_ValueError, "step must be positive.");
        goto fail;
    }

    if (PyArray_NDIM((PyArrayObject *)pix_arr) > 1) {
        PyErr_SetString(PyExc_ValueError, "pix array must be at most 1D.");
        goto fail;
    }

    itr = (PyArrayMultiIterObject *)PyArray_MultiIterNew(2, nside_arr, pix_arr);
    if (itr == NULL) {
        PyErr_SetString(PyExc_ValueError,
                        "nside, pix arrays could not be broadcast together.");
        goto fail;
    }

    int ndims_pix = PyArray_NDIM((PyArrayObject *)pix_arr);
    if (ndims_pix == 0) {
        npy_intp dims[1];
        dims[0] = 4 * step;
        a_arr = PyArray_SimpleNew(1, dims, NPY_FLOAT64);
        if (a_arr == NULL) goto fail;
        b_arr = PyArray_SimpleNew(1, dims, NPY_FLOAT64);
        if (b_arr == NULL) goto fail;
    } else {
        npy_intp dims[2];
        dims[0] = PyArray_DIM((PyArrayObject *)pix_arr, 0);
        dims[1] = 4 * step;
        a_arr = PyArray_SimpleNew(2, dims, NPY_FLOAT64);
        if (a_arr == NULL) goto fail;
        b_arr = PyArray_SimpleNew(2, dims, NPY_FLOAT64);
        if (b_arr == NULL) goto fail;
    }
    as = (double *)PyArray_DATA((PyArrayObject *)a_arr);
    bs = (double *)PyArray_DATA((PyArrayObject *)b_arr);

    enum Scheme scheme;
    if (nest) {
        scheme = NEST;
    } else {
        scheme = RING;
    }

    ptg_arr = pointingarr_new(4 * step, &status, err);
    if (!status) {
        PyErr_SetString(PyExc_RuntimeError, err);
        goto fail;
    }

    int64_t *nside;
    int64_t *pix;
    int64_t last_nside = -1;
    bool started = false;
    while (PyArray_MultiIter_NOTDONE(itr)) {
        nside = (int64_t *)PyArray_MultiIter_DATA(itr, 0);
        pix = (int64_t *)PyArray_MultiIter_DATA(itr, 1);

        if ((!started) || (*nside != last_nside)) {
            if (!hpgeom_check_nside(*nside, scheme, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            hpx = healpix_info_from_nside(*nside, scheme);
            started = true;
        }

        if (!hpgeom_check_pixel(&hpx, *pix, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }

        boundaries(&hpx, *pix, step, ptg_arr, &status);
        if (!status) {
            PyErr_SetString(PyExc_RuntimeError, "Fatal programming error in boundaries.");
            goto fail;
        }

        size_t index;
        for (size_t i = 0; i < ptg_arr->size; i++) {
            index = ptg_arr->size * itr->index + i;
            if (lonlat) {
                // We can skip error checking since theta/phi will always be
                // within range on output.
                hpgeom_thetaphi_to_lonlat(ptg_arr->data[i].theta, ptg_arr->data[i].phi,
                                          &as[index], &bs[index], (bool)degrees, false, err);
            } else {
                as[index] = ptg_arr->data[i].theta;
                bs[index] = ptg_arr->data[i].phi;
            }
        }

        PyArray_MultiIter_NEXT(itr);
    }

    Py_DECREF(nside_arr);
    Py_DECREF(pix_arr);
    Py_DECREF(itr);
    pointingarr_delete(ptg_arr);

    PyObject *retval = PyTuple_New(2);
    PyTuple_SET_ITEM(retval, 0, PyArray_Return((PyArrayObject *)a_arr));
    PyTuple_SET_ITEM(retval, 1, PyArray_Return((PyArrayObject *)b_arr));

    return retval;

fail:
    Py_XDECREF(nside_arr);
    Py_XDECREF(pix_arr);
    Py_XDECREF(a_arr);
    Py_XDECREF(b_arr);
    Py_XDECREF(itr);
    pointingarr_delete(ptg_arr);

    return NULL;
}

PyDoc_STRVAR(vector_to_pixel_doc,
             "vector_to_pixel(nside, x, y, z, nest=True)\n"
             "--\n\n"
             "Convert vectors to pixels.\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR
             "x : `float` or `np.ndarray` (N,)\n"
             "    x coordinates for vectors.\n"
             "y : `float` or `np.ndarray` (N,)\n"
             "    y coordinates for vectors.\n"
             "z : `float` or `np.ndarray` (N,)\n"
             "    z coordinates for vectors.\n" NEST_DOC_PAR
             "\n"
             "Returns\n"
             "-------\n" PIX_DOC_PAR);

static PyObject *vector_to_pixel(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    PyObject *nside_obj = NULL, *x_obj = NULL, *y_obj = NULL, *z_obj = NULL;
    PyObject *nside_arr = NULL, *x_arr = NULL, *y_arr = NULL, *z_arr = NULL;
    PyObject *pix_arr = NULL;
    PyArrayMultiIterObject *itr = NULL;
    int nest = 1;
    static char *kwlist[] = {"nside", "x", "y", "z", "nest", NULL};

    int64_t *pixels = NULL;
    healpix_info hpx;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOOO|p", kwlist, &nside_obj, &x_obj,
                                     &y_obj, &z_obj, &nest))
        goto fail;

    nside_arr =
        PyArray_FROM_OTF(nside_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nside_arr == NULL) goto fail;
    x_arr = PyArray_FROM_OTF(x_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (x_arr == NULL) goto fail;
    y_arr = PyArray_FROM_OTF(y_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (y_arr == NULL) goto fail;
    z_arr = PyArray_FROM_OTF(z_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (z_arr == NULL) goto fail;

    itr = (PyArrayMultiIterObject *)PyArray_MultiIterNew(4, nside_arr, x_arr, y_arr, z_arr);
    if (itr == NULL) {
        PyErr_SetString(PyExc_ValueError,
                        "nside, x, y, z arrays could not be broadcast together.");
        goto fail;
    }

    pix_arr = PyArray_SimpleNew(itr->nd, itr->dimensions, NPY_INT64);
    if (pix_arr == NULL) goto fail;
    pixels = (int64_t *)PyArray_DATA((PyArrayObject *)pix_arr);

    enum Scheme scheme;
    if (nest) {
        scheme = NEST;
    } else {
        scheme = RING;
    }

    int64_t *nside;
    double *x, *y, *z;
    int64_t last_nside = -1;
    bool started = false;
    vec3 vec;
    while (PyArray_MultiIter_NOTDONE(itr)) {
        nside = (int64_t *)PyArray_MultiIter_DATA(itr, 0);
        x = (double *)PyArray_MultiIter_DATA(itr, 1);
        y = (double *)PyArray_MultiIter_DATA(itr, 2);
        z = (double *)PyArray_MultiIter_DATA(itr, 3);

        if ((!started) || (*nside != last_nside)) {
            if (!hpgeom_check_nside(*nside, scheme, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            hpx = healpix_info_from_nside(*nside, scheme);
            started = true;
        }
        vec.x = *x;
        vec.y = *y;
        vec.z = *z;
        pixels[itr->index] = vec2pix(&hpx, &vec);
        PyArray_MultiIter_NEXT(itr);
    }

    Py_DECREF(nside_arr);
    Py_DECREF(x_arr);
    Py_DECREF(y_arr);
    Py_DECREF(z_arr);
    Py_DECREF(itr);

    return PyArray_Return((PyArrayObject *)pix_arr);

fail:
    Py_XDECREF(nside_arr);
    Py_XDECREF(x_arr);
    Py_XDECREF(y_arr);
    Py_XDECREF(z_arr);
    Py_XDECREF(pix_arr);
    Py_XDECREF(itr);

    return NULL;
}

PyDoc_STRVAR(pixel_to_vector_doc,
             "pixel_to_vector(nside, pix, nest=True)\n"
             "--\n\n"
             "Convert pixels to vectors.\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR PIX_DOC_PAR NEST_DOC_PAR
             "\n"
             "Returns\n"
             "-------\n"
             "x : `float` or `np.ndarray` (N,)\n"
             "    x coordinates for vectors.\n"
             "y : `float` or `np.ndarray` (N,)\n"
             "    y coordinates for vectors.\n"
             "z : `float` or `np.ndarray` (N,)\n"
             "    z coordinates for vectors.\n"
             "\n"
             "Raises\n"
             "------\n"
             "ValueError\n"
             "    If pixel values are out of range, or nside and pix arrays are not\n"
             "    compatible.\n");

static PyObject *pixel_to_vector(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    PyObject *nside_obj = NULL, *pix_obj = NULL;
    PyObject *nside_arr = NULL, *pix_arr = NULL;
    PyObject *x_arr = NULL, *y_arr = NULL, *z_arr = NULL;
    PyArrayMultiIterObject *itr = NULL;
    int nest = 1;
    static char *kwlist[] = {"nside", "pix", "nest", NULL};

    double *xs = NULL, *ys = NULL, *zs = NULL;
    healpix_info hpx;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|p", kwlist, &nside_obj, &pix_obj,
                                     &nest))
        goto fail;

    nside_arr =
        PyArray_FROM_OTF(nside_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nside_arr == NULL) goto fail;
    pix_arr = PyArray_FROM_OTF(pix_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (pix_arr == NULL) goto fail;

    itr = (PyArrayMultiIterObject *)PyArray_MultiIterNew(2, nside_arr, pix_arr);
    if (itr == NULL) {
        PyErr_SetString(PyExc_ValueError,
                        "nside, pix arrays could not be broadcast together.");
        goto fail;
    }

    x_arr = PyArray_SimpleNew(itr->nd, itr->dimensions, NPY_FLOAT64);
    if (x_arr == NULL) goto fail;
    xs = (double *)PyArray_DATA((PyArrayObject *)x_arr);

    y_arr = PyArray_SimpleNew(itr->nd, itr->dimensions, NPY_FLOAT64);
    if (y_arr == NULL) goto fail;
    ys = (double *)PyArray_DATA((PyArrayObject *)y_arr);

    z_arr = PyArray_SimpleNew(itr->nd, itr->dimensions, NPY_FLOAT64);
    if (z_arr == NULL) goto fail;
    zs = (double *)PyArray_DATA((PyArrayObject *)z_arr);

    enum Scheme scheme;
    if (nest) {
        scheme = NEST;
    } else {
        scheme = RING;
    }

    int64_t *nside;
    int64_t *pix;
    int64_t last_nside = -1;
    bool started = false;
    vec3 vec;
    while (PyArray_MultiIter_NOTDONE(itr)) {
        nside = (int64_t *)PyArray_MultiIter_DATA(itr, 0);
        pix = (int64_t *)PyArray_MultiIter_DATA(itr, 1);

        if ((!started) || (*nside != last_nside)) {
            if (!hpgeom_check_nside(*nside, scheme, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            hpx = healpix_info_from_nside(*nside, scheme);
            started = true;
        }
        if (!hpgeom_check_pixel(&hpx, *pix, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        vec = pix2vec(&hpx, *pix);
        xs[itr->index] = vec.x;
        ys[itr->index] = vec.y;
        zs[itr->index] = vec.z;
        PyArray_MultiIter_NEXT(itr);
    }

    Py_DECREF(nside_arr);
    Py_DECREF(pix_arr);
    Py_DECREF(itr);

    PyObject *retval = PyTuple_New(3);
    PyTuple_SET_ITEM(retval, 0, PyArray_Return((PyArrayObject *)x_arr));
    PyTuple_SET_ITEM(retval, 1, PyArray_Return((PyArrayObject *)y_arr));
    PyTuple_SET_ITEM(retval, 2, PyArray_Return((PyArrayObject *)z_arr));

    return retval;

fail:
    Py_XDECREF(nside_arr);
    Py_XDECREF(x_arr);
    Py_XDECREF(y_arr);
    Py_XDECREF(z_arr);
    Py_XDECREF(pix_arr);
    Py_XDECREF(itr);

    return NULL;
}

PyDoc_STRVAR(neighbors_doc,
             "neighbors(nside, pix, nest=True)\n"
             "--\n\n"
             "Return 8 nearest neighbors for given pixels.\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR PIX_DOC_PAR NEST_DOC_PAR
             "\n"
             "Returns\n"
             "-------\n"
             "neighbor_pixels : `np.ndarray` (8,) or (N, 8)\n"
             "    Pixel numbers of the SW, W, NW, N, NE, E, SE, and S neighbors.\n"
             "    If a neighbor does not exist (as can be the case for W, N, E, and S)\n"
             "    the corresponding pixel number will be -1.\n"
             "\n"
             "Raises\n"
             "------\n"
             "ValueError\n"
             "    If pixel is out of range, or nside, pix arrays are not compatible.\n");

static PyObject *neighbors_meth(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    PyObject *nside_obj = NULL, *pix_obj = NULL;
    PyObject *nside_arr = NULL, *pix_arr = NULL;
    PyObject *neighbor_arr = NULL;
    PyArrayMultiIterObject *itr = NULL;
    int nest = 1;
    static char *kwlist[] = {"nside", "pix", "nest", NULL};

    int64_t *neighbor_pixels;
    healpix_info hpx;
    int status;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|p", kwlist, &nside_obj, &pix_obj,
                                     &nest))
        goto fail;

    nside_arr =
        PyArray_FROM_OTF(nside_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nside_arr == NULL) goto fail;
    pix_arr = PyArray_FROM_OTF(pix_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (pix_arr == NULL) goto fail;

    if (PyArray_NDIM((PyArrayObject *)pix_arr) > 1) {
        PyErr_SetString(PyExc_ValueError, "pix array must be at most 1D.");
        goto fail;
    }

    itr = (PyArrayMultiIterObject *)PyArray_MultiIterNew(2, nside_arr, pix_arr);
    if (itr == NULL) {
        PyErr_SetString(PyExc_ValueError,
                        "nside, pix arrays could not be broadcast together.");
        goto fail;
    }

    int ndims_pix = PyArray_NDIM((PyArrayObject *)pix_arr);
    if (ndims_pix == 0) {
        npy_intp dims[1];
        dims[0] = 8;
        neighbor_arr = PyArray_SimpleNew(1, dims, NPY_INT64);
    } else {
        npy_intp dims[2];
        dims[0] = PyArray_DIM((PyArrayObject *)pix_arr, 0);
        dims[1] = 8;
        neighbor_arr = PyArray_SimpleNew(2, dims, NPY_INT64);
    }
    if (neighbor_arr == NULL) goto fail;
    neighbor_pixels = (int64_t *)PyArray_DATA((PyArrayObject *)neighbor_arr);

    enum Scheme scheme;
    if (nest) {
        scheme = NEST;
    } else {
        scheme = RING;
    }

    i64stack *neigh = i64stack_new(8, &status, err);
    if (!status) {
        PyErr_SetString(PyExc_RuntimeError, err);
        goto fail;
    }
    i64stack_resize(neigh, 8, &status, err);
    if (!status) {
        PyErr_SetString(PyExc_RuntimeError, err);
        goto fail;
    }

    int64_t *nside;
    int64_t *pix;
    int64_t last_nside = -1;
    bool started = false;
    size_t index;
    while (PyArray_MultiIter_NOTDONE(itr)) {
        nside = (int64_t *)PyArray_MultiIter_DATA(itr, 0);
        pix = (int64_t *)PyArray_MultiIter_DATA(itr, 1);

        if ((!started) || (*nside != last_nside)) {
            if (!hpgeom_check_nside(*nside, scheme, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            hpx = healpix_info_from_nside(*nside, scheme);
            started = true;
        }

        if (!hpgeom_check_pixel(&hpx, *pix, err)) {
            PyErr_SetString(PyExc_ValueError, err);
            goto fail;
        }
        neighbors(&hpx, *pix, neigh, &status, err);

        for (size_t i = 0; i < neigh->size; i++) {
            index = neigh->size * itr->index + i;
            neighbor_pixels[index] = neigh->data[i];
        }

        PyArray_MultiIter_NEXT(itr);
    }

    Py_DECREF(nside_arr);
    Py_DECREF(pix_arr);
    Py_DECREF(itr);

    return PyArray_Return((PyArrayObject *)neighbor_arr);

fail:
    Py_XDECREF(nside_arr);
    Py_XDECREF(pix_arr);
    Py_XDECREF(neighbor_arr);
    Py_XDECREF(itr);

    return NULL;
}

PyDoc_STRVAR(max_pixel_radius_doc,
             "max_pixel_radius(nside, degrees=True)\n"
             "--\n\n"
             "Compute maximum angular distance between any pixel center and its corners.\n"
             "\n"
             "Parameters\n"
             "----------\n" NSIDE_DOC_PAR
             "degrees : `bool`, optional\n"
             "    If True, returns pixel radius in degrees, otherwise radians.\n"
             "\n"
             "Returns\n"
             "-------\n"
             "radii : `np.ndarray` (N, ) or `float`\n"
             "    Angular distance(s) (in degrees or radians).\n");

static PyObject *max_pixel_radius(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    PyObject *nside_obj = NULL;
    PyObject *nside_arr = NULL;
    PyObject *pixrad_arr = NULL;
    PyArrayMultiIterObject *itr = NULL;
    int degrees = 1;
    static char *kwlist[] = {"nside", "degrees", NULL};

    double *pixrads = NULL;
    healpix_info hpx;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|p", kwlist, &nside_obj, &degrees))
        goto fail;

    nside_arr =
        PyArray_FROM_OTF(nside_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nside_arr == NULL) goto fail;

    itr = (PyArrayMultiIterObject *)PyArray_MultiIterNew(1, nside_arr);
    if (itr == NULL) goto fail;

    pixrad_arr = PyArray_SimpleNew(itr->nd, itr->dimensions, NPY_FLOAT64);
    if (pixrad_arr == NULL) goto fail;
    pixrads = (double *)PyArray_DATA((PyArrayObject *)pixrad_arr);

    int64_t *nside;
    int64_t last_nside = -1;
    bool started = false;
    while (PyArray_MultiIter_NOTDONE(itr)) {
        nside = (int64_t *)PyArray_MultiIter_DATA(itr, 0);

        if ((!started) || (*nside != last_nside)) {
            if (!hpgeom_check_nside(*nside, RING, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            hpx = healpix_info_from_nside(*nside, RING);
            started = true;
        }
        pixrads[itr->index] = max_pixrad(&hpx);
        if (degrees) pixrads[itr->index] *= HPG_R2D;

        PyArray_MultiIter_NEXT(itr);
    }

    Py_DECREF(nside_arr);
    Py_DECREF(itr);

    return PyArray_Return((PyArrayObject *)pixrad_arr);

fail:
    Py_XDECREF(nside_arr);
    Py_XDECREF(pixrad_arr);
    Py_XDECREF(itr);

    return NULL;
}

PyDoc_STRVAR(
    get_interpolation_weights_doc,
    "get_interpolation_weights(nside, a, b, nest=True, lonlat=True, degrees=True)\n"
    "--\n\n"
    "Return the 4 closest pixels and weights to perform bilinear interpolation along\n"
    "latitude and longitude.\n"
    "\n"
    "Parameters\n"
    "----------\n" NSIDE_DOC_PAR AB_DOC_PAR NEST_DOC_PAR LONLAT_DOC_PAR DEGREES_DOC_PAR
    "\n"
    "Returns\n"
    "-------\n"
    "pixels : `np.ndarray` (N, 4)\n"
    "    Array of pixels (`np.int64`), each set of 4 can be used to do bilinear\n"
    "    interpolation of a map.\n"
    "weights : `np.ndarray` (N, 4)\n"
    "    Array of weiaghts (`np.float64`), each set of 4 corresponds with the pixels.\n");

static PyObject *get_interpolation_weights(PyObject *dummy, PyObject *args, PyObject *kwargs) {
    PyObject *nside_obj = NULL, *a_obj = NULL, *b_obj = NULL;
    PyObject *nside_arr = NULL, *a_arr = NULL, *b_arr = NULL;
    PyObject *pix_arr = NULL;
    PyObject *wgt_arr = NULL;
    PyArrayMultiIterObject *itr = NULL;
    int lonlat = 1;
    int nest = 1;
    int degrees = 1;
    static char *kwlist[] = {"nside", "a", "b", "lonlat", "nest", "degrees", NULL};

    int64_t *pixels = NULL;
    double *weights = NULL;
    healpix_info hpx;
    char err[ERR_SIZE];

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOO|ppp", kwlist, &nside_obj, &a_obj,
                                     &b_obj, &lonlat, &nest, &degrees))
        goto fail;

    nside_arr =
        PyArray_FROM_OTF(nside_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (nside_arr == NULL) goto fail;
    a_arr = PyArray_FROM_OTF(a_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (a_arr == NULL) goto fail;
    b_arr = PyArray_FROM_OTF(b_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (b_arr == NULL) goto fail;

    if (PyArray_NDIM((PyArrayObject *)a_arr) > 1) {
        PyErr_SetString(PyExc_ValueError, "a array must be at most 1D.");
        goto fail;
    }
    if (PyArray_NDIM((PyArrayObject *)a_arr) != PyArray_NDIM((PyArrayObject *)b_arr)) {
        PyErr_SetString(PyExc_ValueError,
                        "a and b arrays must have same number of dimensions.");
        goto fail;
    }

    itr = (PyArrayMultiIterObject *)PyArray_MultiIterNew(3, nside_arr, a_arr, b_arr);
    if (itr == NULL) {
        PyErr_SetString(PyExc_ValueError,
                        "nside, a, b arrays could not be broadcast together.");
        goto fail;
    }

    int ndims_pos = PyArray_NDIM((PyArrayObject *)a_arr);
    if (ndims_pos == 0) {
        npy_intp dims[1];
        dims[0] = 4;
        pix_arr = PyArray_SimpleNew(1, dims, NPY_INT64);
        if (pix_arr == NULL) goto fail;
        wgt_arr = PyArray_SimpleNew(1, dims, NPY_FLOAT64);
        if (wgt_arr == NULL) goto fail;
    } else {
        npy_intp dims[2];
        dims[0] = PyArray_DIM((PyArrayObject *)a_arr, 0);
        dims[1] = 4;
        pix_arr = PyArray_SimpleNew(2, dims, NPY_INT64);
        if (pix_arr == NULL) goto fail;
        wgt_arr = PyArray_SimpleNew(2, dims, NPY_FLOAT64);
        if (wgt_arr == NULL) goto fail;
    }
    pixels = (int64_t *)PyArray_DATA((PyArrayObject *)pix_arr);
    weights = (double *)PyArray_DATA((PyArrayObject *)wgt_arr);

    enum Scheme scheme;
    if (nest) {
        scheme = NEST;
    } else {
        scheme = RING;
    }

    int64_t *nside;
    double *a, *b;
    double theta, phi;
    int64_t last_nside = -1;
    bool started = false;
    while (PyArray_MultiIter_NOTDONE(itr)) {
        nside = (int64_t *)PyArray_MultiIter_DATA(itr, 0);
        a = (double *)PyArray_MultiIter_DATA(itr, 1);
        b = (double *)PyArray_MultiIter_DATA(itr, 2);

        if ((!started) || (*nside != last_nside)) {
            if (!hpgeom_check_nside(*nside, scheme, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            hpx = healpix_info_from_nside(*nside, scheme);
            started = true;
        }
        if (lonlat) {
            if (!hpgeom_lonlat_to_thetaphi(*a, *b, &theta, &phi, (bool)degrees, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
        } else {
            if (!hpgeom_check_theta_phi(*a, *b, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            theta = *a;
            phi = *b;
        }
        size_t index = 4 * itr->index;
        get_interpol(&hpx, theta, phi, &pixels[index], &weights[index]);
        PyArray_MultiIter_NEXT(itr);
    }

    Py_DECREF(nside_arr);
    Py_DECREF(a_arr);
    Py_DECREF(b_arr);
    Py_DECREF(itr);

    PyObject *retval = PyTuple_New(2);
    PyTuple_SET_ITEM(retval, 0, PyArray_Return((PyArrayObject *)pix_arr));
    PyTuple_SET_ITEM(retval, 1, PyArray_Return((PyArrayObject *)wgt_arr));

    return retval;

fail:
    Py_XDECREF(nside_arr);
    Py_XDECREF(a_arr);
    Py_XDECREF(b_arr);
    Py_XDECREF(pix_arr);
    Py_XDECREF(wgt_arr);
    Py_XDECREF(itr);

    return NULL;
}

struct Moc {
    int64_t nside;
    i64rangeset *rangeset;
};

struct HpgeomMoc {
    PyObject_HEAD

    struct Moc moc;
};

static int
HpgeomMoc_init(struct HpgeomMoc* self, PyObject *args, PyObject *kwargs)
{
    /* It takes ...
       nside_max ...
       ranges_or_nuniq array ...
       values (optional) ...

       ranges will be 2xN, nuniq will be 1D array.

       Then we need lookup by pixel function, that's the fundamental, but at high
       res, so really lookup by thing.

       Can we get nside_max from ranges?  Or nuniq ...
       ... turn each nuniq into a range ...
       ... insert ...
       ... if we know they're sorted can we do no check?  hmmm.
       ... do worry about stuffing scaling.
    */
    int64_t nside_max;
    PyObject *array_obj = NULL;
    PyObject *array_arr = NULL;
    static char *kwlist[] = {"nside_max", "array", NULL};
    NpyIter *iter = NULL;
    NpyIter_IterNextFunc *iternext;
    char** dataptr;
    char err[ERR_SIZE];
    int status = 1;

    // Ensure this is initialized to NULL
    self->moc.rangeset = NULL;

    if (!PyArg_ParseTuple(args, "LO", &nside_max, &array_obj))
        goto fail;

    array_arr = PyArray_FROM_OTF(array_obj, NPY_INT64, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (array_arr == NULL) goto fail;

    // Need to figure out values, optional, default to True.
    // That will be later.
    // Also need to allow an empty one to be done, so we can have insert function.

    // FIXME: check nside

    self->moc.nside = nside_max;
    self->moc.rangeset = i64rangeset_new(&status, err);
    if (!status) {
        PyErr_SetString(PyExc_ValueError, err);
        goto fail;
    }

    // And here we figure out which dimensionality and iterate.
    int ndims_array = PyArray_NDIM((PyArrayObject *)array_arr);
    if (ndims_array == 1) {
        // This is NUNIQ style.
    } else if (ndims_array == 2) {
        // This is RANGE style.
        npy_intp *dims = PyArray_DIMS((PyArrayObject *) array_arr);
        if (dims[1] != 2) {
            PyErr_SetString(PyExc_ValueError,
                            "The array dimensions must be (N, 2) for range style.");
            goto fail;
        }

        iter = NpyIter_New((PyArrayObject *)array_arr,
                           NPY_ITER_READONLY | NPY_ITER_MULTI_INDEX,
                           NPY_KEEPORDER,
                           NPY_NO_CASTING,
                           NULL);
        if (iter == NULL)
            goto fail;

        if (NpyIter_RemoveAxis(iter, 1) == NPY_FAIL)
            goto fail;

        iternext = NpyIter_GetIterNext(iter, NULL);
        if (iternext == NULL)
            goto fail;

        dataptr = NpyIter_GetDataPtrArray(iter);

        do {
            int64_t* data = (int64_t *) *dataptr;

            i64rangeset_append(self->moc.rangeset, *data, *(data + 1), &status, err);
            if (!status) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
        } while(iternext(iter));

    } else {
        // Illegal
        PyErr_SetString(PyExc_ValueError,
                        "The array dimensions must be 1D (NUNIQ) or 2D (range).");
        goto fail;
    }

    Py_DECREF(array_arr);
    if (iter != NULL)
        NpyIter_Deallocate(iter);
    return 0;
 fail:
    Py_XDECREF(array_arr);
    if (iter != NULL)
        NpyIter_Deallocate(iter);
    i64rangeset_delete(self->moc.rangeset);

    return -1;
}

static PyObject *HpgeomMoc_repr(struct HpgeomMoc* self) {
    char buff[2048];

    size_t nrange = self->moc.rangeset->stack->size / 2;

    snprintf(buff, sizeof(buff), "Moc(nside=%lld,\n[", self->moc.nside);

    if (nrange < 20) {
        for (size_t j = 0; j < nrange; j++) {
            snprintf(buff, sizeof(buff), "%s[%lld, %lld]\n", buff,
                     self->moc.rangeset->stack->data[j*2],
                     self->moc.rangeset->stack->data[j*2 + 1]);
        }
    } else {
        for (size_t j = 0; j < 10; j++) {
            snprintf(buff, sizeof(buff), "%s[%lld, %lld]\n", buff,
                     self->moc.rangeset->stack->data[j*2],
                     self->moc.rangeset->stack->data[j*2 + 1]);
        }
        for (size_t j = nrange - 10; j < nrange; j++) {
            snprintf(buff, sizeof(buff), "%s[%lld, %lld]\n", buff,
                     self->moc.rangeset->stack->data[j*2],
                     self->moc.rangeset->stack->data[j*2 + 1]);
        }
    }
    snprintf(buff, sizeof(buff), "%s])", buff);

    return PyUnicode_FromString((const char*)buff);
}

static PyObject *HpgeomMoc_contains_pos(struct HpgeomMoc* self, PyObject *args, PyObject *kwargs) {
    PyObject *a_obj = NULL, *b_obj = NULL;
    PyObject *a_arr = NULL, *b_arr = NULL;
    PyObject *contains_arr = NULL;
    int lonlat = 1;
    int degrees = 1;
    static char *kwlist[] = {"a", "b", "lonlat", "nest", "degrees", NULL};
    NpyIter *iter = NULL;
    NpyIter_IterNextFunc *iternext;
    PyObject *op[3], *ret = NULL;
    npy_uint32 op_flags[3];
    PyArray_Descr *op_dtypes[3];
    char **dataptrarray;
    double theta, phi;
    char err[ERR_SIZE];
    int status = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|pp", kwlist, &a_obj, &b_obj,
                                     &lonlat, &degrees))
        goto fail;

    a_arr = PyArray_FROM_OTF(a_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (a_arr == NULL) goto fail;
    b_arr = PyArray_FROM_OTF(b_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY | NPY_ARRAY_ENSUREARRAY);
    if (b_arr == NULL) goto fail;

    healpix_info hpx = healpix_info_from_nside(self->moc.nside, NEST);

    op[0] = a_arr;
    op[1] = b_arr;
    op[2] = NULL;
    op_flags[0] = op_flags[1] = NPY_ITER_READONLY;
    op_flags[2] = NPY_ITER_WRITEONLY | NPY_ITER_ALLOCATE;
    op_dtypes[0] = op_dtypes[1] = NULL;
    op_dtypes[2] = PyArray_DescrFromType(NPY_BOOL);

    iter = NpyIter_MultiNew(3, op, 0, NPY_KEEPORDER, NPY_NO_CASTING,
                            op_flags, op_dtypes);
    if (iter == NULL)
        goto fail;

    iternext = NpyIter_GetIterNext(iter, NULL);
    if (iternext == NULL)
        goto fail;
    dataptrarray = NpyIter_GetDataPtrArray(iter);

    double *a, *b;
    bool *out;
    int64_t pixel;
    ptrdiff_t range_index;

    size_t max_index = self->moc.rangeset->stack->size - 2;

    do {
        a = (double *)dataptrarray[0];
        b = (double *)dataptrarray[1];

        if (lonlat) {
            if (!hpgeom_lonlat_to_thetaphi(*a, *b, &theta, &phi, (bool)degrees, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
        } else {
            if (!hpgeom_check_theta_phi(*a, *b, err)) {
                PyErr_SetString(PyExc_ValueError, err);
                goto fail;
            }
            theta = *a;
            phi = *b;
        }
        pixel = ang2pix(&hpx, theta, phi);
        range_index = iiv(self->moc.rangeset, pixel);

        out = (bool *)dataptrarray[2];

        if ((range_index < 0) || (range_index > max_index) || ((range_index % 2) == 1)) {
            *out = 0;
        } else {
            *out = 1;
        }

    } while (iternext(iter));

    ret = NpyIter_GetOperandArray(iter)[2];
    Py_INCREF(ret);

    if (NpyIter_Deallocate(iter) != NPY_SUCCEED)
        goto fail;
    Py_DECREF(a_arr);
    Py_DECREF(b_arr);

    return ret;

 fail:
    Py_XDECREF(a_arr);
    Py_XDECREF(b_arr);
    Py_XDECREF(contains_arr);
    if (iter != NULL)
        NpyIter_Deallocate(iter);
    Py_XDECREF(ret);

    return NULL;
}


// define methods on the object ... there is basically lookup functions.
// And append, etc.
// Lookup by pixel or by position.

static void
HpgeomMoc_dealloc(struct HpgeomMoc* self)
{
    i64rangeset_delete(self->moc.rangeset);

    Py_TYPE(self)->tp_free((PyObject*)self);
};

static PyMethodDef HpgeomMoc_methods[] = {
    // This defines the methods like below.
    // And says which c functions.
    {"contains_pos", (PyCFunction)(void (*)(void))HpgeomMoc_contains_pos,
     METH_VARARGS | METH_KEYWORDS, NULL},
    {NULL, NULL, 0, NULL}};


static PyTypeObject HpgeomMocType = {
    PyVarObject_HEAD_INIT(NULL, 0)

    "_hpgeom.Moc",                 /* tp_name */
    sizeof(struct HpgeomMoc),      /* tp_basicsize */
    0,                                 /* tp_itemsize */
    (destructor)HpgeomMoc_dealloc, /* tp_dealloc */
    0,                                 /* tp_print */
    0,                                 /* tp_getattr */
    0,                                 /* tp_setattr */
    0,                                 /* tp_compare */
    //0,                                 /* tp_repr */
    (reprfunc)HpgeomMoc_repr,    /* tp_repr */
    0,                                 /* tp_as_number */
    0,                                 /* tp_as_sequence */
    0,                                 /* tp_as_mapping */
    0,                                 /* tp_hash */
    0,                                 /* tp_call */
    0,                                 /* tp_str */
    0,                                 /* tp_getattro */
    0,                                 /* tp_setattro */
    0,                                 /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
    "Something\n",
    0,                                 /* tp_traverse */
    0,                                 /* tp_clear */
    0,                                 /* tp_richcompare */
    0,                                 /* tp_weaklistoffset */
    0,                                 /* tp_iter */
    0,                                 /* tp_iternext */
    HpgeomMoc_methods,             /* tp_methods */
    0,                                 /* tp_members */
    0,                                 /* tp_getset */
    0,                                 /* tp_base */
    0,                                 /* tp_dict */
    0,                                 /* tp_descr_get */
    0,                                 /* tp_descr_set */
    0,                                 /* tp_dictoffset */
    (initproc)HpgeomMoc_init,      /* tp_init */
    0,                                 /* tp_alloc */
    PyType_GenericNew,                 /* tp_new */
};

static PyMethodDef hpgeom_methods[] = {
    {"angle_to_pixel", (PyCFunction)(void (*)(void))angle_to_pixel,
     METH_VARARGS | METH_KEYWORDS, angle_to_pixel_doc},
    {"pixel_to_angle", (PyCFunction)(void (*)(void))pixel_to_angle,
     METH_VARARGS | METH_KEYWORDS, pixel_to_angle_doc},
    {"query_circle", (PyCFunction)(void (*)(void))query_circle, METH_VARARGS | METH_KEYWORDS,
     query_circle_doc},
    {"query_polygon", (PyCFunction)(void (*)(void))query_polygon_meth,
     METH_VARARGS | METH_KEYWORDS, query_polygon_doc},
    {"query_ellipse", (PyCFunction)(void (*)(void))query_ellipse_meth,
     METH_VARARGS | METH_KEYWORDS, query_ellipse_doc},
    {"query_box", (PyCFunction)(void (*)(void))query_box_meth, METH_VARARGS | METH_KEYWORDS,
     query_box_doc},
    {"nest_to_ring", (PyCFunction)(void (*)(void))nest_to_ring, METH_VARARGS | METH_KEYWORDS,
     nest_to_ring_doc},
    {"ring_to_nest", (PyCFunction)(void (*)(void))ring_to_nest, METH_VARARGS | METH_KEYWORDS,
     ring_to_nest_doc},
    {"boundaries", (PyCFunction)(void (*)(void))boundaries_meth, METH_VARARGS | METH_KEYWORDS,
     boundaries_doc},
    {"vector_to_pixel", (PyCFunction)(void (*)(void))vector_to_pixel,
     METH_VARARGS | METH_KEYWORDS, vector_to_pixel_doc},
    {"pixel_to_vector", (PyCFunction)(void (*)(void))pixel_to_vector,
     METH_VARARGS | METH_KEYWORDS, pixel_to_vector_doc},
    {"neighbors", (PyCFunction)(void (*)(void))neighbors_meth, METH_VARARGS | METH_KEYWORDS,
     neighbors_doc},
    {"max_pixel_radius", (PyCFunction)(void (*)(void))max_pixel_radius,
     METH_VARARGS | METH_KEYWORDS, max_pixel_radius_doc},
    {"get_interpolation_weights", (PyCFunction)(void (*)(void))get_interpolation_weights,
     METH_VARARGS | METH_KEYWORDS, get_interpolation_weights_doc},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef hpgeom_module = {PyModuleDef_HEAD_INIT, "_hpgeom", NULL, -1,
                                           hpgeom_methods};

PyMODINIT_FUNC PyInit__hpgeom(void) {
    PyObject* m;

    import_array();

    HpgeomMocType.tp_new = PyType_GenericNew;

    if (PyType_Ready(&HpgeomMocType) < 0) {
        return NULL;
    }

    m = PyModule_Create(&hpgeom_module);
    if (m == NULL) {
        return NULL;
    }

    Py_INCREF(&HpgeomMocType);
    PyModule_AddObject(m, "Moc", (PyObject *)&HpgeomMocType);

    return m;
}
