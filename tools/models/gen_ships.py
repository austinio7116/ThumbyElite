#!/usr/bin/env python3
"""
ThumbyElite — ship OBJ generator.

Ships are assembled from primitives (warped boxes / wedges / pyramids)
whose face templates are CCW-from-outside by construction, so windings
are always right no matter how the corner points are warped (as long as
the warp doesn't invert the topology). Output: .obj + ships.mtl in this
directory — run manually, commit the results; obj2mesh bakes them at
build time.

Model space: x right, y up, z FORWARD (nose +z). Units: meters.

  python3 gen_ships.py
"""
import os

OUT = os.path.dirname(os.path.abspath(__file__))

MATERIALS = {
    "hull":   (0.62, 0.63, 0.66),
    "hull2":  (0.50, 0.51, 0.56),
    "wing":   (0.40, 0.41, 0.47),
    "glass":  (0.13, 0.42, 0.55),
    "engine": (0.95, 0.45, 0.10),
    "accent": (0.66, 0.14, 0.12),
    "cargo":  (0.52, 0.44, 0.30),
    "dark":   (0.25, 0.26, 0.30),
}


class Builder:
    def __init__(self):
        self.verts = []
        self.faces = []   # (indices_tuple, mtl)

    def vert(self, p):
        self.verts.append(p)
        return len(self.verts)  # 1-based

    def quad(self, a, b, c, d, mtl):
        self.faces.append(((a, b, c, d), mtl))

    def tri(self, a, b, c, mtl):
        self.faces.append(((a, b, c), mtl))

    def box(self, corners, mtl, mtl_overrides=None):
        """corners: 8 points ordered
             0:(-x,-y,-z) 1:(+x,-y,-z) 2:(+x,+y,-z) 3:(-x,+y,-z)
             4:(-x,-y,+z) 5:(+x,-y,+z) 6:(+x,+y,+z) 7:(-x,+y,+z)
           mtl_overrides: dict face-name -> mtl, faces: front(+z) back(-z)
           right(+x) left(-x) top(+y) bottom(-y)."""
        i = [self.vert(p) for p in corners]
        ov = mtl_overrides or {}
        self.quad(i[4], i[5], i[6], i[7], ov.get("front", mtl))
        self.quad(i[1], i[0], i[3], i[2], ov.get("back", mtl))
        self.quad(i[5], i[1], i[2], i[6], ov.get("right", mtl))
        self.quad(i[0], i[4], i[7], i[3], ov.get("left", mtl))
        self.quad(i[7], i[6], i[2], i[3], ov.get("top", mtl))
        self.quad(i[0], i[1], i[5], i[4], ov.get("bottom", mtl))

    def tapered_box(self, zb, zf, xb, yb0, yb1, xf, yf0, yf1, mtl,
                    mtl_overrides=None, dx=0.0, dy=0.0):
        """Box from back quad (z=zb, x±xb, y yb0..yb1) to front quad
           (z=zf, x±xf+dx, y yf0..yf1 shifted dy)."""
        self.box([(-xb, yb0, zb), (xb, yb0, zb), (xb, yb1, zb), (-xb, yb1, zb),
                  (-xf + dx, yf0 + dy, zf), (xf + dx, yf0 + dy, zf),
                  (xf + dx, yf1 + dy, zf), (-xf + dx, yf1 + dy, zf)],
                 mtl, mtl_overrides)

    def pyramid(self, base4, apex, mtl, base_mtl=None):
        """base4: quad ordered CCW seen from OUTSIDE the base side (i.e.
           looking at the base from opposite the apex)."""
        b = [self.vert(p) for p in base4]
        a = self.vert(apex)
        self.quad(b[0], b[1], b[2], b[3], base_mtl or mtl)
        # Sides: base edge reversed relative to base winding + apex.
        self.tri(b[1], b[0], a, mtl)
        self.tri(b[2], b[1], a, mtl)
        self.tri(b[3], b[2], a, mtl)
        self.tri(b[0], b[3], a, mtl)

    def wedge_x(self, root4, tip2, mtl):
        """Wing-like slab: root quad -> tip edge (extruded toward ±x).
           root4 ordered 0:(z-,y-) 1:(z+,y-) 2:(z+,y+) 3:(z-,y+) — i.e.
           CCW seen from the TIP side. tip2: (z-,*) then (z+,*).
           Emits: root cap, top, bottom, leading(z+), trailing(z-)."""
        r = [self.vert(p) for p in root4]
        t = [self.vert(p) for p in tip2]
        tip_is_right = tip2[0][0] > root4[0][0]
        if tip_is_right:
            self.quad(r[0], r[1], r[2], r[3], mtl)          # root cap (-x out)
            self.quad(r[3], r[2], t[1], t[0], mtl)          # top
            self.quad(r[1], r[0], t[0], t[1], mtl)          # bottom
            self.tri(r[2], r[1], t[1], mtl)                 # leading edge (z+)
            self.tri(r[0], r[3], t[0], mtl)                 # trailing edge (z-)
        else:
            self.quad(r[3], r[2], r[1], r[0], mtl)          # root cap (+x out)
            self.quad(r[2], r[3], t[0], t[1], mtl)          # top
            self.quad(r[0], r[1], t[1], t[0], mtl)          # bottom
            self.tri(r[1], r[2], t[1], mtl)                 # leading
            self.tri(r[3], r[0], t[0], mtl)                 # trailing
        return None

    def wedge_y(self, root4, tip2, mtl):
        """Vertical fin: root quad in a y-plane -> tip edge above.
           root4 ordered 0:(x-,z-) 1:(x+,z-) 2:(x+,z+) 3:(x-,z+).
           tip2: (z-,*) then (z+,*) at the tip."""
        r = [self.vert(p) for p in root4]
        t = [self.vert(p) for p in tip2]
        self.quad(r[0], r[1], r[2], r[3], mtl)              # root cap (down/out)
        self.quad(t[0], t[1], r[2], r[1], mtl)              # right side (+x)
        self.quad(t[1], t[0], r[0], r[3], mtl)              # left side (-x)
        self.tri(r[3], r[2], t[1], mtl)                     # leading (z+)
        self.tri(r[1], r[0], t[0], mtl)                     # trailing (z-)

    def write(self, path, name):
        with open(path, "w") as f:
            f.write(f"# generated by gen_ships.py — {name}\n")
            f.write("mtllib ships.mtl\n")
            for v in self.verts:
                f.write(f"v {v[0]:.4f} {v[1]:.4f} {v[2]:.4f}\n")
            cur = None
            for idx, mtl in self.faces:
                if mtl != cur:
                    f.write(f"usemtl {mtl}\n")
                    cur = mtl
                f.write("f " + " ".join(str(i) for i in idx) + "\n")
        ntris = sum(len(i) - 2 for i, _ in self.faces)
        print(f"{name}: {len(self.verts)} verts, {ntris} tris -> {path}")


