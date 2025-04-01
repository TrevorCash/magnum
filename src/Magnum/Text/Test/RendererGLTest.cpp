/*
    This file is part of Magnum.

    Copyright © 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019,
                2020, 2021, 2022, 2023, 2024, 2025
              Vladimír Vondruš <mosra@centrum.cz>

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#include <string>
#include <tuple>
#include <Corrade/Containers/Array.h>
#include <Corrade/Containers/Optional.h>
#include <Corrade/Containers/StridedArrayView.h>
#include <Corrade/Containers/String.h>
#include <Corrade/Containers/StringView.h>
#include <Corrade/PluginManager/Manager.h>
#include <Corrade/TestSuite/Compare/Container.h>
#include <Corrade/Utility/Path.h>

#include "Magnum/Image.h"
#include "Magnum/ImageView.h"
#include "Magnum/Mesh.h"
#include "Magnum/PixelFormat.h"
#include "Magnum/Math/Color.h"
#include "Magnum/Math/Matrix3.h"
#include "Magnum/DebugTools/CompareImage.h"
#include "Magnum/GL/Context.h"
#include "Magnum/GL/Extensions.h"
#include "Magnum/GL/Framebuffer.h"
#include "Magnum/GL/OpenGLTester.h"
#include "Magnum/GL/Renderbuffer.h"
#include "Magnum/GL/RenderbufferFormat.h"
#include "Magnum/Shaders/VectorGL.h"
#include "Magnum/Text/AbstractFont.h"
#include "Magnum/Text/AbstractShaper.h"
#include "Magnum/Text/GlyphCacheGL.h"
#include "Magnum/Text/RendererGL.h"
#include "Magnum/Trade/AbstractImporter.h"

#include "configure.h"

namespace Magnum { namespace Text { namespace Test { namespace {

struct RendererGLTest: GL::OpenGLTester {
    explicit RendererGLTest();

    void construct();
    #ifdef MAGNUM_TARGET_GLES2
    void constructArrayGlyphCacheNotSupported();
    #endif
    void constructCopy();
    void constructMove();

    /* Compared to Renderer & RendererCore, RendererGL doesn't expose any extra
       getters / setters for a properties() test */

    void renderSetup();
    void renderTeardown();
    void renderClearReset();
    void renderIndexTypeChanged();

    void renderMesh();
    void renderMeshIndexType();
    void mutableText();

    void arrayGlyphCache();
    void fontNotFoundInCache();

    private:
        PluginManager::Manager<Trade::AbstractImporter> _manager{"nonexistent"};

        GL::Renderbuffer _color{NoCreate};
        GL::Framebuffer _framebuffer{NoCreate};
};

using namespace Containers::Literals;
using namespace Math::Literals;

const struct {
    const char* name;
    Containers::Optional<MeshIndexType> indexType;
    Int glyphCacheArraySize;
    RendererGLFlags flags;
    MeshIndexType expectedIndexType;
} ConstructData[]{
    {"",
        {}, 1, {}, MeshIndexType::UnsignedShort},
    {"UnsignedByte indices",
        MeshIndexType::UnsignedByte, 1, {}, MeshIndexType::UnsignedByte},
    {"UnsignedInt indices",
        MeshIndexType::UnsignedInt, 1, {}, MeshIndexType::UnsignedInt},
    {"glyph positions and clusters",
        {}, 1, RendererGLFlag::GlyphPositionsClusters, MeshIndexType::UnsignedShort},
    #ifndef MAGNUM_TARGET_GLES2
    {"array glyph cache",
        {}, 5, {}, MeshIndexType::UnsignedShort},
    {"array glyph cache, glyph positions and clusters",
        {}, 5, RendererGLFlag::GlyphPositionsClusters, MeshIndexType::UnsignedShort},
    #endif
};

const struct {
    const char* name;
    Int glyphCacheArraySize;
    RendererGLFlags flags;
    UnsignedInt reserveBefore, reserveAfter;
    Containers::Optional<MeshIndexType> indexTypeBefore, indexTypeAfter;
    bool clear, reset;
    MeshIndexType expectedIndexType;
} RenderClearResetData[]{
    {"",
        1, {}, 0, 0, {}, {}, false, false,
        MeshIndexType::UnsignedShort},
    {"glyph positions and clusters",
        1, RendererGLFlag::GlyphPositionsClusters, 0, 0, {}, {}, false, false, MeshIndexType::UnsignedShort},
    #ifndef MAGNUM_TARGET_GLES2
    {"array glyph cache",
        5, {}, 0, 0, {}, {}, false, false,
        MeshIndexType::UnsignedShort},
    {"array glyph cache, glyph positions and clusters",
        5, RendererGLFlag::GlyphPositionsClusters, 0, 0, {}, {}, false, false,
        MeshIndexType::UnsignedShort},
    #endif
    /* These test just index buffer generation, so no cache- or glyph-related
       variants */
    {"UnsignedByte indices",
        1, {}, 0, 0, MeshIndexType::UnsignedByte, {}, false, false,
        MeshIndexType::UnsignedByte},
    {"explicit default UnsignedShort indices",
        1, {}, 0, 0, MeshIndexType::UnsignedShort, {}, false, false,
        MeshIndexType::UnsignedShort},
    {"UnsignedInt indices",
        1, {}, 0, 0, MeshIndexType::UnsignedInt, {}, false, false,
        MeshIndexType::UnsignedInt},
    {"reserve exactly upfront",
        1, {}, 5, 0, {}, {}, false, false,
        MeshIndexType::UnsignedShort},
    {"reserve partially upfront",
        1, {}, 3, 0, {}, {}, false, false,
        MeshIndexType::UnsignedShort},
    {"reserve more upfront",
        1, {}, 16385, 0, {}, {}, false, false,
        MeshIndexType::UnsignedInt},
    {"reserve again after render with the same",
        1, {}, 0, 5, {}, {}, false, false,
        MeshIndexType::UnsignedShort},
    {"reserve again after render with less",
        1, {}, 0, 3, {}, {}, false, false,
        MeshIndexType::UnsignedShort},
    {"reserve again after render with more",
        /* Reserve a bigger size to ensure it doesn't get aliased with the old
           memory, preserving the original contents by accident and hiding a
           potential bug where it doesn't get correctly reuploaded */
        1, {}, 0, 1024*1024, {}, {}, false, false,
        MeshIndexType::UnsignedInt},
    {"reserve all upfront and then change indices to UnsignedByte",
        1, {}, 5, 0, MeshIndexType::UnsignedByte, {}, false, false,
        MeshIndexType::UnsignedByte},
    {"reserve all upfront and then explicitly use default UnsignedShort indices",
        1, {}, 5, 0, MeshIndexType::UnsignedShort, {}, false, false,
        MeshIndexType::UnsignedShort},
    {"reserve all upfront and then change indices to UnsignedInt",
        1, {}, 5, 0, MeshIndexType::UnsignedInt, {}, false, false,
        MeshIndexType::UnsignedInt},
    {"change indices to UnsignedByte after render",
        1, {}, 0, 0, {}, MeshIndexType::UnsignedByte, false, false,
        MeshIndexType::UnsignedByte},
    {"explicitly set default UnsignedShort after render",
        1, {}, 0, 0, {}, MeshIndexType::UnsignedShort, false, false,
        MeshIndexType::UnsignedShort},
    {"change indices to UnsignedInt after render",
        1, {}, 0, 0, {}, MeshIndexType::UnsignedInt, false, false,
        MeshIndexType::UnsignedInt},
    {"clear and rerender",
        1, {}, 0, 0, {}, {}, true, false,
        MeshIndexType::UnsignedShort},
    {"clear and rerender, UnsignedInt indices",
        1, {}, 0, 0, MeshIndexType::UnsignedInt, {}, true, false,
        MeshIndexType::UnsignedInt},
    {"reset and rerender",
        1, {}, 0, 0, {}, {}, true, true,
        MeshIndexType::UnsignedShort},
};

