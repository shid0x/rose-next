#include "stdafx.h"

#include "RoseRmlRenderer.h"

#include <RmlUi/Core/Core.h>

#include <d3dx9.h>

namespace {

/// RmlUi 6 hands us RGBA-ordered bytes with **premultiplied alpha**;
/// D3DCOLOR / D3DFMT_A8R8G8B8 is BGRA. Getting this backwards is not a subtle
/// bug - reds and blues swap - but the premultiply half is, so read the note on
/// the blend state in ApplyRenderState() before touching either.
inline DWORD
RmlColourToD3D(const Rml::ColourbPremultiplied& c) {
    return D3DCOLOR_ARGB(c.alpha, c.red, c.green, c.blue);
}

/// In-place RGBA -> BGRA channel swap for a raw pixel buffer.
void
SwizzleRGBAtoBGRA(unsigned char* p, size_t nPixels) {
    for (size_t i = 0; i < nPixels; ++i) {
        unsigned char t = p[i * 4 + 0];
        p[i * 4 + 0] = p[i * 4 + 2];
        p[i * 4 + 2] = t;
    }
}

} // namespace

RoseRmlRenderer::RoseRmlRenderer():
    m_pDevice(NULL),
    m_pSavedState(NULL),
    m_NextGeometryHandle(1),
    m_NextTextureHandle(1),
    m_iViewportWidth(0),
    m_iViewportHeight(0),
    m_bScissorEnabled(false),
    m_iDrawCalls(0),
    m_bDeviceObjectsValid(false) {
    SetRect(&m_rcScissor, 0, 0, 0, 0);
}

RoseRmlRenderer::~RoseRmlRenderer() {
    Shutdown();
}

bool
RoseRmlRenderer::Initialise(IDirect3DDevice9* pDevice) {
    if (pDevice == NULL)
        return false;

    m_pDevice = pDevice;

    D3DVIEWPORT9 vp;
    if (SUCCEEDED(m_pDevice->GetViewport(&vp)))
        SetViewportSize((int)vp.Width, (int)vp.Height);

    return CreateDeviceObjects();
}

void
RoseRmlRenderer::Shutdown() {
    ReleaseDeviceObjects();

    for (std::map<Rml::CompiledGeometryHandle, Geometry*>::iterator it = m_Geometries.begin();
        it != m_Geometries.end(); ++it) {
        delete it->second;
    }
    m_Geometries.clear();

    for (std::map<Rml::TextureHandle, Texture*>::iterator it = m_Textures.begin();
        it != m_Textures.end(); ++it) {
        delete it->second;
    }
    m_Textures.clear();

    m_pDevice = NULL;
}

void
RoseRmlRenderer::SetViewportSize(int iWidth, int iHeight) {
    m_iViewportWidth = iWidth;
    m_iViewportHeight = iHeight;
}

/// ---------------------------------------------------------------------------
/// Device lifetime
/// ---------------------------------------------------------------------------

void
RoseRmlRenderer::ReleaseDeviceObjects() {
    if (m_pSavedState != NULL) {
        m_pSavedState->Release();
        m_pSavedState = NULL;
    }

    /// Buffers die with the device but the CPU-side copies stay, so the geometry
    /// records survive and can be refilled in CreateDeviceObjects().
    for (std::map<Rml::CompiledGeometryHandle, Geometry*>::iterator it = m_Geometries.begin();
        it != m_Geometries.end(); ++it) {
        Geometry* pGeom = it->second;
        if (pGeom->pVB != NULL) {
            pGeom->pVB->Release();
            pGeom->pVB = NULL;
        }
        if (pGeom->pIB != NULL) {
            pGeom->pIB->Release();
            pGeom->pIB = NULL;
        }
    }

    for (std::map<Rml::TextureHandle, Texture*>::iterator it = m_Textures.begin();
        it != m_Textures.end(); ++it) {
        Texture* pTex = it->second;
        if (pTex->pTexture != NULL) {
            pTex->pTexture->Release();
            pTex->pTexture = NULL;
        }
    }

    m_bDeviceObjectsValid = false;
}