def write_mtl():
    path = os.path.join(OUT, "ships.mtl")
    with open(path, "w") as f:
        for name, (r, g, b) in MATERIALS.items():
            f.write(f"newmtl {name}\nKd {r} {g} {b}\n\n")
    print(f"materials -> {path}")


def fighter():
    """'Sparrow' light fighter — dart fuselage, swept wings, twin-look
    engine block. ~12m long, 9m span."""
    b = Builder()
    # Fuselage: rear quad -> front quad, then nose pyramid.
    b.tapered_box(-5.0, 3.0, 0.9, -0.55, 0.75, 0.55, -0.40, 0.45, "hull",
                  {"back": "dark"})
    # Base wound CCW seen from BEHIND (outward -z, away from the apex).
    b.pyramid([(-0.55, -0.40, 3.0), (-0.55, 0.45, 3.0),
               (0.55, 0.45, 3.0), (0.55, -0.40, 3.0)],
              (0.0, -0.02, 6.0), "hull2", base_mtl="hull2")
    # Canopy: tapered box on top, narrowing upward and forward.
    b.tapered_box(0.4, 2.6, 0.40, 0.45, 0.95, 0.26, 0.42, 0.62, "glass",
                  {"back": "hull2"})
    # Wings: swept, thin, slight dihedral.
    b.wedge_x([(-0.85, -0.30, -4.6), (-0.85, -0.30, -1.0),
               (-0.85, -0.12, -1.0), (-0.85, -0.12, -4.6)],
              [(-4.6, -0.02, -4.9), (-4.6, -0.02, -3.4)], "wing")
    b.wedge_x([(0.85, -0.30, -4.6), (0.85, -0.30, -1.0),
               (0.85, -0.12, -1.0), (0.85, -0.12, -4.6)],
              [(4.6, -0.02, -4.9), (4.6, -0.02, -3.4)], "wing")
    # Tail fin.
    b.wedge_y([(-0.07, 0.75, -4.9), (0.07, 0.75, -4.9),
               (0.07, 0.75, -3.2), (-0.07, 0.75, -3.2)],
              [(0.0, 2.0, -5.0), (0.0, 2.0, -4.2)], "wing")
    # Engine block: slightly inset, glowing rear.
    b.tapered_box(-5.9, -5.0, 0.75, -0.45, 0.60, 0.85, -0.52, 0.70, "hull2",
                  {"back": "engine"})
    b.write(os.path.join(OUT, "fighter.obj"), "fighter")


