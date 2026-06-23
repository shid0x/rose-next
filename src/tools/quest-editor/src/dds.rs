//! Minimal DDS decoder covering the formats ROSE uses for ICON*.DDS:
//! DXT1 (BC1), DXT3 (BC2), DXT5 (BC3), and A8R8G8B8 / X8R8G8B8 uncompressed.

use anyhow::{anyhow, Result};

#[derive(Debug)]
pub struct DecodedImage {
    pub width: u32,
    pub height: u32,
    /// Tightly packed RGBA8, row-major, top-down.
    pub rgba: Vec<u8>,
}

impl DecodedImage {
    pub fn crop(&self, x: u32, y: u32, w: u32, h: u32) -> Option<DecodedImage> {
        if x.checked_add(w)? > self.width || y.checked_add(h)? > self.height {
            return None;
        }
        let mut out = Vec::with_capacity((w * h * 4) as usize);
        for row in 0..h {
            let src_off = (((y + row) * self.width + x) * 4) as usize;
            let len = (w * 4) as usize;
            out.extend_from_slice(&self.rgba[src_off..src_off + len]);
        }
        Some(DecodedImage {
            width: w,
            height: h,
            rgba: out,
        })
    }
}

pub fn decode(data: &[u8]) -> Result<DecodedImage> {
    if data.len() < 128 || &data[0..4] != b"DDS " {
        return Err(anyhow!("not a DDS file"));
    }
    // DDS_HEADER starts at byte 4, 124 bytes long.
    let height = u32_at(data, 12);
    let width = u32_at(data, 16);
    // pf starts at offset 76 (within 124-byte header) + 4 (magic) = 80.
    let pf_flags = u32_at(data, 80);
    let pf_fourcc = &data[84..88];
    let rgb_bit_count = u32_at(data, 88);
    let r_mask = u32_at(data, 92);
    let g_mask = u32_at(data, 96);
    let b_mask = u32_at(data, 100);
    let a_mask = u32_at(data, 104);

    let pixel_data = &data[128..];

    const DDPF_FOURCC: u32 = 0x4;
    const DDPF_RGB: u32 = 0x40;
    const DDPF_ALPHAPIXELS: u32 = 0x1;

    if (pf_flags & DDPF_FOURCC) != 0 {
        match pf_fourcc {
            b"DXT1" => Ok(decode_bc1(pixel_data, width, height)),
            b"DXT3" => Ok(decode_bc2(pixel_data, width, height)),
            b"DXT5" => Ok(decode_bc3(pixel_data, width, height)),
            other => Err(anyhow!(
                "unsupported DDS fourcc: {}",
                String::from_utf8_lossy(other)
            )),
        }
    } else if (pf_flags & DDPF_RGB) != 0 && rgb_bit_count == 32 {
        Ok(decode_rgb32(
            pixel_data,
            width,
            height,
            r_mask,
            g_mask,
            b_mask,
            if (pf_flags & DDPF_ALPHAPIXELS) != 0 {
                Some(a_mask)
            } else {
                None
            },
        ))
    } else if (pf_flags & DDPF_RGB) != 0 && rgb_bit_count == 16 {
        Ok(decode_rgb16(pixel_data, width, height, r_mask, g_mask, b_mask))
    } else {
        Err(anyhow!(
            "unsupported DDS format (flags=0x{:x}, bpp={})",
            pf_flags,
            rgb_bit_count
        ))
    }
}

fn u32_at(data: &[u8], off: usize) -> u32 {
    u32::from_le_bytes([data[off], data[off + 1], data[off + 2], data[off + 3]])
}

