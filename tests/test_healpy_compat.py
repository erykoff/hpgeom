import numpy as np
import pytest

try:
    import healpy as hp
    has_healpy = True
except ImportError:
    has_healpy = False

import hpgeom
import hpgeom.healpy_compat as hpc


@pytest.mark.skipif(not has_healpy, reason="Skipping test without healpy")
def test_ang2pix():
    """Test hpgeom.healpy_compat.ang2pix."""
    np.random.seed(12345)

    nside = 2048

    lon = np.random.uniform(low=0.0, high=360.0, size=1_000_000)
    lat = np.random.uniform(low=-90.0, high=90.0, size=1_000_000)
    theta, phi = hpgeom.lonlat_to_thetaphi(lon, lat)

    pix_hpcompat = hpc.ang2pix(nside, theta, phi)
    pix_healpy = hp.ang2pix(nside, theta, phi)
    np.testing.assert_array_equal(pix_hpcompat, pix_healpy)

    pix_hpcompat = hpc.ang2pix(nside, theta, phi, nest=True)
    pix_healpy = hp.ang2pix(nside, theta, phi, nest=True)
    np.testing.assert_array_equal(pix_hpcompat, pix_healpy)

    pix_hpcompat = hpc.ang2pix(nside, lon, lat, lonlat=True)
    pix_healpy = hpc.ang2pix(nside, lon, lat, lonlat=True)
    np.testing.assert_array_equal(pix_hpcompat, pix_healpy)


def test_pix2ang():
    """Test hpgeom.healpy_compat.pix2ang."""
    np.random.seed(12345)

    nside = 2048

    pix = np.random.randint(low=0, high=12*nside*nside-1, size=1_000_000)

    theta_hpcompat, phi_hpcompat = hpc.pix2ang(nside, pix)
    theta_healpy, phi_healpy = hp.pix2ang(nside, pix)
    np.testing.assert_array_almost_equal(theta_hpcompat, theta_healpy)
    np.testing.assert_array_almost_equal(phi_hpcompat, phi_healpy)

    theta_hpcompat, phi_hpcompat = hpc.pix2ang(nside, pix, nest=True)
    theta_healpy, phi_healpy = hp.pix2ang(nside, pix, nest=True)
    np.testing.assert_array_almost_equal(theta_hpcompat, theta_healpy)
    np.testing.assert_array_almost_equal(phi_hpcompat, phi_healpy)

    lon_hpcompat, lat_hpcompat = hpc.pix2ang(nside, pix, lonlat=True)
    lon_healpy, lat_healpy = hp.pix2ang(nside, pix, lonlat=True)
    np.testing.assert_array_almost_equal(lon_hpcompat, lon_healpy)
    np.testing.assert_array_almost_equal(lat_hpcompat, lat_healpy)


def test_query_disc():
    """Test hpgeom.healpy_compat.query_disc."""
    np.random.seed(12345)

    nside = 2048
    lon = 10.0
    lat = 20.0
    radius = 0.5

    theta, phi = hpgeom.lonlat_to_thetaphi(lon, lat)
    sintheta = np.sin(theta)
    vec = [sintheta*np.cos(phi), sintheta*np.sin(phi), np.cos(theta)]

    pix_hpcompat = hpc.query_disc(nside, vec, radius)
    pix_healpy = hp.query_disc(nside, vec, radius)
    np.testing.assert_array_equal(pix_hpcompat, pix_healpy)

    pix_hpcompat = hpc.query_disc(nside, vec, radius, nest=False)
    pix_healpy = hp.query_disc(nside, vec, radius, nest=False)
    np.testing.assert_array_equal(pix_hpcompat, pix_healpy)


def test_ring2nest():
    """Test hpgeom.healpy_compat.ring2nest."""
    pix_hpcompat = hpc.ring2nest(2048, 1000)
    pix_healpy = hp.ring2nest(2048, 1000)

    assert(pix_hpcompat == pix_healpy)

    pix_hpcompat = hpc.ring2nest(2048, np.arange(100))
    pix_healpy = hp.ring2nest(2048, np.arange(100))

    np.testing.assert_array_equal(pix_hpcompat, pix_healpy)


def test_nest2ring():
    """Test hpgeom.healpy_compat.nest2ring."""
    pix_hpcompat = hpc.nest2ring(2048, 1000)
    pix_healpy = hp.nest2ring(2048, 1000)

    assert(pix_hpcompat == pix_healpy)

    pix_hpcompat = hpc.nest2ring(2048, np.arange(100))
    pix_healpy = hp.nest2ring(2048, np.arange(100))

    np.testing.assert_array_equal(pix_hpcompat, pix_healpy)


def test_nside2npix():
    """Test hpgeom.healpy_compat.nside2npix."""
    npix_hpcompat = hpc.nside2npix(2048)
    npix_healpy = hp.nside2npix(2048)

    assert(npix_hpcompat == npix_healpy)


def test_npix2nside():
    """Test hpgeom.healpy_compat.npix2nside."""
    nside_hpcompat = hpc.nside2npix(12*2048*2048)
    nside_healpy = hp.nside2npix(12*2048*2048)

    assert(nside_hpcompat == nside_healpy)

    with pytest.raises(ValueError):
        hpc.npix2nside(100)

    with pytest.raises(ValueError):
        hpc.npix2nside(100)