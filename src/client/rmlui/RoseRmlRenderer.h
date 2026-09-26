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
#include <d3dx9math.h>
#include <deque>
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

    /// resetScreen() destroys the D3D device and creates a NEW one, so the
    /// cached pointer must be replaced, not reused. Call after a rebuild.
    void SetDevice(IDirect3DDevice9* pDevice) {
        m_pDevice = pDevice;
    }
    IDirect3DDevice9* GetDevice() const {
        return m_pDevice;
    }

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

    /// CSS transform ( rotate, scale ... ): every later draw goes through it
    /// until RmlUi resets it with NULL. The minimap's heading arrow uses it.
    virtual void SetTransform(const Rml::Matrix4f* transform);

    /// --- optional: gradients -----------------------------------------------
    /// Implemented because CSS-authored skins should not need image files for
    /// something as basic as a gradient. Only `linear-gradient` is supported;
    /// radial and conic need a per-pixel function of position that the
    /// fixed-function pipeline cannot express as a coordinate transform.
    virtual Rml::CompiledShaderHandle CompileShader(const Rml::String& name,
        const Rml::Dictionary& parameters);
    virtual void RenderShader(Rml::CompiledShaderHandle shader,
        Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation,
        Rml::TextureHandle texture);
    virtual void ReleaseShader(Rml::CompiledShaderHandle shader);

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

    /// Work done since the last call, for the slow-frame report: geometry
    /// compiled, textures generated ( font glyph atlases and effects ) and
    /// gradients compiled, with their times.
    struct WorkStats {
        int iGeometries;
        double fGeometryMs;
        int iGenerated;
        double fGeneratedMs;
        int iShaders;
        double fShaderMs;
    };
    WorkStats TakeWorkStats() {
        WorkStats stats = m_Work;
        m_Work = WorkStats();
        return stats;
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

    /// CPU-side only, drawn with DrawIndexedPrimitiveUP ( the driver streams it
    /// through its own ring buffer ). A D3D vertex + index buffer per piece of
    /// geometry made a window's first draw create hundreds of them -- ~50 ms
    /// for the shop and the inventory -- and anything redrawn with new content
    /// every frame ( a cooldown curtain ) created and freed two buffers a
    /// frame. Nothing here dies with the device either.
    struct Geometry {
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
        /// A LoadTexture() whose decode is queued ( ProcessPendingTextures ):
        /// its size is known, its pixels are not, and it draws nothing yet.
        bool bPending;
        /// Loaded from a file in its own format ( DXT stays compressed ), so
        /// its alpha is STRAIGHT: the premultiply RmlUi expects is done by
        /// texture stage 1 at draw time ( SetStraightAlphaStage ).
        bool bStraightAlpha;
    };

    /// A compiled linear gradient: the colour ramp baked into a 1-D texture,
    /// plus the axis it is projected along. Ramp pixels are retained so the
    /// texture can be rebuilt after a device loss, exactly like Texture.
    struct Shader {
        IDirect3DTexture9* pRamp;
        std::vector<unsigned char> RampPixels;
        int iRampWidth;
        Rml::Vector2f p0;
        Rml::Vector2f p1;
        bool bRepeating;
    };

    bool ApplyRenderState();
    bool UploadTexture(Texture& tex, const unsigned char* pBGRA, int iWidth, int iHeight);
    bool ReloadTexture(Texture& tex);
    /// Stage 1 multiplies the colour by the texture's alpha for a straight-
    /// alpha texture, and is off for everything else.
    void SetStraightAlphaStage(IDirect3DTexture9* pStraight);
    /// Decodes queued LoadTexture()s for up to kDecodeBudgetMs ( at least one
    /// per frame ). Called from BeginFrame, before anything draws.
    void ProcessPendingTextures();
    bool UploadRamp(Shader& sh);
    void DrawGeometryRaw(const Geometry& geom);

    IDirect3DDevice9* m_pDevice;
    IDirect3DStateBlock9* m_pSavedState; ///< engine state captured at BeginFrame

    std::map<Rml::CompiledGeometryHandle, Geometry*> m_Geometries;
    std::map<Rml::TextureHandle, Texture*> m_Textures;
    /// LoadTexture()s waiting for their decode, oldest first. A handle released
    /// meanwhile is skipped ( handles are never reused ).
    std::deque<Rml::TextureHandle> m_PendingTextures;
    std::map<Rml::CompiledShaderHandle, Shader*> m_Shaders;
    Rml::CompiledGeometryHandle m_NextGeometryHandle;
    Rml::TextureHandle m_NextTextureHandle;
    Rml::CompiledShaderHandle m_NextShaderHandle;

    int m_iViewportWidth;
    int m_iViewportHeight;

    bool m_bScissorEnabled;
    RECT m_rcScissor;

    int m_iDrawCalls;
    WorkStats m_Work;
    bool m_bDeviceObjectsValid;

    /// The active CSS transform ( SetTransform ), applied after each draw's
    /// translation.
    bool m_bTransform;
    D3DXMATRIX m_matTransform;

    /// The world matrix for one draw: its translation, then the transform.
    void SetWorld(Rml::Vector2f translation);
};

#endif /// _ROSE_RML_RENDERER_H_
