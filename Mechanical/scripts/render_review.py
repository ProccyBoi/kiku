"""Generate lightweight review renders from the exported Kiku STEP files."""

from pathlib import Path

import cadquery as cq
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection


ROOT = Path(__file__).resolve().parents[2]
MECH = ROOT / "Mechanical"
EXP = MECH / "exports"
REF = MECH / "reference"
REV = MECH / "review"
REV.mkdir(parents=True, exist_ok=True)


def load(name: str):
    return cq.importers.importStep(str(name)).val()


def add_shape(ax, shape, colour, alpha=1.0, shift=(0.0, 0.0, 0.0), tol=0.45):
    verts, tris = shape.tessellate(tol)
    sx, sy, sz = shift
    xyz = [(v.x + sx, v.y + sy, v.z + sz) for v in verts]
    faces = [[xyz[i] for i in tri] for tri in tris]
    poly = Poly3DCollection(faces, facecolor=colour, edgecolor="none", alpha=alpha)
    ax.add_collection3d(poly)


def setup(ax, elev, azim, zlim=(-14, 22)):
    ax.set_xlim(-8, 68)
    ax.set_ylim(-8, 114)
    ax.set_zlim(*zlim)
    ax.set_box_aspect((76, 122, zlim[1] - zlim[0]))
    ax.view_init(elev=elev, azim=azim)
    ax.set_axis_off()


def save_front(parts):
    fig = plt.figure(figsize=(9, 12), dpi=180)
    ax = fig.add_subplot(111, projection="3d")
    add_shape(ax, parts["rear"], "#9aa18f", 1.0)
    add_shape(ax, parts["front"], "#ded8ca", 1.0)
    add_shape(ax, parts["lens"], "#24282b", 0.72)
    add_shape(ax, parts["b1"], "#343434", 1.0)
    add_shape(ax, parts["b2"], "#343434", 1.0)
    add_shape(ax, parts["knob"], "#343434", 1.0)
    setup(ax, elev=26, azim=-63)
    fig.tight_layout(pad=0)
    fig.savefig(REV / "kiku_housing_v8_front_3q.png", bbox_inches="tight", pad_inches=0.05)
    plt.close(fig)


def save_rear(parts):
    fig = plt.figure(figsize=(9, 12), dpi=180)
    ax = fig.add_subplot(111, projection="3d")
    add_shape(ax, parts["rear"], "#9aa18f", 1.0)
    add_shape(ax, parts["stand"], "#343434", 1.0)
    add_shape(ax, parts["front"], "#ded8ca", 0.22)
    setup(ax, elev=-22, azim=116, zlim=(-16, 20))
    fig.tight_layout(pad=0)
    fig.savefig(REV / "kiku_housing_v8_rear_3q.png", bbox_inches="tight", pad_inches=0.05)
    plt.close(fig)


def save_exploded(parts):
    fig = plt.figure(figsize=(10, 13), dpi=180)
    ax = fig.add_subplot(111, projection="3d")
    add_shape(ax, parts["rear"], "#9aa18f", 0.92, shift=(0, 0, -12))
    add_shape(ax, parts["stand"], "#343434", 1.0, shift=(0, 0, -18))
    add_shape(ax, parts["pcb"], "#245c45", 1.0, shift=(0, 0, 0), tol=0.65)
    add_shape(ax, parts["front"], "#ded8ca", 0.85, shift=(0, 0, 14))
    add_shape(ax, parts["lens"], "#24282b", 0.60, shift=(0, 0, 20))
    add_shape(ax, parts["b1"], "#343434", 1.0, shift=(0, 0, 20))
    add_shape(ax, parts["b2"], "#343434", 1.0, shift=(0, 0, 20))
    add_shape(ax, parts["knob"], "#343434", 1.0, shift=(0, 0, 20))
    setup(ax, elev=24, azim=-60, zlim=(-28, 44))
    fig.tight_layout(pad=0)
    fig.savefig(REV / "kiku_housing_v8_exploded.png", bbox_inches="tight", pad_inches=0.05)
    plt.close(fig)


def save_stand_open(parts):
    fig = plt.figure(figsize=(9, 12), dpi=180)
    ax = fig.add_subplot(111, projection="3d")
    stand_open = parts["stand"].rotate((0, 87.0, -11.6), (1, 87.0, -11.6), 62.0)
    add_shape(ax, parts["rear"], "#9aa18f", 1.0)
    add_shape(ax, parts["front"], "#ded8ca", 0.65)
    add_shape(ax, stand_open, "#343434", 1.0)
    setup(ax, elev=-14, azim=126, zlim=(-68, 20))
    fig.tight_layout(pad=0)
    fig.savefig(REV / "kiku_housing_v8_stand_open.png", bbox_inches="tight", pad_inches=0.05)
    plt.close(fig)


def save_controls_closeup(parts):
    """Front control close-up for checking the V8 blobject control language."""
    fig = plt.figure(figsize=(10, 8), dpi=190)
    ax = fig.add_subplot(111, projection="3d")
    add_shape(ax, parts["front"], "#ded8ca", 1.0, tol=0.28)
    add_shape(ax, parts["b1"], "#343434", 1.0, tol=0.18)
    add_shape(ax, parts["b2"], "#343434", 1.0, tol=0.18)
    add_shape(ax, parts["knob"], "#343434", 1.0, tol=0.18)
    ax.set_xlim(4, 56)
    ax.set_ylim(25, 69)
    ax.set_zlim(12, 24)
    ax.set_box_aspect((52, 44, 12))
    ax.view_init(elev=42, azim=-72)
    ax.set_axis_off()
    fig.tight_layout(pad=0)
    fig.savefig(REV / "kiku_housing_v8_controls_closeup.png", bbox_inches="tight", pad_inches=0.05)
    plt.close(fig)


def main():
    parts = {
        "front": load(EXP / "kiku_front_shell_external_speaker_v8.step"),
        "rear": load(EXP / "kiku_rear_shell_smooth_stand_v8.step"),
        "stand": load(EXP / "kiku_kickstand_v8.step"),
        "lens": load(EXP / "kiku_screen_lens_v8.step"),
        "b1": load(EXP / "kiku_button_left_v8.step"),
        "b2": load(EXP / "kiku_button_right_v8.step"),
        "knob": load(EXP / "kiku_encoder_knob_v8.step"),
        "pcb": load(REF / "pcb_populated.step"),
    }
    save_front(parts)
    save_rear(parts)
    save_exploded(parts)
    save_stand_open(parts)
    save_controls_closeup(parts)
    print("Review renders written to Mechanical/review")


if __name__ == "__main__":
    main()
