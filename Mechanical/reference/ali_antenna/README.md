# FM antenna mechanical reference

Source products supplied by the project owner:

- Telescopic antenna: `https://www.aliexpress.com/item/1005007806214529.html`
- U.FL/IPX to SMA RF1.13 pigtail: `https://www.aliexpress.com/item/1005009812335103.html`

## Telescopic antenna

The listing title identifies a hinged SMA rod/telescopic antenna intended for 40 MHz to 6 GHz. Supplier dimensions recorded during the mechanical review are:

- collapsed overall length: **105 mm**
- extended overall length: **310 mm**
- maximum body/base diameter shown: **9 mm**
- 90-degree hinge at the connector end

The listing artwork is internally inconsistent about the connector naming (title says SMA while one image labels an F-type connector), so V2 does **not** assume antenna gender in the printed geometry. It provides clearance around a conventional 6.35 mm SMA bulkhead thread and leaves the removable antenna outside the print.

Cached supplier HTML and product images are deliberately excluded from the
public source tree. Use the source-product links above for the current listing;
local cached copies, when present on an engineering workstation, are ignored by
Git.

## Pigtail

The second listing is an RF1.13 coax pigtail family with U.FL/IPX at the PCB end and SMA/RP-SMA bulkhead variants. The exact ordered lead is treated mechanically as:

- nominal cable length: **100 mm**
- cable type: **RF1.13**
- PCB end: U.FL/IPX for Kiku J3
- enclosure end: SMA-style bulkhead

V2 uses a **6.8 mm bulkhead hole** and a **12 mm-diameter local reinforcement boss**. The larger boss is deliberate: if the delivered telescopic antenna/pigtail combination turns out to need a larger bulkhead despite the listing's SMA wording, the prototype hole can be cautiously opened up without reprinting the shell. The internal reserved cable centreline is approximately **88.3 mm**, allowing connector transitions and a small amount of service slack without coiling the cable tightly.

## Housing routing decision

J3 is at mechanical XY `(57.395, 66.55)` mm, very close to the right PCB edge. The cable first goes down the right side, then crosses towards the left only below the battery. The diagonal crossing leaves the lower-right BM83 RF keepout before it moves left. The bulkhead is at the left side around Y=18 mm. This also lets the hinged telescopic rod fold upward along the left side, keeping the 9 mm antenna body off the rear face and preserving the thinnest practical body profile.