bool
RoseRmlRenderer::CreateDeviceObjects() {
    if (m_pDevice == NULL)
        return false;

    /// Refill every compiled geometry from its retained CPU copy. RmlUi does not
    /// re-issue CompileGeometry() after a reset, so skipping this leaves the UI
    /// silently blank until every document happens to be rebuilt.
    for (std::map<Rml::CompiledGeometryHandle, Geometry*>::iterator it = m_Geometries.begin();
        it != m_Geometries.end(); ++it) {
        Geometry* pGeom = it->second;
        if (pGeom->Vertices.empty() || pGeom->Indices.empty())
            continue;

        const UINT nVBBytes = (UINT)(pGeom->Vertices.size() * sizeof(Vertex));
        const UINT nIBBytes = (UINT)(pGeom->Indices.size() * sizeof(unsigned short));

        /// Write-once geometry: DEFAULT pool, but *not* D3DUSAGE_DYNAMIC. Only
        /// per-frame streaming buffers need DYNAMIC + DISCARD.
        if (FAILED(m_pDevice->CreateVertexBuffer(nVBBytes, D3DUSAGE_WRITEONLY, kFVF,
                D3DPOOL_DEFAULT, &pGeom->pVB, NULL)))
            return false;
        if (FAILED(m_pDevice->CreateIndexBuffer(nIBBytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16,
                D3DPOOL_DEFAULT, &pGeom->pIB, NULL)))
            return false;

        void* pDst = NULL;
        if (SUCCEEDED(pGeom->pVB->Lock(0, nVBBytes, &pDst, 0))) {
            memcpy(pDst, &pGeom->Vertices[0], nVBBytes);
            pGeom->pVB->Unlock();
        }
        if (SUCCEEDED(pGeom->pIB->Lock(0, nIBBytes, &pDst, 0))) {
            memcpy(pDst, &pGeom->Indices[0], nIBBytes);
            pGeom->pIB->Unlock();
        }
    }

    /// Textures sourced from disk can be reloaded; generated ones ( font atlases )
    /// are rebuilt from their retained pixels. Rml::ReleaseTextures() is still the
    /// belt-and-braces path for anything we miss.
    for (std::map<Rml::TextureHandle, Texture*>::iterator it = m_Textures.begin();
        it != m_Textures.end(); ++it) {
        ReloadTexture(*it->second);
    }

    m_bDeviceObjectsValid = true;
    return true;
}

/// ---------------------------------------------------------------------------
/// Geometry
/// ---------------------------------------------------------------------------

Rml::CompiledGeometryHandle
RoseRmlRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
    Rml::Span<const int> indices) {
    if (m_pDevice == NULL || vertices.empty() || indices.empty())
        return 0;

    Geometry* pGeom = new Geometry();
    pGeom->pVB = NULL;
    pGeom->pIB = NULL;
    pGeom->iNumVerts = (int)vertices.size();
    pGeom->iNumIndices = (int)indices.size();

    pGeom->Vertices.resize(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        const Rml::Vertex& src = vertices[i];
        Vertex& dst = pGeom->Vertices[i];
        dst.x = src.position.x;
        dst.y = src.position.y;
        dst.z = 0.0f;
        dst.color = RmlColourToD3D(src.colour);
        dst.u = src.tex_coord.x;
        dst.v = src.tex_coord.y;
    }

    /// 16-bit indices: RmlUi geometry chunks are far below 65k verts. Guard
    /// anyway rather than silently truncating.
    if (pGeom->iNumVerts > 65535) {
        delete pGeom;
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "RoseRmlRenderer: geometry exceeds 16-bit index range (%d verts).",
            pGeom->iNumVerts);
        return 0;
    }

    pGeom->Indices.resize(indices.size());
    for (size_t i = 0; i < indices.size(); ++i)
        pGeom->Indices[i] = (unsigned short)indices[i];

    const Rml::CompiledGeometryHandle handle = m_NextGeometryHandle++;
    m_Geometries[handle] = pGeom;

    /// Build the device buffers now by running the shared refill path for just
    /// this record.
    const UINT nVBBytes = (UINT)(pGeom->Vertices.size() * sizeof(Vertex));
    const UINT nIBBytes = (UINT)(pGeom->Indices.size() * sizeof(unsigned short));

    if (FAILED(m_pDevice->CreateVertexBuffer(nVBBytes, D3DUSAGE_WRITEONLY, kFVF, D3DPOOL_DEFAULT,
            &pGeom->pVB, NULL))
        || FAILED(m_pDevice->CreateIndexBuffer(nIBBytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16,
            D3DPOOL_DEFAULT, &pGeom->pIB, NULL))) {
        return handle; /// record kept; buffers retried on the next rebuild
    }

    void* pDst = NULL;
    if (SUCCEEDED(pGeom->pVB->Lock(0, nVBBytes, &pDst, 0))) {
        memcpy(pDst, &pGeom->Vertices[0], nVBBytes);
        pGeom->pVB->Unlock();
    }
    if (SUCCEEDED(pGeom->pIB->Lock(0, nIBBytes, &pDst, 0))) {
        memcpy(pDst, &pGeom->Indices[0], nIBBytes);
        pGeom->pIB->Unlock();
    }

    return handle;
}