RendererGLTest::RendererGLTest() {
    addInstancedTests({&RendererGLTest::construct},
        Containers::arraySize(ConstructData));

    #ifdef MAGNUM_TARGET_GLES2
    addTests({&RendererGLTest::constructArrayGlyphCacheNotSupported});
    #endif

    addTests({&RendererGLTest::constructCopy,
              &RendererGLTest::constructMove});

    addInstancedTests({&RendererGLTest::renderClearReset},
        Containers::arraySize(RenderClearResetData),
        &RendererGLTest::renderSetup,
        &RendererGLTest::renderTeardown);

    addTests({&RendererGLTest::renderIndexTypeChanged},
        &RendererGLTest::renderSetup,
        &RendererGLTest::renderTeardown);

    addTests({&RendererGLTest::renderMesh,
              &RendererGLTest::renderMeshIndexType,
              &RendererGLTest::mutableText,

              &RendererGLTest::arrayGlyphCache,
              &RendererGLTest::fontNotFoundInCache});

    /* Load the plugins directly from the build tree. Otherwise they're either
       static and already loaded or not present in the build tree */
    #ifdef ANYIMAGEIMPORTER_PLUGIN_FILENAME
    CORRADE_INTERNAL_ASSERT_OUTPUT(_manager.load(ANYIMAGEIMPORTER_PLUGIN_FILENAME) & PluginManager::LoadState::Loaded);
    #endif
    #ifdef TGAIMPORTER_PLUGIN_FILENAME
    CORRADE_INTERNAL_ASSERT_OUTPUT(_manager.load(TGAIMPORTER_PLUGIN_FILENAME) & PluginManager::LoadState::Loaded);
    #endif
}

void RendererGLTest::construct() {
    auto&& data = ConstructData[testCaseInstanceId()];
    setTestCaseDescription(data.name);

    struct: AbstractGlyphCache {
        using AbstractGlyphCache::AbstractGlyphCache;

        GlyphCacheFeatures doFeatures() const override { return {}; }
    } glyphCache{PixelFormat::R8Unorm, {16, 16, data.glyphCacheArraySize}};

    RendererGL renderer{glyphCache, data.flags};
    if(data.indexType)
        renderer.setIndexType(*data.indexType);

    MAGNUM_VERIFY_NO_GL_ERROR();

    CORRADE_COMPARE(renderer.flags(), data.flags);
    CORRADE_COMPARE(renderer.indexType(), data.expectedIndexType);
    CORRADE_COMPARE(renderer.mesh().count(), 0);
    CORRADE_COMPARE(renderer.mesh().indexType(), GL::meshIndexType(data.expectedIndexType));
}

#ifdef MAGNUM_TARGET_GLES2
void RendererGLTest::constructArrayGlyphCacheNotSupported() {
    CORRADE_SKIP_IF_NO_ASSERT();

    struct: AbstractGlyphCache {
        using AbstractGlyphCache::AbstractGlyphCache;

        GlyphCacheFeatures doFeatures() const override { return {}; }
    } glyphCache{PixelFormat::R8Unorm, {16, 16, 5}};

    Containers::String out;
    Error redirectError{&out};
    RendererGL{glyphCache};
    CORRADE_COMPARE(out, "Text::RendererGL: array glyph caches are not supported in OpenGL ES 2.0 and WebGL 1 builds\n");
}
#endif

void RendererGLTest::constructCopy() {
    CORRADE_VERIFY(!std::is_copy_constructible<RendererGL>{});
    CORRADE_VERIFY(!std::is_copy_assignable<RendererGL>{});
}

void RendererGLTest::constructMove() {
    struct: AbstractGlyphCache {
        using AbstractGlyphCache::AbstractGlyphCache;

        GlyphCacheFeatures doFeatures() const override { return {}; }
    } glyphCache{PixelFormat::R8Unorm, {16, 16}},
        anotherGlyphCache{PixelFormat::RGBA8Unorm, {4, 4}};

    /* Verify that both the Renderer and the RendererGL state is
       transferred */
    RendererGL a{glyphCache, RendererGLFlags{0x80}};
    UnsignedInt meshId = a.mesh().id();
    a.setIndexType(MeshIndexType::UnsignedInt);

    RendererGL b = Utility::move(a);
    CORRADE_COMPARE(&b.glyphCache(), &glyphCache);
    CORRADE_COMPARE(b.flags(), RendererGLFlags{0x80});
    CORRADE_COMPARE(b.indexType(), MeshIndexType::UnsignedInt);
    CORRADE_COMPARE(b.mesh().id(), meshId);

    RendererGL c{anotherGlyphCache};
    c = Utility::move(b);
    CORRADE_COMPARE(&c.glyphCache(), &glyphCache);
    CORRADE_COMPARE(c.flags(), RendererGLFlags{0x80});
    CORRADE_COMPARE(c.indexType(), MeshIndexType::UnsignedInt);
    CORRADE_COMPARE(c.mesh().id(), meshId);

    CORRADE_VERIFY(std::is_nothrow_move_constructible<RendererGL>::value);
    CORRADE_VERIFY(std::is_nothrow_move_assignable<RendererGL>::value);
}

