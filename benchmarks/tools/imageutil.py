"""Image I/O and quality metrics shared by the FellowBench analysis tools.

Metrics
  PSNR   on 8-bit sRGB display images (data range 255), or on tonemapped HDR.
  SSIM   skimage.metrics.structural_similarity, computed on luma (Rec.709 weights) of display images,
         gaussian window (sigma 1.5) as in Wang et al. 2004.
  FLIP-lite: not used. LPIPS is optional (only if `lpips` + `torch` import successfully).
No metric is treated as ground truth; the report always shows the images next to the numbers.
"""

from __future__ import annotations

import os

import numpy as np
from PIL import Image

try:
    from skimage.metrics import structural_similarity as _ssim
except Exception:  # pragma: no cover
    _ssim = None


def load_rgb(path: str) -> np.ndarray:
    """Returns float32 HxWx3. PNG -> display-referred 0..1 sRGB values; EXR -> linear radiance."""
    if path.lower().endswith(".exr"):
        return load_exr(path)
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0


def load_exr(path: str) -> np.ndarray:
    import OpenEXR  # type: ignore

    with OpenEXR.File(path) as f:
        ch = f.channels()
        if "RGB" in ch:
            arr = ch["RGB"].pixels
        elif "RGBA" in ch:
            arr = ch["RGBA"].pixels[..., :3]
        else:
            arr = np.stack([ch[c].pixels for c in ("R", "G", "B")], axis=-1)
    return np.asarray(arr, dtype=np.float32)


def save_rgb(path: str, img: np.ndarray) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    Image.fromarray(np.clip(img * 255.0 + 0.5, 0, 255).astype(np.uint8)).save(path)


def srgb_to_linear(c: np.ndarray) -> np.ndarray:
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c: np.ndarray) -> np.ndarray:
    c = np.clip(c, 0.0, None)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1.0 / 2.4) - 0.055)


def luma(img: np.ndarray) -> np.ndarray:
    return img[..., 0] * 0.2126 + img[..., 1] * 0.7152 + img[..., 2] * 0.0722


def psnr(a: np.ndarray, b: np.ndarray) -> float:
    mse = float(np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2))
    if mse <= 1e-12:
        return float("inf")
    return 10.0 * np.log10(1.0 / mse)


def ssim(a: np.ndarray, b: np.ndarray) -> float:
    if _ssim is None:
        return float("nan")
    return float(_ssim(luma(a), luma(b), data_range=1.0, gaussian_weights=True, sigma=1.5, use_sample_covariance=False))


def ssim_map(a: np.ndarray, b: np.ndarray):
    if _ssim is None:
        return float("nan"), None
    s, m = _ssim(luma(a), luma(b), data_range=1.0, gaussian_weights=True, sigma=1.5, use_sample_covariance=False, full=True)
    return float(s), m


_lpips_model = None


def lpips(a: np.ndarray, b: np.ndarray) -> float | None:
    """LPIPS (AlexNet) if available; inputs are display images 0..1. Returns None when unavailable."""
    global _lpips_model
    # Keep downloaded AlexNet weights inside the repository (.tools/torch), not in the user profile.
    os.environ.setdefault("TORCH_HOME", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".tools", "torch"))
    try:
        import lpips as _lp  # type: ignore
        import torch  # type: ignore
    except Exception:
        return None
    if _lpips_model is None:
        _lpips_model = _lp.LPIPS(net="alex", verbose=False)
    ta = torch.from_numpy(a.transpose(2, 0, 1)[None] * 2.0 - 1.0).float()
    tb = torch.from_numpy(b.transpose(2, 0, 1)[None] * 2.0 - 1.0).float()
    with torch.no_grad():
        return float(_lpips_model(ta, tb).item())


def diff_heatmap(a: np.ndarray, b: np.ndarray, gain: float = 4.0) -> np.ndarray:
    """|a-b| luma difference x gain mapped through 'inferno' (black = identical)."""
    import matplotlib

    d = np.clip(np.abs(luma(a) - luma(b)) * gain, 0.0, 1.0)
    cmap = matplotlib.colormaps["inferno"]
    return cmap(d)[..., :3].astype(np.float32)


def tonemap_display(hdr: np.ndarray, exposure: float = 1.0) -> np.ndarray:
    """Neutral display transform used ONLY to compare two HDR images on equal terms (both go through it)."""
    x = np.clip(hdr * exposure, 0.0, None)
    # ACES fitted curve (Narkowicz 2015), then sRGB encode.
    a, b, c, d, e = 2.51, 0.03, 2.43, 0.59, 0.14
    y = np.clip((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0)
    return linear_to_srgb(y).astype(np.float32)


def crop(img: np.ndarray, box) -> np.ndarray:
    x0, y0, x1, y1 = box
    return img[y0:y1, x0:x1]