void
RoseRmlRenderer::RenderGeometry(Rml::CompiledGeometryHandle geometry,
    Rml::Vector2f translation,
    Rml::TextureHandle texture) {
    if (m_pDevice == NULL)
        return;

    std::map<Rml::CompiledGeometryHandle, Geometry*>::iterator it = m_Geometries.find(geometry);
    if (it == m_Geometries.end())
        return;

    Geometry* pGeom = it->second;
    if (pGeom->pVB == NULL || pGeom->pIB == NULL)
        return;

    /// RmlUi's per-geometry translation rides on the world matrix so the compiled
    /// vertex buffer stays immutable.
    D3DXMATRIX matWorld;
    D3DXMatrixTranslation(&matWorld, translation.x, translation.y, 0.0f);
    m_pDevice->SetTransform(D3DTS_WORLD, &matWorld);

    if (texture != 0) {
        std::map<Rml::TextureHandle, Texture*>::iterator itTex = m_Textures.find(texture);
        m_pDevice->SetTexture(0,
            (itTex != m_Textures.end()) ? itTex->second->pTexture : NULL);
        m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    } else {
        /// Untextured geometry ( solid backgrounds, borders ): take colour from
        /// the vertex diffuse only, otherwise stage 0 samples a stale texture.
        m_pDevice->SetTexture(0, NULL);
        m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    }

    m_pDevice->SetStreamSource(0, pGeom->pVB, 0, sizeof(Vertex));
    m_pDevice->SetIndices(pGeom->pIB);
    m_pDevice->SetFVF(kFVF);
    m_pDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, pGeom->iNumVerts, 0,
        pGeom->iNumIndices / 3);

    ++m_iDrawCalls;
}

void
RoseRmlRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    std::map<Rml::CompiledGeometryHandle, Geometry*>::iterator it = m_Geometries.find(geometry);
    if (it == m_Geometries.end())
        return;

    Geometry* pGeom = it->second;
    if (pGeom->pVB != NULL)
        pGeom->pVB->Release();
    if (pGeom->pIB != NULL)
        pGeom->pIB->Release();
    delete pGeom;

    m_Geometries.erase(it);
}

/// ---------------------------------------------------------------------------
/// Textures
/// ---------------------------------------------------------------------------

bool
RoseRmlRenderer::UploadTexture(Texture& tex, const unsigned char* pBGRA, int iWidth, int iHeight) {
    if (m_pDevice == NULL || pBGRA == NULL || iWidth <= 0 || iHeight <= 0)
        return false;

    /// A D3DPOOL_DEFAULT texture cannot be locked, so fill a SYSTEMMEM staging
    /// copy and UpdateTexture() it across. This is the standard 9Ex dance and
    /// also keeps LoadTexture and GenerateTexture on one path.
    IDirect3DTexture9* pStaging = NULL;
    if (FAILED(m_pDevice->CreateTexture(iWidth, iHeight, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_SYSTEMMEM, &pStaging, NULL)))
        return false;

    D3DLOCKED_RECT lr;
    if (FAILED(pStaging->LockRect(0, &lr, NULL, 0))) {
        pStaging->Release();
        return false;
    }
    for (int y = 0; y < iHeight; ++y) {
        memcpy((unsigned char*)lr.pBits + y * lr.Pitch, pBGRA + (size_t)y * iWidth * 4,
            (size_t)iWidth * 4);
    }
    pStaging->UnlockRect(0);

    IDirect3DTexture9* pDefault = NULL;
    if (FAILED(m_pDevice->CreateTexture(iWidth, iHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
            &pDefault, NULL))) {
        pStaging->Release();
        return false;
    }

    const HRESULT hr = m_pDevice->UpdateTexture(pStaging, pDefault);
    pStaging->Release();

    if (FAILED(hr)) {
        pDefault->Release();
        return false;
    }

    if (tex.pTexture != NULL)
        tex.pTexture->Release();

    tex.pTexture = pDefault;
    tex.iWidth = iWidth;
    tex.iHeight = iHeight;
    return true;
}