constexpr Vector2i RenderSize{8, 8};

void RendererGLTest::renderSetup() {
    /* Pick a color that's directly representable on RGBA4 as well to reduce
       artifacts */
    GL::Renderer::setClearColor(0x111111_rgbf);
    GL::Renderer::enable(GL::Renderer::Feature::FaceCulling);

    _color = GL::Renderbuffer{};
    _color.setStorage(
        #if !defined(MAGNUM_TARGET_GLES2) || !defined(MAGNUM_TARGET_WEBGL)
        GL::RenderbufferFormat::RGBA8,
        #else
        GL::RenderbufferFormat::RGBA4,
        #endif
        RenderSize);
    _framebuffer = GL::Framebuffer{{{}, RenderSize}};
    _framebuffer
        .attachRenderbuffer(GL::Framebuffer::ColorAttachment{0}, _color)
        .clear(GL::FramebufferClear::Color)
        .bind();
}

void RendererGLTest::renderTeardown() {
    _framebuffer = GL::Framebuffer{NoCreate};
    _color = GL::Renderbuffer{NoCreate};
}

void RendererGLTest::renderClearReset() {
    auto&& data = RenderClearResetData[testCaseInstanceId()];
    setTestCaseDescription(data.name);

    /* Override the default padding to zero to make testing easier, also use
       nearest neighbor filtering for predictable output */
    GlyphCacheGL cache{NoCreate};
    #ifndef MAGNUM_TARGET_GLES2
    GlyphCacheArrayGL cacheArray{NoCreate};
    if(data.glyphCacheArraySize != 1) {
        cacheArray = GlyphCacheArrayGL{PixelFormat::R8Unorm, {2, 2, data.glyphCacheArraySize}, {}};
        cacheArray.texture()
            .setMinificationFilter(SamplerFilter::Nearest)
            .setMagnificationFilter(SamplerFilter::Nearest);
    } else
    #endif
    {
        cache = GlyphCacheGL{PixelFormat::R8Unorm, {2, 2}, {}};
        cache.texture()
            .setMinificationFilter(SamplerFilter::Nearest)
            .setMagnificationFilter(SamplerFilter::Nearest);
    }
    /* For type-independent access below */
    AbstractGlyphCache& glyphCache =
        #ifndef MAGNUM_TARGET_GLES2
        data.glyphCacheArraySize != 1 ? static_cast<AbstractGlyphCache&>(cacheArray) :
        #endif
            cache;

    Shaders::VectorGL2D shader{Shaders::VectorGL2D::Configuration{}
        #ifndef MAGNUM_TARGET_GLES2
        .setFlags(data.glyphCacheArraySize != 1 ?
            Shaders::VectorGL2D::Flag::TextureArrays :
            Shaders::VectorGL2D::Flags{})
        #endif
    };
    shader.setTransformationProjectionMatrix(Matrix3::projection(Vector2{RenderSize}));
    #ifndef MAGNUM_TARGET_GLES2
    if(data.glyphCacheArraySize != 1) {
        shader.bindVectorTexture(cacheArray.texture());
    } else
    #endif
    {
        shader.bindVectorTexture(cache.texture());
    }

    struct: AbstractFont {
        FontFeatures doFeatures() const override { return {}; }
        bool doIsOpened() const override { return _opened; }
        void doClose() override { _opened = false; }

        Properties doOpenFile(Containers::StringView, Float) override {
            _opened = true;
            /* The size is used to scale advances, ascent & descent is used to
               align the block. Line height is used for multi-line text which
               we don't test here, glyph count is overriden in addFont()
               below. */
            return {1.0f, 2.0f, -1.0f, 10000.0f, 0};
        }

        void doGlyphIdsInto(const Containers::StridedArrayView1D<const char32_t>&, const Containers::StridedArrayView1D<UnsignedInt>&) override {}
        Vector2 doGlyphSize(UnsignedInt) override { return {}; }
        Vector2 doGlyphAdvance(UnsignedInt) override { return {}; }
        Containers::Pointer<AbstractShaper> doCreateShaper() override { return {}; }

        private:
            bool _opened = false;
    } font;
    font.openFile("", 1.0f);

    UnsignedInt fontId = glyphCache.addFont(4, &font);
    /* Shuffled order to not have their IDs match the clusters, other than that
       the simplest possible contents to make it easy to verify that the data
       get correctly uploaded. All corner cases are verified in RendererTest
       already. */
    glyphCache.addGlyph(fontId, 3, {},          /* c, white square */
        data.glyphCacheArraySize/2,
        Range2Di::fromSize({1, 0}, {1, 1}));
    glyphCache.addGlyph(fontId, 1, {},          /* a / d, light gray square */
        data.glyphCacheArraySize - 1,
        Range2Di::fromSize({1, 1}, {1, 1}));
    glyphCache.addGlyph(fontId, 2, {},          /* b / e, dark gray rect */
        0,
        Range2Di::fromSize({0, 0}, {1, 2}));
    {
        const Containers::StridedArrayView3D<UnsignedByte> pixels = glyphCache.image().pixels<UnsignedByte>();
        pixels[data.glyphCacheArraySize/2][0][1] = 0xff;
        pixels[0][0][0] = 0x33;
        pixels[0][1][0] = 0x33;
        pixels[data.glyphCacheArraySize - 1][1][1] = 0x99;
    }
    glyphCache.flushImage({{}, glyphCache.size()});

    MAGNUM_VERIFY_NO_GL_ERROR();

    struct: AbstractShaper {
        using AbstractShaper::AbstractShaper;

        UnsignedInt doShape(Containers::StringView, UnsignedInt begin, UnsignedInt end, Containers::ArrayView<const FeatureRange>) override {
            return end - begin;
        }
        void doGlyphIdsInto(const Containers::StridedArrayView1D<UnsignedInt>& ids) const override {
            for(UnsignedInt i = 0; i != ids.size(); ++i)
                ids[i] = i + 1;
        }
        void doGlyphOffsetsAdvancesInto(const Containers::StridedArrayView1D<Vector2>& offsets, const Containers::StridedArrayView1D<Vector2>& advances) const override {
            /* Just the simplest possible, rigorously tested in RendererTest
               already */
            for(UnsignedInt i = 0; i != offsets.size(); ++i) {
                advances[i] = {2.0f, 0.0f};
                offsets[i] = {0.0f, i == 2 ? 1.0f : 0.0f};
            }
        }
        void doGlyphClustersInto(const Containers::StridedArrayView1D<UnsignedInt>& clusters) const override {
            /* Just to verify that the clusters get populated with meaningful
               data */
            for(UnsignedInt i = 0; i != clusters.size(); ++i)
                clusters[i] = 10 + i*3;
        }
    } shaper{font};

    RendererGL renderer{glyphCache, data.flags};

    /* Rendering with nothing inside should result in nothing */
    shader.draw(renderer.mesh());

    MAGNUM_VERIFY_NO_GL_ERROR();

    if(!(_manager.loadState("AnyImageImporter") & PluginManager::LoadState::Loaded) ||
        !(_manager.loadState("TgaImporter") & PluginManager::LoadState::Loaded))
        CORRADE_SKIP("AnyImageImporter / TgaImporter plugins not found.");

    CORRADE_COMPARE_WITH(
        /* Use just one channel, the others are always the same */
        _framebuffer.read(_framebuffer.viewport(), {PixelFormat::RGBA8Unorm}).pixels<Color4ub>().slice(&Color4ub::r),
        Utility::Path::join(TEXT_TEST_DIR, "render-nothing.tga"),
        (DebugTools::CompareImageToFile{_manager, 0.0f, 0.0f}));

    /* This uploads indices if called */
    if(data.reserveBefore) {
        renderer.reserve(data.reserveBefore, 0);
        MAGNUM_VERIFY_NO_GL_ERROR();
        CORRADE_COMPARE(renderer.mesh().count(), 0);
    }

    /* This may reupload indices if reserve() was called */
    if(data.indexTypeBefore) {
        renderer.setIndexType(*data.indexTypeBefore);
        MAGNUM_VERIFY_NO_GL_ERROR();
        CORRADE_COMPARE(renderer.indexType(), *data.indexTypeBefore);
        CORRADE_COMPARE(renderer.mesh().count(), 0);
        CORRADE_COMPARE(renderer.mesh().indexType(), GL::meshIndexType(*data.indexTypeBefore));
    }

    for(Int i = 0, iMax = 1 + (data.reset || data.clear); i != iMax; ++i) {
        if(data.clear)
            CORRADE_ITERATION("after clear");
        else if(data.reset)
            CORRADE_ITERATION("after reset");

        /* This uploads indices if reserve() wasn't called */
        renderer
            .setAlignment(Alignment::LineLeft)
            .setCursor({-3.0f, 1.0f})
            .render(shaper, 1.0f, "abc");

        /* This uploads indices if reserve() wasn't called or was too little */
        renderer
            .setAlignment(Alignment::LineRight)
            .setCursor({5.0f, -3.0f})
            .render(shaper, 2.0f, "de");

        /* This may reupload indices if called */
        if(data.indexTypeAfter) {
            renderer.setIndexType(*data.indexTypeAfter);
            MAGNUM_VERIFY_NO_GL_ERROR();
            CORRADE_COMPARE(renderer.indexType(), *data.indexTypeAfter);
            CORRADE_COMPARE(renderer.mesh().count(), 5*6);
            CORRADE_COMPARE(renderer.mesh().indexType(), GL::meshIndexType(*data.indexTypeAfter));
        }

        /* This may reupload indices and vertices if called */
        if(data.reserveAfter) {
            renderer.reserve(data.reserveAfter, 0);
            MAGNUM_VERIFY_NO_GL_ERROR();
            CORRADE_COMPARE(renderer.mesh().count(), 5*6);
        }

        MAGNUM_VERIFY_NO_GL_ERROR();
        CORRADE_COMPARE(renderer.indexType(), data.expectedIndexType);
        CORRADE_COMPARE(renderer.mesh().count(), 5*6);
        CORRADE_COMPARE(renderer.mesh().indexType(), GL::meshIndexType(data.expectedIndexType));

        /* If glyph positions and clusters are enabled, verify they got filled as
           well. Again just to be sure that the operation is done at all,
           thoroughly tested in RendererTest already. */
        if(data.flags & RendererGLFlag::GlyphPositionsClusters) {
            CORRADE_COMPARE_AS(renderer.glyphPositions(), Containers::arrayView<Vector2>({
                {-3.0f,  1.0f}, /* a */
                {-1.0f,  1.0f}, /* b */
                { 1.0f,  2.0f}, /* c */

                {-3.0f, -3.0f}, /* d */
                { 1.0f, -3.0f}, /* e */
            }), TestSuite::Compare::Container);
            CORRADE_COMPARE_AS(renderer.glyphClusters(), Containers::arrayView({
                10u, 13u, 16u,
                10u, 13u
            }), TestSuite::Compare::Container);
        }

        /* Verify the index and vertex data are generated as expected */
        if(data.expectedIndexType == MeshIndexType::UnsignedByte)
            CORRADE_COMPARE_AS(renderer.indices<UnsignedByte>(), Containers::arrayView<UnsignedByte>({
                0, 1, 2, 2, 1, 3,
                4, 5, 6, 6, 5, 7,
                8, 9, 10, 10, 9, 11,
                12, 13, 14, 14, 13, 15,
                16, 17, 18, 18, 17, 19
            }), TestSuite::Compare::Container);
        else if(data.expectedIndexType == MeshIndexType::UnsignedShort)
            CORRADE_COMPARE_AS(renderer.indices<UnsignedShort>(), Containers::arrayView<UnsignedShort>({
                0, 1, 2, 2, 1, 3,
                4, 5, 6, 6, 5, 7,
                8, 9, 10, 10, 9, 11,
                12, 13, 14, 14, 13, 15,
                16, 17, 18, 18, 17, 19
            }), TestSuite::Compare::Container);
        else if(data.expectedIndexType == MeshIndexType::UnsignedInt)
            CORRADE_COMPARE_AS(renderer.indices<UnsignedInt>(), Containers::arrayView<UnsignedInt>({
                0, 1, 2, 2, 1, 3,
                4, 5, 6, 6, 5, 7,
                8, 9, 10, 10, 9, 11,
                12, 13, 14, 14, 13, 15,
                16, 17, 18, 18, 17, 19
            }), TestSuite::Compare::Container);
        else CORRADE_INTERNAL_ASSERT_UNREACHABLE();

        CORRADE_COMPARE_AS(renderer.vertexPositions(), Containers::arrayView<Vector2>({
            {-3.0f,  1.0f}, /* a */
            {-2.0f,  1.0f},
            {-3.0f,  2.0f},
            {-2.0f,  2.0f},

            {-1.0f,  1.0f}, /* b, rect */
            { 0.0f,  1.0f},
            {-1.0f,  3.0f},
            { 0.0f,  3.0f},

            { 1.0f,  2.0f}, /* c */
            { 2.0f,  2.0f},
            { 1.0f,  3.0f},
            { 2.0f,  3.0f},

            {-3.0f, -3.0f}, /* d */
            {-1.0f, -3.0f},
            {-3.0f, -1.0f},
            {-1.0f, -1.0f},

            { 1.0f, -3.0f}, /* e, rect */
            { 3.0f, -3.0f},
            { 1.0f,  1.0f},
            { 3.0f,  1.0f},
        }), TestSuite::Compare::Container);

        Vector3 expectedTextureCoordinates[]{
            {0.5f, 0.5f, Float(data.glyphCacheArraySize - 1)}, /* a */
            {1.0f, 0.5f, Float(data.glyphCacheArraySize - 1)},
            {0.5f, 1.0f, Float(data.glyphCacheArraySize - 1)},
            {1.0f, 1.0f, Float(data.glyphCacheArraySize - 1)},

            {0.0f, 0.0f, 0.0f}, /* b, rect */
            {0.5f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.5f, 1.0f, 0.0f},

            {0.5f, 0.0f, Float(data.glyphCacheArraySize/2)}, /* c */
            {1.0f, 0.0f, Float(data.glyphCacheArraySize/2)},
            {0.5f, 0.5f, Float(data.glyphCacheArraySize/2)},
            {1.0f, 0.5f, Float(data.glyphCacheArraySize/2)},

            {0.5f, 0.5f, Float(data.glyphCacheArraySize - 1)}, /* d; same as a */
            {1.0f, 0.5f, Float(data.glyphCacheArraySize - 1)},
            {0.5f, 1.0f, Float(data.glyphCacheArraySize - 1)},
            {1.0f, 1.0f, Float(data.glyphCacheArraySize - 1)},

            {0.0f, 0.0f, 0.0f}, /* e, rect; same as b */
            {0.5f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.5f, 1.0f, 0.0f},
        };
        #ifndef MAGNUM_TARGET_GLES2
        if(data.glyphCacheArraySize != 1) {
            CORRADE_COMPARE_AS(renderer.vertexTextureArrayCoordinates(),
                Containers::arrayView(expectedTextureCoordinates),
                TestSuite::Compare::Container);
        } else
        #endif
        {
            CORRADE_COMPARE_AS(renderer.vertexTextureCoordinates(),
                Containers::stridedArrayView(expectedTextureCoordinates).slice(&Vector3::xy),
                TestSuite::Compare::Container);
        }

        /* Draw the generated mesh */
        _framebuffer.clear(GL::FramebufferClear::Color);
        shader.draw(renderer.mesh());

        MAGNUM_VERIFY_NO_GL_ERROR();

        /* light gray square, dark gray rect, white square on top left
           large light gray square, large dark gray rect on bottom right */
        CORRADE_COMPARE_WITH(
            /* Use just one channel, the others are always the same */
            _framebuffer.read(_framebuffer.viewport(), {PixelFormat::RGBA8Unorm}).pixels<Color4ub>().slice(&Color4ub::r),
            Utility::Path::join(TEXT_TEST_DIR, "render.tga"),
            (DebugTools::CompareImageToFile{_manager, 0.0f, 0.0f}));

        /* If resetting or clearing, there's another iteration of all above.
           Verify that it calls correct parent reset or clear by checking
           whether the cursor stays as before or not. */
        if(data.reset || data.clear) {
            if(data.reset) {
                renderer.reset();
                CORRADE_COMPARE(renderer.mesh().count(), 0);
                CORRADE_COMPARE(renderer.cursor(), Vector2{});
            } else {
                renderer.clear();
                CORRADE_COMPARE(renderer.cursor(), (Vector2{5.0f, -3.0f}));
            }

            /* The index type should stay even after clear / reset */
            CORRADE_COMPARE(renderer.indexType(), data.expectedIndexType);
            CORRADE_COMPARE(renderer.mesh().count(), 0);
            CORRADE_COMPARE(renderer.mesh().indexType(), GL::meshIndexType(data.expectedIndexType));

            /* Rendering after a reset or clear should result in nothing
               again */
            _framebuffer.clear(GL::FramebufferClear::Color);
            shader.draw(renderer.mesh());

            MAGNUM_VERIFY_NO_GL_ERROR();

            CORRADE_COMPARE_WITH(
                /* Use just one channel, the others are always the same */
                _framebuffer.read(_framebuffer.viewport(), {PixelFormat::RGBA8Unorm}).pixels<Color4ub>().slice(&Color4ub::r),
                Utility::Path::join(TEXT_TEST_DIR, "render-nothing.tga"),
                (DebugTools::CompareImageToFile{_manager, 0.0f, 0.0f}));
        }
    }

    /* Clearing twice in a row should not result in anything different */
    if(data.reset || data.clear) {
        if(data.reset) {
            renderer.reset();
            CORRADE_COMPARE(renderer.mesh().count(), 0);
            CORRADE_COMPARE(renderer.cursor(), Vector2{});
        } else {
            renderer.clear();
            CORRADE_COMPARE(renderer.cursor(), (Vector2{5.0f, -3.0f}));
        }

        /* The index type should stay even after clear / reset */
        CORRADE_COMPARE(renderer.indexType(), data.expectedIndexType);
        CORRADE_COMPARE(renderer.mesh().count(), 0);
        CORRADE_COMPARE(renderer.mesh().indexType(), GL::meshIndexType(data.expectedIndexType));
    }
}