def viper():
    """'Viper' police interceptor — flat Elite-style wedge, twin fins."""
    b = Builder()
    # Main wedge: wide flat rear -> narrow nose edge (use tapered_box with
    # a thin front face = effectively a wedge with a blunt tip).
    b.tapered_box(-4.0, 4.5, 2.4, -0.55, 0.65, 0.35, -0.10, 0.08, "hull",
                  {"back": "dark", "top": "hull", "bottom": "hull2"})
    # Cockpit stripe: small box on the upper slope.
    b.tapered_box(-1.2, 1.4, 0.7, 0.62, 0.92, 0.4, 0.20, 0.38, "glass",
                  {"back": "hull2"})
    # Twin canted fins at the rear corners.
    b.wedge_y([(-2.1, 0.6, -3.9), (-1.7, 0.6, -3.9),
               (-1.7, 0.6, -2.6), (-2.1, 0.6, -2.6)],
              [(-2.25, 1.7, -3.95), (-2.25, 1.7, -3.3)], "accent")
    b.wedge_y([(1.7, 0.6, -3.9), (2.1, 0.6, -3.9),
               (2.1, 0.6, -2.6), (1.7, 0.6, -2.6)],
              [(2.25, 1.7, -3.95), (2.25, 1.7, -3.3)], "accent")
    # Engine plate.
    b.tapered_box(-4.5, -4.0, 1.9, -0.42, 0.5, 2.1, -0.50, 0.58, "hull2",
                  {"back": "engine"})
    b.write(os.path.join(OUT, "viper.obj"), "viper")