bool
RoseRmlRenderer::ReloadTexture(Texture& tex) {
    /// Generated textures ( font atlases ) and already-decoded disk textures both
    /// replay straight from the retained pixels.
    if (!tex.Pixels.empty())
        return UploadTexture(tex, &tex.Pixels[0], tex.iWidth, tex.iHeight);

    if (tex.strSource.empty())
        return false;

    /// D3DX handles DDS / PNG / TGA / BMP, covering both RmlUi's sample assets
    /// and the game's DDS art. SYSTEMMEM so the result can be locked and cached.
    IDirect3DTexture9* pSys = NULL;
    if (FAILED(D3DXCreateTextureFromFileExA(m_pDevice, tex.strSource.c_str(), D3DX_DEFAULT_NONPOW2,
            D3DX_DEFAULT_NONPOW2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, D3DX_FILTER_NONE,
            D3DX_FILTER_NONE, 0, NULL, NULL, &pSys)))
        return false;

    D3DSURFACE_DESC desc;
    pSys->GetLevelDesc(0, &desc);

    D3DLOCKED_RECT lr;
    if (FAILED(pSys->LockRect(0, &lr, NULL, 0))) {
        pSys->Release();
        return false;
    }

    const int w = (int)desc.Width;
    const int h = (int)desc.Height;
    tex.Pixels.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) {
        memcpy(&tex.Pixels[(size_t)y * w * 4], (unsigned char*)lr.pBits + y * lr.Pitch,
            (size_t)w * 4);
    }
    pSys->UnlockRect(0);
    pSys->Release();

    /// RmlUi 6 composites in premultiplied alpha; D3DX gives us straight alpha,
    /// so premultiply here or every texture edge renders too bright.
    for (size_t i = 0; i < tex.Pixels.size(); i += 4) {
        const unsigned int a = tex.Pixels[i + 3];
        tex.Pixels[i + 0] = (unsigned char)((tex.Pixels[i + 0] * a) / 255);
        tex.Pixels[i + 1] = (unsigned char)((tex.Pixels[i + 1] * a) / 255);
        tex.Pixels[i + 2] = (unsigned char)((tex.Pixels[i + 2] * a) / 255);
    }

    tex.iWidth = w;
    tex.iHeight = h;
    return UploadTexture(tex, &tex.Pixels[0], w, h);
}

Rml::TextureHandle
RoseRmlRenderer::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) {
    if (m_pDevice == NULL)
        return 0;

    Texture* pTex = new Texture();
    pTex->pTexture = NULL;
    pTex->iWidth = 0;
    pTex->iHeight = 0;
    pTex->strSource = source.c_str();

    if (!ReloadTexture(*pTex)) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "RoseRmlRenderer: failed to load texture '%s'.",
            source.c_str());
        delete pTex;
        return 0;
    }

    texture_dimensions.x = pTex->iWidth;
    texture_dimensions.y = pTex->iHeight;

    const Rml::TextureHandle handle = m_NextTextureHandle++;
    m_Textures[handle] = pTex;
    return handle;
}

Rml::TextureHandle
RoseRmlRenderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) {
    if (m_pDevice == NULL || source.empty())
        return 0;

    const int w = source_dimensions.x;
    const int h = source_dimensions.y;
    if (w <= 0 || h <= 0)
        return 0;

    Texture* pTex = new Texture();
    pTex->pTexture = NULL;
    pTex->iWidth = w;
    pTex->iHeight = h;

    /// Already premultiplied by RmlUi; only the channel order needs fixing.
    pTex->Pixels.assign(source.begin(), source.end());
    SwizzleRGBAtoBGRA(&pTex->Pixels[0], (size_t)w * h);

    if (!UploadTexture(*pTex, &pTex->Pixels[0], w, h)) {
        delete pTex;
        return 0;
    }

    const Rml::TextureHandle handle = m_NextTextureHandle++;
    m_Textures[handle] = pTex;
    return handle;
}

void
RoseRmlRenderer::ReleaseTexture(Rml::TextureHandle texture) {
    std::map<Rml::TextureHandle, Texture*>::iterator it = m_Textures.find(texture);
    if (it == m_Textures.end())
        return;

    if (it->second->pTexture != NULL)
        it->second->pTexture->Release();
    delete it->second;
    m_Textures.erase(it);
}

