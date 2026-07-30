#include "stdafx.h"

#include "RoseRmlRenderer.h"

#include <RmlUi/Core/Core.h>
/// Core.h only forward-declares ColorStop; the gradient ramp needs the layout.
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Variant.h>

#include "../Util/CFileSystem.h"
#include "../Util/VFSManager.h"

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

/// Reads a file out of the game VFS. The game's own UI art ( Ui.TSI and the DDS
/// files under 3DDATA/CONTROL/RES ) lives inside data.idx/.vfs, which RmlUi's
/// default fopen-based loader cannot see -- so any RCSS that wants to reuse
/// game sprites has to come through here.
///
/// Paths are normalised to backslashes to match the rest of the client
/// ( io_imageres.cpp opens "3DData\\Control\\Res\\Ui.TSI" ). Returns false when
/// the file is not in the VFS, letting the caller fall back to disk -- which is
/// what keeps loose authoring files working during iteration.
bool
ReadFromVFS(const char* pPath, std::vector<unsigned char>& Out) {
    if (pPath == NULL || *pPath == '\0')
        return false;

    std::string strPath(pPath);
    for (size_t i = 0; i < strPath.size(); ++i) {
        if (strPath[i] == '/')
            strPath[i] = '\\';
    }

    CFileSystem* pFS = CVFSManager::GetSingleton().GetFileSystem();
    if (pFS == NULL)
        return false;

    bool bOk = false;
    if (pFS->IsExist(strPath.c_str()) && pFS->OpenFile(strPath.c_str(), OPEN_READ_BIN)) {
        if (pFS->ReadToMemory()) {
            const int iSize = pFS->GetSize();
            unsigned char* pData = pFS->GetData();
            if (iSize > 0 && pData != NULL) {
                Out.assign(pData, pData + iSize);
                bOk = true;
            }
            pFS->ReleaseData();
        }
        pFS->CloseFile();
    }

    /// The manager pools filesystems; failing to return one leaks a slot.
    CVFSManager::GetSingleton().ReturnToManager(pFS);
    return bOk;
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
    m_NextShaderHandle(1),
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

    for (std::map<Rml::CompiledShaderHandle, Shader*>::iterator it = m_Shaders.begin();
        it != m_Shaders.end(); ++it) {
        delete it->second;
    }
    m_Shaders.clear();

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

    /// Gradient ramps are DEFAULT-pool too, and RmlUi no more re-requests a
    /// compiled shader after a reset than it does compiled geometry -- so the
    /// retained ramp pixels are what bring them back.
    for (std::map<Rml::CompiledShaderHandle, Shader*>::iterator it = m_Shaders.begin();
        it != m_Shaders.end(); ++it) {
        if (it->second->pRamp != NULL) {
            it->second->pRamp->Release();
            it->second->pRamp = NULL;
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

    for (std::map<Rml::CompiledShaderHandle, Shader*>::iterator it = m_Shaders.begin();
        it != m_Shaders.end(); ++it) {
        UploadRamp(*it->second);
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

    DrawGeometryRaw(*pGeom);
}

void
RoseRmlRenderer::DrawGeometryRaw(const Geometry& geom) {
    m_pDevice->SetStreamSource(0, geom.pVB, 0, sizeof(Vertex));
    m_pDevice->SetIndices(geom.pIB);
    m_pDevice->SetFVF(kFVF);
    m_pDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, geom.iNumVerts, 0,
        geom.iNumIndices / 3);

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

    /// D3DX handles DDS / PNG / TGA / BMP, covering both loose authoring art and
    /// the game's DDS atlases. SYSTEMMEM so the result can be locked and cached.
    ///
    /// **Loose files win.** A player who drops an image next to the .rcss must be
    /// able to override whatever the skin shipped with -- that is the whole point
    /// of authoring UI from editable files rather than baked atlases. The VFS is
    /// only the fallback, for the occasional case where a skin deliberately
    /// reuses existing game art ( class icons and the like ).
    IDirect3DTexture9* pSys = NULL;
    bool bFromVFS = false;

    if (FAILED(D3DXCreateTextureFromFileExA(m_pDevice, tex.strSource.c_str(),
            D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
            D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0, NULL, NULL, &pSys))) {
        std::vector<unsigned char> FileBytes;
        if (!ReadFromVFS(tex.strSource.c_str(), FileBytes))
            return false;
        if (FAILED(D3DXCreateTextureFromFileInMemoryEx(m_pDevice, &FileBytes[0],
                (UINT)FileBytes.size(), D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2, 1, 0,
                D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0, NULL,
                NULL, &pSys)))
            return false;
        bFromVFS = true;
    }

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

    /// Say which source won. Game art and loose authoring files look identical
    /// once loaded, so without this a silently-failing VFS path is impossible to
    /// tell from a working one.
    Rml::Log::Message(Rml::Log::LT_INFO, "RoseRmlRenderer: loaded %s from %s (%dx%d)",
        tex.strSource.c_str(), bFromVFS ? "VFS" : "disk", w, h);

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
/// Gradients
///
/// RmlUi asks for gradients through CompileShader/RenderShader, which normally
/// implies a programmable pipeline. It does not have to: the gradient decorator
/// sets every vertex's tex_coord to its element-local pixel position
/// ( DecoratorGradient.cpp, `vertex.tex_coord = vertex.position - render_offset` )
/// and gives us p0/p1 in that same space. Projecting position onto that axis is
/// an affine map, so a fixed-function texture-coordinate transform onto a 1-D
/// colour ramp reproduces a linear gradient exactly, with no shader at all.
/// ---------------------------------------------------------------------------

bool
RoseRmlRenderer::UploadRamp(Shader& sh) {
    if (m_pDevice == NULL || sh.RampPixels.empty() || sh.iRampWidth <= 0)
        return false;

    IDirect3DTexture9* pStaging = NULL;
    if (FAILED(m_pDevice->CreateTexture(sh.iRampWidth, 1, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_SYSTEMMEM, &pStaging, NULL)))
        return false;

    D3DLOCKED_RECT lr;
    if (FAILED(pStaging->LockRect(0, &lr, NULL, 0))) {
        pStaging->Release();
        return false;
    }
    memcpy(lr.pBits, &sh.RampPixels[0], (size_t)sh.iRampWidth * 4);
    pStaging->UnlockRect(0);

    IDirect3DTexture9* pDefault = NULL;
    if (FAILED(m_pDevice->CreateTexture(sh.iRampWidth, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
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

    if (sh.pRamp != NULL)
        sh.pRamp->Release();
    sh.pRamp = pDefault;
    return true;
}

Rml::CompiledShaderHandle
RoseRmlRenderer::CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) {
    if (m_pDevice == NULL)
        return 0;

    if (name != "linear-gradient") {
        /// Radial and conic gradients are a per-pixel function of distance or
        /// angle, which no texture-coordinate transform can express. Say so
        /// rather than drawing nothing silently.
        Rml::Log::Message(Rml::Log::LT_WARNING,
            "RoseRmlRenderer: '%s' is not supported by the D3D9 backend; only "
            "linear-gradient is. The decorator will not draw.",
            name.c_str());
        return 0;
    }

    const Rml::Variant* pP0 = Rml::GetIf(parameters, "p0");
    const Rml::Variant* pP1 = Rml::GetIf(parameters, "p1");
    const Rml::Variant* pRepeating = Rml::GetIf(parameters, "repeating");
    const Rml::Variant* pStops = Rml::GetIf(parameters, "color_stop_list");
    if (pP0 == NULL || pP1 == NULL || pStops == NULL)
        return 0;

    Shader* pShader = new Shader();
    pShader->pRamp = NULL;
    pShader->iRampWidth = 256;
    pShader->p0 = pP0->Get<Rml::Vector2f>();
    pShader->p1 = pP1->Get<Rml::Vector2f>();
    pShader->bRepeating = (pRepeating != NULL) ? pRepeating->Get<bool>() : false;

    const Rml::ColorStopList stops = pStops->Get<Rml::ColorStopList>();
    if (stops.empty()) {
        delete pShader;
        return 0;
    }

    /// Bake the ramp. Stop positions arrive already resolved to 0..1 along the
    /// gradient line ( ResolveColorStops converts lengths and percentages ), so
    /// this is a straight piecewise-linear walk. Colours are premultiplied
    /// alpha, matching the blend state, so interpolating them directly is
    /// correct -- interpolating straight alpha here would darken the midpoints.
    pShader->RampPixels.resize((size_t)pShader->iRampWidth * 4);
    for (int i = 0; i < pShader->iRampWidth; ++i) {
        const float t = (float)i / (float)(pShader->iRampWidth - 1);

        size_t iNext = 0;
        while (iNext < stops.size() && stops[iNext].position.number < t)
            ++iNext;

        Rml::ColourbPremultiplied c;
        if (iNext == 0) {
            c = stops.front().color;
        } else if (iNext >= stops.size()) {
            c = stops.back().color;
        } else {
            const Rml::ColorStop& a = stops[iNext - 1];
            const Rml::ColorStop& b = stops[iNext];
            const float span = b.position.number - a.position.number;
            const float f = (span > 0.0f) ? ((t - a.position.number) / span) : 0.0f;
            c.red = (Rml::byte)(a.color.red + (b.color.red - a.color.red) * f);
            c.green = (Rml::byte)(a.color.green + (b.color.green - a.color.green) * f);
            c.blue = (Rml::byte)(a.color.blue + (b.color.blue - a.color.blue) * f);
            c.alpha = (Rml::byte)(a.color.alpha + (b.color.alpha - a.color.alpha) * f);
        }

        /// BGRA byte order for D3DFMT_A8R8G8B8.
        unsigned char* p = &pShader->RampPixels[(size_t)i * 4];
        p[0] = c.blue;
        p[1] = c.green;
        p[2] = c.red;
        p[3] = c.alpha;
    }

    if (!UploadRamp(*pShader)) {
        delete pShader;
        return 0;
    }

    const Rml::CompiledShaderHandle handle = m_NextShaderHandle++;
    m_Shaders[handle] = pShader;
    return handle;
}

void
RoseRmlRenderer::RenderShader(Rml::CompiledShaderHandle shader,
    Rml::CompiledGeometryHandle geometry,
    Rml::Vector2f translation,
    Rml::TextureHandle /*texture*/) {
    if (m_pDevice == NULL)
        return;

    std::map<Rml::CompiledShaderHandle, Shader*>::iterator itShader = m_Shaders.find(shader);
    std::map<Rml::CompiledGeometryHandle, Geometry*>::iterator itGeom = m_Geometries.find(geometry);
    if (itShader == m_Shaders.end() || itGeom == m_Geometries.end())
        return;

    Shader* pShader = itShader->second;
    Geometry* pGeom = itGeom->second;
    if (pShader->pRamp == NULL || pGeom->pVB == NULL || pGeom->pIB == NULL)
        return;

    D3DXMATRIX matWorld;
    D3DXMatrixTranslation(&matWorld, translation.x, translation.y, 0.0f);
    m_pDevice->SetTransform(D3DTS_WORLD, &matWorld);

    /// Project the element-local position carried in the texcoords onto the
    /// gradient axis:  t = dot(uv - p0, d) / |d|^2,  v = 0.5 ( ramp is 1 texel
    /// tall ). With D3DTTFF_COUNT2 the incoming coordinate is treated as
    /// (u, v, 1), so the constant term belongs in row 3.
    const Rml::Vector2f d = pShader->p1 - pShader->p0;
    const float fLenSq = d.x * d.x + d.y * d.y;
    if (fLenSq <= 0.0f)
        return;

    const float ax = d.x / fLenSq;
    const float ay = d.y / fLenSq;
    const float c = -(pShader->p0.x * d.x + pShader->p0.y * d.y) / fLenSq;

    D3DXMATRIX matTex;
    D3DXMatrixIdentity(&matTex);
    matTex._11 = ax;
    matTex._21 = ay;
    matTex._31 = c;
    matTex._12 = 0.0f;
    matTex._22 = 0.0f;
    matTex._32 = 0.5f;

    m_pDevice->SetTransform(D3DTS_TEXTURE0, &matTex);
    m_pDevice->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

    /// Repeating gradients tile the ramp; non-repeating clamp the end colours.
    const DWORD dwAddress = pShader->bRepeating ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP;
    m_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, dwAddress);
    m_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    m_pDevice->SetTexture(0, pShader->pRamp);
    m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);

    DrawGeometryRaw(*pGeom);

    /// Leave the pipeline as the plain-geometry path expects to find it.
    m_pDevice->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    m_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
}

void
RoseRmlRenderer::ReleaseShader(Rml::CompiledShaderHandle shader) {
    std::map<Rml::CompiledShaderHandle, Shader*>::iterator it = m_Shaders.find(shader);
    if (it == m_Shaders.end())
        return;

    if (it->second->pRamp != NULL)
        it->second->pRamp->Release();
    delete it->second;
    m_Shaders.erase(it);
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