void RendererGLTest::renderIndexTypeChanged() {
    /* Verifies that an index type change happening inside render() due to
       there being too many glyphs is correctly propagated to the GL mesh. A
       trimmed-down version of renderClearReset() that verifies just the image
       output, because that's the only place where it can be detected. */

    GlyphCacheGL glyphCache{PixelFormat::R8Unorm, {2, 2}, {}};
    glyphCache.texture()
        .setMinificationFilter(SamplerFilter::Nearest)
        .setMagnificationFilter(SamplerFilter::Nearest);

    struct: AbstractFont {
        FontFeatures doFeatures() const override { return {}; }
        bool doIsOpened() const override { return _opened; }
        void doClose() override { _opened = false; }

        Properties doOpenFile(Containers::StringView, Float) override {
            _opened = true;
            /* Compared to renderClearReset(), the line height is 0 so we can
               render the 256 glyph prefix on the same spot without having to
               adjust the cursor to place the next line correctly */
            return {1.0f, 2.0f, -1.0f, 0.0f, 0};
        }

        void doGlyphIdsInto(const Containers::StridedArrayView1D<const char32_t>&, const Containers::StridedArrayView1D<UnsignedInt>&) override {}
        Vector2 doGlyphSize(UnsignedInt) override { return {}; }
        Vector2 doGlyphAdvance(UnsignedInt) override { return {}; }
        Containers::Pointer<AbstractShaper> doCreateShaper() override { return {}; }

        private:
            bool _opened = false;
    } font;
    font.openFile("", 1.0f);

    /* We have many glyphs from the initial prefix, only the first three
       resolve to a valid one */
    UnsignedInt fontId = glyphCache.addFont(260, &font);
    glyphCache.addGlyph(fontId, 3, {},
        Range2Di::fromSize({1, 0}, {1, 1}));
    glyphCache.addGlyph(fontId, 1, {},
        Range2Di::fromSize({1, 1}, {1, 1}));
    glyphCache.addGlyph(fontId, 2, {},
        Range2Di::fromSize({0, 0}, {1, 2}));
    {
        const Containers::StridedArrayView2D<UnsignedByte> pixels = glyphCache.image().pixels<UnsignedByte>()[0];
        pixels[0][1] = 0xff;
        pixels[0][0] = 0x33;
        pixels[1][0] = 0x33;
        pixels[1][1] = 0x99;
    }
    glyphCache.flushImage({{}, glyphCache.size()});

    MAGNUM_VERIFY_NO_GL_ERROR();

    struct: AbstractShaper {
        using AbstractShaper::AbstractShaper;

        UnsignedInt doShape(Containers::StringView, UnsignedInt begin, UnsignedInt end, Containers::ArrayView<const FeatureRange>) override {
            return end - begin;
        }
        void doGlyphIdsInto(const Containers::StridedArrayView1D<UnsignedInt>& ids) const override {
            for(UnsignedInt i = 0; i != ids.size(); ++i)
                ids[i] = i + 1;
        }
        void doGlyphOffsetsAdvancesInto(const Containers::StridedArrayView1D<Vector2>& offsets, const Containers::StridedArrayView1D<Vector2>& advances) const override {
            for(UnsignedInt i = 0; i != offsets.size(); ++i) {
                advances[i] = {2.0f, 0.0f};
                offsets[i] = {0.0f, i == 2 ? 1.0f : 0.0f};
            }
        }
        void doGlyphClustersInto(const Containers::StridedArrayView1D<UnsignedInt>&) const override {}
    } shaper{font};

    RendererGL renderer{glyphCache};
    /* Set a small index type to only have to render 256 glyphs to make it
       change, not 16k */
    renderer.setIndexType(MeshIndexType::UnsignedByte);

    /* 16*16 glyphs. Index type doesn't change yet, only after render(). */
    renderer
        .setAlignment(Alignment::LineLeft)
        .setCursor({-3.0f, 1.0f})
        .add(shaper, 1.0f, "0123456789abcdef"_s*16);
    CORRADE_COMPARE(renderer.indexType(), MeshIndexType::UnsignedByte);
    CORRADE_COMPARE(renderer.mesh().indexType(), GL::MeshIndexType::UnsignedByte);

    /* This should then cause the index type change, and the GL mesh should
       adapt to it */
    renderer.render(shaper, 1.0f, "\nabc");
    CORRADE_COMPARE(renderer.indexType(), MeshIndexType::UnsignedShort);
    CORRADE_COMPARE(renderer.mesh().indexType(), GL::MeshIndexType::UnsignedShort);

    /* Just to match the image made in renderClearReset(), nothing else. There
       should be 256 + 5 glyphs in total. */
    renderer
        .setAlignment(Alignment::LineRight)
        .setCursor({5.0f, -3.0f})
        .render(shaper, 2.0f, "de");
    CORRADE_COMPARE(renderer.glyphCount(), 256 + 5);

    /* Draw just the suffix from the mesh, not everything */
    renderer.mesh()
        .setIndexOffset(256*6)
        .setCount(5*6);

    Shaders::VectorGL2D shader;
    shader.setTransformationProjectionMatrix(Matrix3::projection(Vector2{RenderSize}))
        .bindVectorTexture(glyphCache.texture())
        .draw(renderer.mesh());

    MAGNUM_VERIFY_NO_GL_ERROR();

    CORRADE_COMPARE_WITH(
        /* Use just one channel, the others are always the same */
        _framebuffer.read(_framebuffer.viewport(), {PixelFormat::RGBA8Unorm}).pixels<Color4ub>().slice(&Color4ub::r),
        Utility::Path::join(TEXT_TEST_DIR, "render.tga"),
        (DebugTools::CompareImageToFile{_manager, 0.0f, 0.0f}));
}