def freighter():
    """'Mule' light freighter — boxy hull, cab, side cargo pods."""
    b = Builder()
    # Main hull.
    b.tapered_box(-6.0, 4.0, 1.7, -1.1, 1.3, 1.5, -0.95, 1.05, "hull",
                  {"back": "dark"})
    # Nose taper.
    b.tapered_box(4.0, 6.2, 1.5, -0.95, 1.05, 0.9, -0.45, 0.35, "hull2")
    # Cab on top front.
    b.tapered_box(2.2, 4.6, 0.9, 1.05, 1.85, 0.7, 1.0, 1.45, "hull2",
                  {"front": "glass", "back": "hull"})
    # Cargo pods (left/right), slightly proud of the hull.
    b.box([(-2.6, -0.8, -5.2), (-1.6, -0.8, -5.2), (-1.6, 0.7, -5.2),
           (-2.6, 0.7, -5.2), (-2.6, -0.8, 2.4), (-1.6, -0.8, 2.4),
           (-1.6, 0.7, 2.4), (-2.6, 0.7, 2.4)], "cargo", {"back": "dark"})
    b.box([(1.6, -0.8, -5.2), (2.6, -0.8, -5.2), (2.6, 0.7, -5.2),
           (1.6, 0.7, -5.2), (1.6, -0.8, 2.4), (2.6, -0.8, 2.4),
           (2.6, 0.7, 2.4), (1.6, 0.7, 2.4)], "cargo", {"back": "dark"})
    # Engine block.
    b.tapered_box(-7.0, -6.0, 1.3, -0.85, 1.0, 1.5, -1.0, 1.2, "dark",
                  {"back": "engine"})
    # Dorsal antenna fin.
    b.wedge_y([(-0.06, 1.3, -3.5), (0.06, 1.3, -3.5),
               (0.06, 1.3, -2.6), (-0.06, 1.3, -2.6)],
              [(0.0, 2.3, -3.45), (0.0, 2.3, -3.1)], "accent")
    b.write(os.path.join(OUT, "freighter.obj"), "freighter")


def station():
    """Orbital outpost — hub cube with a glowing docking face, two arms
    carrying solar panels. ~60 tris, ~90m across."""
    b = Builder()
    # Hub with docking bay on +z.
    b.tapered_box(-14, 14, 14, -14, 14, 14, -14, 14, "hull",
                  {"front": "glass", "back": "dark"})
    # Arms.
    b.box([(-26, -2.5, -2.5), (-14, -2.5, -2.5), (-14, 2.5, -2.5),
           (-26, 2.5, -2.5), (-26, -2.5, 2.5), (-14, -2.5, 2.5),
           (-14, 2.5, 2.5), (-26, 2.5, 2.5)], "hull2")
    b.box([(14, -2.5, -2.5), (26, -2.5, -2.5), (26, 2.5, -2.5),
           (14, 2.5, -2.5), (14, -2.5, 2.5), (26, -2.5, 2.5),
           (26, 2.5, 2.5), (14, 2.5, 2.5)], "hull2")
    # Solar panels (thin, broad).
    b.box([(-44, -0.8, -12), (-26, -0.8, -12), (-26, 0.8, -12),
           (-44, 0.8, -12), (-44, -0.8, 12), (-26, -0.8, 12),
           (-26, 0.8, 12), (-44, 0.8, 12)], "glass")
    b.box([(26, -0.8, -12), (44, -0.8, -12), (44, 0.8, -12),
           (26, 0.8, -12), (26, -0.8, 12), (44, -0.8, 12),
           (44, 0.8, 12), (26, 0.8, 12)], "glass")
    # Beacon mast.
    b.wedge_y([(-1, 14, -1), (1, 14, -1), (1, 14, 1), (-1, 14, 1)],
              [(0, 22, -0.5), (0, 22, 0.5)], "accent")
    b.write(os.path.join(OUT, "station.obj"), "station")


def beacon():
    """Nav beacon — small octahedron, accent-coloured. ~12 tris, 12m."""
    b = Builder()
    base = [(-3, 0, -3), (3, 0, -3), (3, 0, 3), (-3, 0, 3)]   # outward -y
    b.pyramid(base, (0, 6, 0), "accent")
    base2 = [(-3, 0, -3), (-3, 0, 3), (3, 0, 3), (3, 0, -3)]  # outward +y
    b.pyramid(base2, (0, -6, 0), "hull2")
    b.write(os.path.join(OUT, "beacon.obj"), "beacon")


if __name__ == "__main__":
    write_mtl()
    fighter()
    viper()
    freighter()
    station()
    beacon()
