# -*- coding: utf-8 -*-
"""SGP4 en Python puro (sin NumPy ni la extension C).

Copia vendorizada de python-sgp4 (Brandon Rhodes), licencia MIT.
Ver LICENSE y ORIGEN.txt en esta carpeta. No forma parte del codigo propio
del rotor; no lleva la cabecera de copyright del proyecto.

Uso:
    from .sgp4 import Satrec, WGS72, jday
    sat = Satrec.twoline2rv(line1, line2)
    jd, fr = jday(2026, 11, 28, 9, 47, 0.0)
    err, r, v = sat.sgp4(jd, fr)   # r, v en km y km/s (marco TEME)
"""
from .model import Satrec, WGS72, WGS72OLD, WGS84
from .functions import jday
from .ext import invjday

__all__ = ["Satrec", "WGS72", "WGS72OLD", "WGS84", "jday", "invjday"]
__version__ = "2.27-vendored"
