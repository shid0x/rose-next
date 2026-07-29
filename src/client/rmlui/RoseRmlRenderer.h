#ifndef _ROSE_RML_RENDERER_H_
#define _ROSE_RML_RENDERER_H_

/**
 * Direct3D 9(Ex) render interface for RmlUi.
 *
 * Spike 1 scope: the eight mandatory RenderInterface functions only -- textured /
 * untextured triangles, alpha blending, rectangular scissoring, textures. The
 * optional feature set ( clip masks, layers, filters, shaders ) is intentionally
 * left on the base-class no-op implementations; see doc/rmlui-evaluation.md.
 *
 * Deliberately synchronous: every RenderGeometry() call issues its own draw. Do
 * not add batching until the 3D-pane element ( Spike 2 ) is proven, because the
 * avatar viewport render has to be able to interleave at an exact point in the
 * command stream.
 */

#include <RmlUi/Core/RenderInterface.h>

#include <d3d9.h>
#include <map>
#include <string>
#include <vector>

class RoseRmlRenderer: public Rml::RenderInterface {
public:
    RoseRmlRenderer();
    virtual ~RoseRmlRenderer();

    /// Binds the device and builds the device-dependent state. Safe to call again
    /// after a device rebuild.
    bool Initialise(IDirect3DDevice9* pDevice);
    void Shutdown();

    /// --- device lifetime ---------------------------------------------------
    /// D3DPOOL_DEFAULT resources do not survive a device reset. The client still
    /// rebuilds the device on a windowed frame drag ( ApplyWindowedClientResize ),
    /// so both halves must be wired even on a 9Ex device.
    void ReleaseDeviceObjects();
    bool CreateDeviceObjects();

    /// Screen size drives the orthographic projection.
    void SetViewportSize(int iWidth, int iHeight);

    /// --- mandatory RenderInterface surface ---------------------------------
    virtual Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
        Rml::Span<const int> indices);
    virtual void RenderGeometry(Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation,
        Rml::TextureHandle texture);
    virtual void ReleaseGeometry(Rml::CompiledGeometryHandle geometry);

    virtual Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions,
        const Rml::String& source);
    virtual Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
        Rml::Vector2i source_dimensions);
    virtual void ReleaseTexture(Rml::TextureHandle texture);

    virtual void EnableScissorRegion(bool enable);
    virtual void SetScissorRegion(Rml::Rectanglei region);

    /// --- pass framing -------------------------------------------------------
    /// BeginFrame captures the engine's device state and installs ours;
    /// EndFrame puts the engine's state back exactly as it was. The 3D pane
    /// element ( Spike 2 ) reuses these through the guard below.
    void BeginFrame();
    void EndFrame();

    /// Hands the current scissor rect to a custom element that is about to take
    /// over the device ( the model-preview pane ). Returns false when the element
    /// is fully clipped and should skip rendering entirely.
    bool GetEffectiveScissor(const RECT& rcElement, RECT& rcOut) const;

    int GetDrawCallCount() const {
        return m_iDrawCalls;
    }
    void ResetStats() {
        m_iDrawCalls = 0;
    }

private:
    /// UI vertex: position is 2D but kept as XYZ so the world matrix can carry
    /// RmlUi's per-geometry translation ( and, later, SetTransform ).
    struct Vertex {
        float x, y, z;
        DWORD color;
        float u, v;
    };
    enum { kFVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 };

    struct Geometry {
        IDirect3DVertexBuffer9* pVB;
        IDirect3DIndexBuffer9* pIB;
        /// CPU-side copies are retained on purpose: RmlUi will NOT re-request
        /// compiled geometry after a device rebuild, so we have to be able to
        /// refill the buffers ourselves.
        std::vector<Vertex> Vertices;
        std::vector<unsigned short> Indices;
        int iNumVerts;
        int iNumIndices;
    };

    struct Texture {
        IDirect3DTexture9* pTexture;
        int iWidth;
        int iHeight;
        /// Non-empty for LoadTexture()-sourced textures so they can be reloaded.
        std::string strSource;
        /// Retained pixels for GenerateTexture()-sourced textures ( font atlases ).
        std::vector<unsigned char> Pixels;
    };

    bool ApplyRenderState();
    bool UploadTexture(Texture& tex, const unsigned char* pBGRA, int iWidth, int iHeight);
    bool ReloadTexture(Texture& tex);

    IDirect3DDevice9* m_pDevice;
    IDirect3DStateBlock9* m_pSavedState; ///< engine state captured at BeginFrame

    std::map<Rml::CompiledGeometryHandle, Geometry*> m_Geometries;
    std::map<Rml::TextureHandle, Texture*> m_Textures;
    Rml::CompiledGeometryHandle m_NextGeometryHandle;
    Rml::TextureHandle m_NextTextureHandle;

    int m_iViewportWidth;
    int m_iViewportHeight;

    bool m_bScissorEnabled;
    RECT m_rcScissor;

    int m_iDrawCalls;
    bool m_bDeviceObjectsValid;
};

#endif /// _ROSE_RML_RENDERER_H_
