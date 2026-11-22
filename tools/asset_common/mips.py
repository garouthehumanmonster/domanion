"""mips.py - Mipmap chain generation supporting sRGB, linear, normal2, and premul modes."""

import numpy as np


# Precomputed 256-entry sRGB -> linear LUT (float32)
_U = np.arange(256, dtype=np.float32) / 255.0
_SRGB_TO_LIN = np.where(_U <= 0.04045, _U / 12.92, ((_U + 0.055) / 1.055) ** 2.4).astype(np.float32)


def _linear_to_srgb(lin):
    """Convert linear float32 [..., C] to uint8 sRGB using standard OETF."""
    lin = np.clip(lin, 0.0, 1.0)
    u = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * (lin ** (1.0 / 2.4)) - 0.055)
    return np.rint(np.clip(u, 0.0, 1.0) * 255.0).astype(np.uint8)


def _downsample_level(img, mode):
    """Downsample a single image level by 2x in width and height."""
    h, w = img.shape[:2]
    new_h = max(1, h // 2)
    new_w = max(1, w // 2)

    # Box-averaging slices
    # If dimension is 1, repeat slice; otherwise step by 2
    if h > 1 and w > 1:
        s00 = img[0::2, 0::2]
        s10 = img[1::2, 0::2]
        s01 = img[0::2, 1::2]
        s11 = img[1::2, 1::2]
        count = 4.0
    elif h > 1:
        s00 = img[0::2, :]
        s10 = img[1::2, :]
        s01 = s00
        s11 = s10
        count = 2.0
    elif w > 1:
        s00 = img[:, 0::2]
        s10 = s00
        s01 = img[:, 1::2]
        s11 = s01
        count = 2.0
    else:
        return img.copy()

    if mode == "srgb":
        # Convert RGB channels via LUT
        has_alpha = img.ndim == 3 and img.shape[2] == 4
        lin00 = _SRGB_TO_LIN[s00[..., :3]]
        lin10 = _SRGB_TO_LIN[s10[..., :3]]
        lin01 = _SRGB_TO_LIN[s01[..., :3]]
        lin11 = _SRGB_TO_LIN[s11[..., :3]]
        lin_avg = (lin00 + lin10 + lin01 + lin11) / count
        rgb_srgb = _linear_to_srgb(lin_avg)

        if has_alpha:
            a_avg = (
                s00[..., 3].astype(np.float32)
                + s10[..., 3].astype(np.float32)
                + s01[..., 3].astype(np.float32)
                + s11[..., 3].astype(np.float32)
            ) / count
            a_byte = np.rint(np.clip(a_avg, 0.0, 255.0)).astype(np.uint8)
            return np.concatenate([rgb_srgb, a_byte[..., None]], axis=-1)
        return rgb_srgb

    if mode == "normal2":
        # 2-channel uint8 xy -> decode to [-1, 1], z = sqrt(max(0, 1 - x^2 - y^2))
        def to_vec3(s):
            x = s[..., 0].astype(np.float32) / 255.0 * 2.0 - 1.0
            y = s[..., 1].astype(np.float32) / 255.0 * 2.0 - 1.0
            z = np.sqrt(np.maximum(0.0, 1.0 - x * x - y * y))
            return np.stack([x, y, z], axis=-1)

        v00 = to_vec3(s00)
        v10 = to_vec3(s10)
        v01 = to_vec3(s01)
        v11 = to_vec3(s11)
        v_avg = (v00 + v10 + v01 + v11) / count

        # Renormalize 3-vectors
        lens = np.linalg.norm(v_avg, axis=-1, keepdims=True)
        lens = np.where(lens > 0.0, lens, 1.0)
        v_norm = v_avg / lens

        # Re-encode xy with rint((v*0.5 + 0.5) * 255)
        xy = v_norm[..., :2]
        return np.rint(np.clip(xy * 0.5 + 0.5, 0.0, 1.0) * 255.0).astype(np.uint8)

    # linear / premul mode: plain linear average
    avg = (
        s00.astype(np.float32)
        + s10.astype(np.float32)
        + s01.astype(np.float32)
        + s11.astype(np.float32)
    ) / count
    return np.rint(np.clip(avg, 0.0, 255.0)).astype(np.uint8)


def mip_chain(img, mode):
    """Generate mipmap chain from level 0 down to 1x1. Returns list of uint8 arrays."""
    img = np.asarray(img, dtype=np.uint8)
    chain = [img]
    curr = img
    while curr.shape[0] > 1 or curr.shape[1] > 1:
        curr = _downsample_level(curr, mode)
        chain.append(curr)
    return chain