struct TestShaper: AbstractShaper {
    using AbstractShaper::AbstractShaper;

    UnsignedInt doShape(Containers::StringView text, UnsignedInt, UnsignedInt, Containers::ArrayView<const FeatureRange>) override {
        return text.size();
    }

    void doGlyphIdsInto(const Containers::StridedArrayView1D<UnsignedInt>& ids) const override {
        for(UnsignedInt i = 0; i != ids.size(); ++i) {
            /* It just rotates between the three glyphs */
            if(i % 3 == 0)
                ids[i] = 3;
            else if(i % 3 == 1)
                ids[i] = 7;
            else
                ids[i] = 9;
        }
    }
    void doGlyphOffsetsAdvancesInto(const Containers::StridedArrayView1D<Vector2>& offsets, const Containers::StridedArrayView1D<Vector2>& advances) const override {
        for(UnsignedInt i = 0; i != offsets.size(); ++i) {
            /* Offset Y and advance X is getting larger with every glyph,
               advance Y is flipping its sign with every glyph */
            offsets[i] = Vector2::yAxis(i + 1);
            advances[i] = {Float(i + 1), i % 2 ? -0.5f : +0.5f};
        }
    }
    void doGlyphClustersInto(const Containers::StridedArrayView1D<UnsignedInt>&) const override {
        /* Nothing in the renderer uses this API */
        CORRADE_FAIL("This shouldn't be called.");
    }
};