/// ---------------------------------------------------------------------------
/// Scissor
/// ---------------------------------------------------------------------------

void
RoseRmlRenderer::EnableScissorRegion(bool enable) {
    if (m_pDevice == NULL)
        return;

    m_bScissorEnabled = enable;
    m_pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, enable ? TRUE : FALSE);
}

void
RoseRmlRenderer::SetScissorRegion(Rml::Rectanglei region) {
    if (m_pDevice == NULL)
        return;

    m_rcScissor.left = region.Left();
    m_rcScissor.top = region.Top();
    m_rcScissor.right = region.Right();
    m_rcScissor.bottom = region.Bottom();
    m_pDevice->SetScissorRect(&m_rcScissor);
}

bool
RoseRmlRenderer::GetEffectiveScissor(const RECT& rcElement, RECT& rcOut) const {
    rcOut = rcElement;

    if (m_bScissorEnabled) {
        /// setAvatarViewPort() sets a D3D *viewport*, which ignores the scissor
        /// test entirely - so a preview pane inside a scrolling container would
        /// draw outside its clip. Intersect here and let the caller skip.
        if (rcOut.left < m_rcScissor.left)
            rcOut.left = m_rcScissor.left;
        if (rcOut.top < m_rcScissor.top)
            rcOut.top = m_rcScissor.top;
        if (rcOut.right > m_rcScissor.right)
            rcOut.right = m_rcScissor.right;
        if (rcOut.bottom > m_rcScissor.bottom)
            rcOut.bottom = m_rcScissor.bottom;
    }

    return (rcOut.right > rcOut.left) && (rcOut.bottom > rcOut.top);
}

/// ---------------------------------------------------------------------------
/// Pass framing
/// ---------------------------------------------------------------------------

bool
RoseRmlRenderer::ApplyRenderState() {
    if (m_pDevice == NULL)
        return false;

    m_pDevice->SetVertexShader(NULL);
    m_pDevice->SetPixelShader(NULL);
    m_pDevice->SetFVF(kFVF);

    m_pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    m_pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_pDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
    m_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    m_pDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    m_pDevice->SetRenderState(D3DRS_STENCILENABLE, FALSE);

    /// PREMULTIPLIED alpha ( RmlUi 6 ) - source is ONE, not SRCALPHA. Using the
    /// conventional SRCALPHA/INVSRCALPHA pair here double-applies alpha and every
    /// anti-aliased glyph edge and faded panel comes out too dark.
    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    m_pDevice->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);

    m_pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    m_pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    m_pDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    m_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    m_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    m_pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    m_pDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    /// Pixel-exact 2D: D3D9 samples texel centres, so shift by half a pixel.
    D3DXMATRIX matIdentity, matView, matProj;
    D3DXMatrixIdentity(&matIdentity);
    D3DXMatrixTranslation(&matView, -0.5f, -0.5f, 0.0f);
    D3DXMatrixOrthoOffCenterLH(&matProj, 0.0f, (float)m_iViewportWidth,
        (float)m_iViewportHeight, 0.0f, -1.0f, 1.0f);

    m_pDevice->SetTransform(D3DTS_WORLD, &matIdentity);
    m_pDevice->SetTransform(D3DTS_VIEW, &matView);
    m_pDevice->SetTransform(D3DTS_PROJECTION, &matProj);

    return true;
}

void
RoseRmlRenderer::BeginFrame() {
    if (m_pDevice == NULL)
        return;

    /// Capture the engine's entire device state so EndFrame can put it back
    /// byte-for-byte. This is what keeps the world render unaffected by the
    /// overlay ( Spike 1 exit criterion #3 ).
    if (m_pSavedState == NULL)
        m_pDevice->CreateStateBlock(D3DSBT_ALL, &m_pSavedState);
    if (m_pSavedState != NULL)
        m_pSavedState->Capture();

    m_iDrawCalls = 0;
    m_bScissorEnabled = false;
    m_pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

    ApplyRenderState();
}

void
RoseRmlRenderer::EndFrame() {
    if (m_pDevice == NULL)
        return;

    m_pDevice->SetTexture(0, NULL);
    m_pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

    if (m_pSavedState != NULL)
        m_pSavedState->Apply();
}