/// ---- BC1 ----
fn decode_bc1(data: &[u8], width: u32, height: u32) -> DecodedImage {
    let mut rgba = vec![0u8; (width * height * 4) as usize];
    let bw = ((width + 3) / 4) as usize;
    let bh = ((height + 3) / 4) as usize;
    for by in 0..bh {
        for bx in 0..bw {
            let off = (by * bw + bx) * 8;
            if off + 8 > data.len() {
                continue;
            }
            let block = &data[off..off + 8];
            let c0 = u16::from_le_bytes([block[0], block[1]]);
            let c1 = u16::from_le_bytes([block[2], block[3]]);
            let bits =
                u32::from_le_bytes([block[4], block[5], block[6], block[7]]);
            let (r0, g0, b0) = rgb565(c0);
            let (r1, g1, b1) = rgb565(c1);
            let mut palette = [[0u8; 4]; 4];
            palette[0] = [r0, g0, b0, 255];
            palette[1] = [r1, g1, b1, 255];
            if c0 > c1 {
                palette[2] = [
                    ((2 * r0 as u16 + r1 as u16) / 3) as u8,
                    ((2 * g0 as u16 + g1 as u16) / 3) as u8,
                    ((2 * b0 as u16 + b1 as u16) / 3) as u8,
                    255,
                ];
                palette[3] = [
                    ((r0 as u16 + 2 * r1 as u16) / 3) as u8,
                    ((g0 as u16 + 2 * g1 as u16) / 3) as u8,
                    ((b0 as u16 + 2 * b1 as u16) / 3) as u8,
                    255,
                ];
            } else {
                palette[2] = [
                    ((r0 as u16 + r1 as u16) / 2) as u8,
                    ((g0 as u16 + g1 as u16) / 2) as u8,
                    ((b0 as u16 + b1 as u16) / 2) as u8,
                    255,
                ];
                palette[3] = [0, 0, 0, 0]; // transparent black (1-bit alpha)
            }
            for py in 0..4 {
                for px in 0..4 {
                    let idx = ((bits >> (2 * (4 * py + px))) & 0x3) as usize;
                    let x = bx * 4 + px;
                    let y = by * 4 + py;
                    if x < width as usize && y < height as usize {
                        let off = (y * width as usize + x) * 4;
                        rgba[off..off + 4].copy_from_slice(&palette[idx]);
                    }
                }
            }
        }
    }
    DecodedImage {
        width,
        height,
        rgba,
    }
}

/// ---- BC2 (DXT3): explicit 4-bit alpha + BC1-style color ----
fn decode_bc2(data: &[u8], width: u32, height: u32) -> DecodedImage {
    let mut rgba = vec![0u8; (width * height * 4) as usize];
    let bw = ((width + 3) / 4) as usize;
    let bh = ((height + 3) / 4) as usize;
    for by in 0..bh {
        for bx in 0..bw {
            let off = (by * bw + bx) * 16;
            if off + 16 > data.len() {
                continue;
            }
            let block = &data[off..off + 16];
            // 8 bytes of 4-bit alpha
            let alpha = &block[0..8];
            // color block is same as BC1 but c0>c1 rule doesn't branch
            let c0 = u16::from_le_bytes([block[8], block[9]]);
            let c1 = u16::from_le_bytes([block[10], block[11]]);
            let bits =
                u32::from_le_bytes([block[12], block[13], block[14], block[15]]);
            let (r0, g0, b0) = rgb565(c0);
            let (r1, g1, b1) = rgb565(c1);
            let mut palette = [[0u8; 3]; 4];
            palette[0] = [r0, g0, b0];
            palette[1] = [r1, g1, b1];
            palette[2] = [
                ((2 * r0 as u16 + r1 as u16) / 3) as u8,
                ((2 * g0 as u16 + g1 as u16) / 3) as u8,
                ((2 * b0 as u16 + b1 as u16) / 3) as u8,
            ];
            palette[3] = [
                ((r0 as u16 + 2 * r1 as u16) / 3) as u8,
                ((g0 as u16 + 2 * g1 as u16) / 3) as u8,
                ((b0 as u16 + 2 * b1 as u16) / 3) as u8,
            ];
            for py in 0..4 {
                let a_row = u16::from_le_bytes([alpha[py * 2], alpha[py * 2 + 1]]);
                for px in 0..4 {
                    let idx = ((bits >> (2 * (4 * py + px))) & 0x3) as usize;
                    let a4 = ((a_row >> (4 * px)) & 0xf) as u8;
                    let a = (a4 << 4) | a4;
                    let x = bx * 4 + px;
                    let y = by * 4 + py;
                    if x < width as usize && y < height as usize {
                        let off = (y * width as usize + x) * 4;
                        rgba[off] = palette[idx][0];
                        rgba[off + 1] = palette[idx][1];
                        rgba[off + 2] = palette[idx][2];
                        rgba[off + 3] = a;
                    }
                }
            }
        }
    }
    DecodedImage {
        width,
        height,
        rgba,
    }
}