struct TestFont: AbstractFont {
    FontFeatures doFeatures() const override { return {}; }

    bool doIsOpened() const override { return _opened; }
    void doClose() override { _opened = false; }

    Properties doOpenFile(Containers::StringView, Float size) override {
        _opened = true;
        /* Line height isn't used for anything here so can be arbitrary */
        return {size, 4.5f, -2.5f, 10000.0f, 10};
    }

    void doGlyphIdsInto(const Containers::StridedArrayView1D<const char32_t>&, const Containers::StridedArrayView1D<UnsignedInt>& glyphs) override {
        for(UnsignedInt& i: glyphs)
            i = 0;
    }
    Vector2 doGlyphSize(UnsignedInt) override { return {}; }
    Vector2 doGlyphAdvance(UnsignedInt) override { return {}; }

    Containers::Pointer<AbstractShaper> doCreateShaper() override {
        return Containers::pointer<TestShaper>(*this);
    }

    bool _opened = false;
};

GlyphCacheGL testGlyphCache(AbstractFont& font) {
    /* Default padding is 1 to avoid artifacts, set that to 0 to simplify */
    GlyphCacheGL cache{PixelFormat::R8Unorm, {20, 20}, {}};

    /* Add one more font to verify the right one gets picked */
    cache.addFont(96);
    UnsignedInt fontId = cache.addFont(font.glyphCount(), &font);

    /* Three glyphs, covering bottom, top left and top right of the cache */
    cache.addGlyph(fontId, 3, {5, 10}, {{}, {20, 10}});
    cache.addGlyph(fontId, 7, {10, 5}, {{0, 10}, {10, 20}});
    cache.addGlyph(fontId, 9, {5, 5}, {{10, 10}, {20, 20}});

    return cache;
}

