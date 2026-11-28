# Credits: the showcase's people

The people in `showcase/people/*.glb` are made by `make_characters.py` with MakeHuman's MPFB 2 from these assets.
Their bodies, skins, eyes, eyebrows, eyelashes, hair, teeth and tongue, and the region masks behind the skin's maps,
are MakeHuman's (CC0: the system assets pack, <https://static.makehumancommunity.org/assets/assetpacks/makehuman_system_assets.html>;
the faces' shapes from the Faceunits 01 pack, CC0). Their clothes (milestone 253) come from MakeHuman's community asset
packs:

| garment | author | licence | pack | worn by |
|---|---|---|---|---|
| `elvs_goddess_dress6` | Elvaerwyn | [CC BY](https://creativecommons.org/licenses/by/4.0/) | [dress02](https://static.makehumancommunity.org/assets/assetpacks/dress02.html) | Giulia |
| `elvs_ladies_apron` | Elvaerwyn | [CC BY](https://creativecommons.org/licenses/by/4.0/) | [dress02](https://static.makehumancommunity.org/assets/assetpacks/dress02.html) | Rosa |
| `elvs_ruffle_sleeve_peasant_blouse_1` | Elvaerwyn | [CC BY](https://creativecommons.org/licenses/by/4.0/) | [shirts03](https://static.makehumancommunity.org/assets/assetpacks/shirts03.html) | Rosa |
| `drednicolson_asymmetric_tunic_and_sash` | DredNicolson | [CC BY](https://creativecommons.org/licenses/by/4.0/) | [shirts03](https://static.makehumancommunity.org/assets/assetpacks/shirts03.html) | Kenji |
| `rehmanpolanski_viking_tunic`, `_pants`, `_boots` | RehmanPolanski | CC0 | [suits02](https://static.makehumancommunity.org/assets/assetpacks/suits02.html) | Marco, Luca, Kenji (and everyone's boots) |
| `donitz_monk_robe` | Donitz | CC0 | [suits02](https://static.makehumancommunity.org/assets/assetpacks/suits02.html) | Aldo |
| `wdg_mycenaean_tunic` | WDG | CC0 | [dress01](https://static.makehumancommunity.org/assets/assetpacks/dress01.html) | Amara |
| `toigo_long_full_skirt` | MargaretToigo | CC0 | [skirts01](https://static.makehumancommunity.org/assets/assetpacks/skirts01.html) | Rosa, Amara |

Changes made to them (`make_characters.py`): each fitted to its person's body by MPFB; the colour textures of the viking
tunic, the long full skirt, the peasant blouse, the apron and the asymmetric tunic replaced by a plain woven cloth of
the tool's own (`PLAIN_CLOTH`; their normal maps kept), which the showcase dyes in period colours
(`FellowPerson.PERIOD_DYES`); the blouse and the Mycenaean tunic tucked under the skirt and the skirt drawn in under
the apron (`layer_clothes`); textures resized and re-encoded.

The CC BY garments' licence asks for this attribution wherever the people are shown or redistributed: "Clothes by
Elvaerwyn and DredNicolson (MakeHuman community assets, CC BY)".