/// ---- BC3 (DXT5): interpolated alpha + BC1-style color ----
fn decode_bc3(data: &[u8], width: u32, height: u32) -> DecodedImage {
    let mut rgba = vec![0u8; (width * height * 4) as usize];
    let bw = ((width + 3) / 4) as usize;
    let bh = ((height + 3) / 4) as usize;
    for by in 0..bh {
        for bx in 0..bw {
            let off = (by * bw + bx) * 16;
            if off + 16 > data.len() {
                continue;
            }
            let block = &data[off..off + 16];
            let a0 = block[0];
            let a1 = block[1];
            // 48-bit alpha indices
            let mut a_bits: u64 = 0;
            for i in 0..6 {
                a_bits |= (block[2 + i] as u64) << (8 * i);
            }
            let mut a_pal = [0u8; 8];
            a_pal[0] = a0;
            a_pal[1] = a1;
            if a0 > a1 {
                for i in 1..=6 {
                    a_pal[i + 1] =
                        (((7 - i) as u16 * a0 as u16 + i as u16 * a1 as u16) / 7) as u8;
                }
            } else {
                for i in 1..=4 {
                    a_pal[i + 1] =
                        (((5 - i) as u16 * a0 as u16 + i as u16 * a1 as u16) / 5) as u8;
                }
                a_pal[6] = 0;
                a_pal[7] = 255;
            }

            let c0 = u16::from_le_bytes([block[8], block[9]]);
            let c1 = u16::from_le_bytes([block[10], block[11]]);
            let bits =
                u32::from_le_bytes([block[12], block[13], block[14], block[15]]);
            let (r0, g0, b0) = rgb565(c0);
            let (r1, g1, b1) = rgb565(c1);
            let mut palette = [[0u8; 3]; 4];
            palette[0] = [r0, g0, b0];
            palette[1] = [r1, g1, b1];
            palette[2] = [
                ((2 * r0 as u16 + r1 as u16) / 3) as u8,
                ((2 * g0 as u16 + g1 as u16) / 3) as u8,
                ((2 * b0 as u16 + b1 as u16) / 3) as u8,
            ];
            palette[3] = [
                ((r0 as u16 + 2 * r1 as u16) / 3) as u8,
                ((g0 as u16 + 2 * g1 as u16) / 3) as u8,
                ((b0 as u16 + 2 * b1 as u16) / 3) as u8,
            ];

            for py in 0..4 {
                for px in 0..4 {
                    let c_idx = ((bits >> (2 * (4 * py + px))) & 0x3) as usize;
                    let a_idx = ((a_bits >> (3 * (4 * py + px))) & 0x7) as usize;
                    let x = bx * 4 + px;
                    let y = by * 4 + py;
                    if x < width as usize && y < height as usize {
                        let off = (y * width as usize + x) * 4;
                        rgba[off] = palette[c_idx][0];
                        rgba[off + 1] = palette[c_idx][1];
                        rgba[off + 2] = palette[c_idx][2];
                        rgba[off + 3] = a_pal[a_idx];
                    }
                }
            }
        }
    }
    DecodedImage {
        width,
        height,
        rgba,
    }
}

fn rgb565(c: u16) -> (u8, u8, u8) {
    let r = ((c >> 11) & 0x1f) as u8;
    let g = ((c >> 5) & 0x3f) as u8;
    let b = (c & 0x1f) as u8;
    ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))
}

fn decode_rgb32(
    data: &[u8],
    width: u32,
    height: u32,
    r_mask: u32,
    g_mask: u32,
    b_mask: u32,
    a_mask: Option<u32>,
) -> DecodedImage {
    let n = (width * height) as usize;
    let mut rgba = vec![0u8; n * 4];
    for i in 0..n {
        let off = i * 4;
        if off + 4 > data.len() {
            break;
        }
        let px = u32::from_le_bytes([data[off], data[off + 1], data[off + 2], data[off + 3]]);
        let r = mask_to_byte(px, r_mask);
        let g = mask_to_byte(px, g_mask);
        let b = mask_to_byte(px, b_mask);
        let a = match a_mask {
            Some(m) if m != 0 => mask_to_byte(px, m),
            _ => 255,
        };
        rgba[off] = r;
        rgba[off + 1] = g;
        rgba[off + 2] = b;
        rgba[off + 3] = a;
    }
    DecodedImage {
        width,
        height,
        rgba,
    }
}

fn decode_rgb16(
    data: &[u8],
    width: u32,
    height: u32,
    r_mask: u32,
    g_mask: u32,
    b_mask: u32,
) -> DecodedImage {
    let n = (width * height) as usize;
    let mut rgba = vec![0u8; n * 4];
    for i in 0..n {
        let off = i * 2;
        if off + 2 > data.len() {
            break;
        }
        let px = u16::from_le_bytes([data[off], data[off + 1]]) as u32;
        let out = i * 4;
        rgba[out] = mask_to_byte(px, r_mask);
        rgba[out + 1] = mask_to_byte(px, g_mask);
        rgba[out + 2] = mask_to_byte(px, b_mask);
        rgba[out + 3] = 255;
    }
    DecodedImage {
        width,
        height,
        rgba,
    }
}

fn mask_to_byte(px: u32, mask: u32) -> u8 {
    if mask == 0 {
        return 0;
    }
    let shift = mask.trailing_zeros();
    let bits = 32 - shift - mask.leading_zeros();
    let v = (px & mask) >> shift;
    if bits >= 8 {
        (v >> (bits - 8)) as u8
    } else {
        let max = (1u32 << bits) - 1;
        ((v * 255 + max / 2) / max) as u8
    }
}