void RendererGLTest::renderMesh() {
    /* Like render(middle center), but with a mesh output instead of data */

    TestFont font;
    font.openFile({}, 0.5f);
    GlyphCacheGL cache = testGlyphCache(font);

    /* Capture the correct function name */
    CORRADE_VERIFY(true);

    GL::Mesh mesh{NoCreate};
    GL::Buffer vertexBuffer{GL::Buffer::TargetHint::Array},
        indexBuffer{GL::Buffer::TargetHint::ElementArray};
    Range2D bounds;
    std::tie(mesh, bounds) = Renderer3D::render(font, cache,
        0.25f, "abc", vertexBuffer, indexBuffer, GL::BufferUsage::StaticDraw, Alignment::MiddleCenter);
    MAGNUM_VERIFY_NO_GL_ERROR();

    /* Alignment offset */
    const Vector2 offset{-1.5f, -0.5f};

    /* Bounds */
    CORRADE_COMPARE(bounds, (Range2D{{0.0f, -1.25f}, {3.0f, 2.25f}}.translated(offset)));

    /** @todo How to verify this on ES? */
    #ifndef MAGNUM_TARGET_GLES
    /* Vertex buffer contents */
    Containers::Array<char> vertices = vertexBuffer.data();
    CORRADE_COMPARE_AS(Containers::arrayCast<const Vector2>(vertices), Containers::arrayView<Vector2>({
        Vector2{ 2.5f,  5.5f} + offset, {0.0f, 0.0f},
        Vector2{12.5f,  5.5f} + offset, {1.0f, 0.0f},
        Vector2{ 2.5f, 10.5f} + offset, {0.0f, 0.5f},
        Vector2{12.5f, 10.5f} + offset, {1.0f, 0.5f},

        Vector2{ 5.5f, 3.75f} + offset, {0.0f, 0.5f},
        Vector2{10.5f, 3.75f} + offset, {0.5f, 0.5f},
        Vector2{ 5.5f, 8.75f} + offset, {0.0f, 1.0f},
        Vector2{10.5f, 8.75f} + offset, {0.5f, 1.0f},

        Vector2{ 4.0f,  4.0f} + offset, {0.5f, 0.5f},
        Vector2{ 9.0f,  4.0f} + offset, {1.0f, 0.5f},
        Vector2{ 4.0f,  9.0f} + offset, {0.5f, 1.0f},
        Vector2{ 9.0f,  9.0f} + offset, {1.0f, 1.0f},
    }), TestSuite::Compare::Container);

    Containers::Array<char> indices = indexBuffer.data();
    CORRADE_COMPARE_AS(Containers::arrayCast<const UnsignedByte>(indices),
        Containers::arrayView<UnsignedByte>({
            0,  1,  2,  2,  1,  3,
            4,  5,  6,  6,  5,  7,
            8,  9, 10, 10,  9, 11,
        }), TestSuite::Compare::Container);
    #endif
}

void RendererGLTest::renderMeshIndexType() {
    #ifndef MAGNUM_TARGET_GLES
    TestFont font;
    font.openFile({}, 0.5f);
    GlyphCacheGL cache = testGlyphCache(font);

    /* Capture the correct function name */
    CORRADE_VERIFY(true);

    GL::Mesh mesh{NoCreate};
    GL::Buffer vertexBuffer, indexBuffer;

    /* Sizes: four vertices per glyph, each vertex has 2D position and 2D
       texture coordinates, each float is four bytes; six indices per glyph. */

    /* 8-bit indices (exactly 256 vertices) */
    std::tie(mesh, std::ignore) = Renderer3D::render(font, cache,
        1.0f, std::string(64, 'a'), vertexBuffer, indexBuffer, GL::BufferUsage::StaticDraw);
    MAGNUM_VERIFY_NO_GL_ERROR();
    Containers::Array<char> indicesByte = indexBuffer.data();
    CORRADE_COMPARE(vertexBuffer.size(), 256*(2 + 2)*4);
    CORRADE_COMPARE(indicesByte.size(), 64*6);
    CORRADE_COMPARE_AS(Containers::arrayCast<const UnsignedByte>(indicesByte).prefix(18),
        Containers::arrayView<UnsignedByte>({
            0,  1,  2,  2,  1,  3,
            4,  5,  6,  6,  5,  7,
            8,  9, 10, 10,  9, 11,
        }), TestSuite::Compare::Container);

    /* 16-bit indices (260 vertices) */
    std::tie(mesh, std::ignore) = Renderer3D::render(font, cache,
        1.0f, std::string(65, 'a'), vertexBuffer, indexBuffer, GL::BufferUsage::StaticDraw);
    MAGNUM_VERIFY_NO_GL_ERROR();
    Containers::Array<char> indicesShort = indexBuffer.data();
    CORRADE_COMPARE(vertexBuffer.size(), 260*(2 + 2)*4);
    CORRADE_COMPARE(indicesShort.size(), 65*6*2);
    CORRADE_COMPARE_AS(Containers::arrayCast<const UnsignedShort>(indicesShort).prefix(18),
        Containers::arrayView<UnsignedShort>({
            0,  1,  2,  2,  1,  3,
            4,  5,  6,  6,  5,  7,
            8,  9, 10, 10,  9, 11,
        }), TestSuite::Compare::Container);
    #else
    CORRADE_SKIP("Can't verify buffer contents on OpenGL ES.");
    #endif
}

void RendererGLTest::mutableText() {
    #ifndef MAGNUM_TARGET_GLES
    if(!GL::Context::current().isExtensionSupported<GL::Extensions::ARB::map_buffer_range>())
        CORRADE_SKIP(GL::Extensions::ARB::map_buffer_range::string() << "is not supported.");
    #elif defined(MAGNUM_TARGET_GLES2) && !defined(MAGNUM_TARGET_WEBGL)
    if(!GL::Context::current().isExtensionSupported<GL::Extensions::EXT::map_buffer_range>() &&
       !GL::Context::current().isExtensionSupported<GL::Extensions::OES::mapbuffer>())
        CORRADE_SKIP("No required extension is supported");
    #endif

    /* Like render(middle center) and renderMesh(), but modifying an instance
       instead of rendering once */

    TestFont font;
    font.openFile({}, 0.5f);
    GlyphCacheGL cache = testGlyphCache(font);
    Renderer2D renderer(font, cache, 0.25f, Alignment::MiddleCenter);
    MAGNUM_VERIFY_NO_GL_ERROR();
    CORRADE_COMPARE(renderer.capacity(), 0);
    CORRADE_COMPARE(renderer.fontSize(), 0.25f);
    CORRADE_COMPARE(renderer.rectangle(), Range2D());

    /* Reserve some capacity */
    renderer.reserve(4, GL::BufferUsage::DynamicDraw, GL::BufferUsage::DynamicDraw);
    MAGNUM_VERIFY_NO_GL_ERROR();
    CORRADE_COMPARE(renderer.capacity(), 4);
    /** @todo How to verify this on ES? */
    #ifndef MAGNUM_TARGET_GLES
    Containers::Array<char> indices = renderer.indexBuffer().data();
    CORRADE_COMPARE_AS(Containers::arrayCast<const UnsignedByte>(indices).prefix(24),
        Containers::arrayView<UnsignedByte>({
             0,  1,  2,  2,  1,  3,
             4,  5,  6,  6,  5,  7,
             8,  9, 10, 10,  9, 11,
            12, 13, 14, 14, 13, 15,
        }), TestSuite::Compare::Container);
    #endif

    /* Render text */
    renderer.render("abc");
    MAGNUM_VERIFY_NO_GL_ERROR();

    /* Alignment offset */
    const Vector2 offset{-1.5f, -0.5f};

    /* Updated bounds and mesh vertex count */
    CORRADE_COMPARE(renderer.rectangle(), (Range2D{{0.0f, -1.25f}, {3.0f, 2.25f}}.translated(offset)));
    CORRADE_COMPARE(renderer.mesh().count(), 3*6);

    /** @todo How to verify this on ES? */
    #ifndef MAGNUM_TARGET_GLES
    Containers::Array<char> vertices = renderer.vertexBuffer().data();
    CORRADE_COMPARE_AS(Containers::arrayCast<const Vector2>(vertices).prefix(2*4*3), Containers::arrayView<Vector2>({
        Vector2{ 2.5f,  5.5f} + offset, {0.0f, 0.0f},
        Vector2{12.5f,  5.5f} + offset, {1.0f, 0.0f},
        Vector2{ 2.5f, 10.5f} + offset, {0.0f, 0.5f},
        Vector2{12.5f, 10.5f} + offset, {1.0f, 0.5f},

        Vector2{ 5.5f, 3.75f} + offset, {0.0f, 0.5f},
        Vector2{10.5f, 3.75f} + offset, {0.5f, 0.5f},
        Vector2{ 5.5f, 8.75f} + offset, {0.0f, 1.0f},
        Vector2{10.5f, 8.75f} + offset, {0.5f, 1.0f},

        Vector2{ 4.0f,  4.0f} + offset, {0.5f, 0.5f},
        Vector2{ 9.0f,  4.0f} + offset, {1.0f, 0.5f},
        Vector2{ 4.0f,  9.0f} + offset, {0.5f, 1.0f},
        Vector2{ 9.0f,  9.0f} + offset, {1.0f, 1.0f},
    }), TestSuite::Compare::Container);
    #endif
}

void RendererGLTest::arrayGlyphCache() {
    CORRADE_SKIP_IF_NO_ASSERT();

    TestFont font;
    font.openFile({}, 0.5f);
    struct: AbstractGlyphCache {
        using AbstractGlyphCache::AbstractGlyphCache;

        GlyphCacheFeatures doFeatures() const override { return {}; }
    } cache{PixelFormat::R8Unorm, {100, 100, 3}};

    GL::Buffer buffer;

    Containers::String out;
    Error redirectError{&out};
    Renderer2D::render(font, cache, 1.0f, "", buffer, buffer, GL::BufferUsage::StaticDraw);
    Renderer2D{font, cache, 1.0f};
    CORRADE_COMPARE(out,
        "Text::Renderer: array glyph caches are not supported\n"
        "Text::AbstractRenderer: array glyph caches are not supported\n");
}

void RendererGLTest::fontNotFoundInCache() {
    CORRADE_SKIP_IF_NO_ASSERT();

    TestFont font;
    font.openFile({}, 0.5f);

    struct: AbstractGlyphCache {
        using AbstractGlyphCache::AbstractGlyphCache;

        GlyphCacheFeatures doFeatures() const override { return {}; }
    } cache{PixelFormat::R8Unorm, {100, 100}};
    cache.addFont(34);
    cache.addFont(25);

    GL::Buffer buffer;

    Containers::String out;
    Error redirectError{&out};
    Renderer2D::render(font, cache, 1.0f, "", buffer, buffer, GL::BufferUsage::StaticDraw);
    Renderer2D{font, cache, 1.0f};
    CORRADE_COMPARE(out,
        "Text::Renderer: font not found among 2 fonts in passed glyph cache\n"
        "Text::AbstractRenderer: font not found among 2 fonts in passed glyph cache\n");
}

}}}}

CORRADE_TEST_MAIN(Magnum::Text::Test::RendererGLTest)
