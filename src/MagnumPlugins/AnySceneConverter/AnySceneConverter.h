#ifndef Magnum_Trade_AnySceneConverter_h
#define Magnum_Trade_AnySceneConverter_h
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

/** @file
 * @brief Class @ref Magnum::Trade::AnySceneConverter
 * @m_since{2020,06}
 */

#include <Corrade/Containers/Pointer.h>

#include "Magnum/Trade/AbstractSceneConverter.h"
#include "MagnumPlugins/AnySceneConverter/configure.h"

#ifndef DOXYGEN_GENERATING_OUTPUT
#ifndef MAGNUM_ANYSCENECONVERTER_BUILD_STATIC
    #ifdef AnySceneConverter_EXPORTS
        #define MAGNUM_ANYSCENECONVERTER_EXPORT CORRADE_VISIBILITY_EXPORT
    #else
        #define MAGNUM_ANYSCENECONVERTER_EXPORT CORRADE_VISIBILITY_IMPORT
    #endif
#else
    #define MAGNUM_ANYSCENECONVERTER_EXPORT CORRADE_VISIBILITY_STATIC
#endif
#define MAGNUM_ANYSCENECONVERTER_LOCAL CORRADE_VISIBILITY_LOCAL
#else
#define MAGNUM_ANYSCENECONVERTER_EXPORT
#define MAGNUM_ANYSCENECONVERTER_LOCAL
#endif

namespace Magnum { namespace Trade {

/**
@brief Any scene converter plugin
@m_since{2020,06}

Detects file type based on file extension, loads corresponding plugin and then
tries to convert the file with it. Supported formats:

-   glTF (`*.gltf`, `*.glb`), converted with @ref GltfSceneConverter or any
    other plugin that provides it
-   Stanford (`*.ply`), converted with @ref StanfordSceneConverter or any other
    plugin that provides it

Only converting to files is supported.

@section Trade-AnySceneConverter-usage Usage

@m_class{m-note m-success}

@par
    This class is a plugin that's meant to be dynamically loaded and used
    through the base @ref AbstractSceneConverter interface. See its
    documentation for introduction and usage examples.

This plugin depends on the @ref Trade library and is built if
`MAGNUM_WITH_ANYSCENECONVERTER` is enabled when building Magnum. To use as a
dynamic plugin, load @cpp "AnySceneConverter" @ce via
@ref Corrade::PluginManager::Manager.

Additionally, if you're using Magnum as a CMake subproject, do the following:

@code{.cmake}
set(MAGNUM_WITH_ANYSCENECONVERTER ON CACHE BOOL "" FORCE)
add_subdirectory(magnum EXCLUDE_FROM_ALL)

# So the dynamically loaded plugin gets built implicitly
add_dependencies(your-app Magnum::AnySceneConverter)
@endcode

To use as a static plugin or as a dependency of another plugin with CMake, you
need to request the `AnySceneConverter` component of the `Magnum` package and
link to the `Magnum::AnySceneConverter` target:

@code{.cmake}
find_package(Magnum REQUIRED AnySceneConverter)

# ...
target_link_libraries(your-app PRIVATE Magnum::AnySceneConverter)
@endcode

See @ref building, @ref cmake, @ref plugins and @ref file-formats for more
information.

@section Trade-AnySceneConverter-proxy Interface proxying and option propagation

On a call to @ref convertToFile() or @ref beginFile(), a target file format is
detected from the extension and a corresponding plugin is loaded. After that,
flags set via @ref setFlags() and options set through @ref configuration() are
propagated to the concrete implementation. A warning is emitted in case an
option set is not present in the default configuration of the target plugin.

The @ref features() initially report just
@ref SceneConverterFeature::ConvertMeshToFile and @relativeref{SceneConverterFeature,ConvertMultipleToFile} --- i.e., not
advertising support for any actual data types. These are included only once
@ref beginFile() is called, taken from the concrete plugin implementation.

Calls to the @ref endFile(), @ref add() and related functions are then proxied
to the concrete implementation. The @ref abort() function aborts and destroys
the internally instantiated plugin; @ref isConverting() works as usual.

Besides delegating the flags, the @ref AnySceneConverter itself recognizes
@ref SceneConverterFlag::Verbose, printing info about the concrete plugin being
used when the flag is enabled. @ref SceneConverterFlag::Quiet is recognized as
well and causes all warnings to be suppressed.
*/
class MAGNUM_ANYSCENECONVERTER_EXPORT AnySceneConverter: public AbstractSceneConverter {
    public:
        /** @brief Constructor with access to plugin manager */
        explicit AnySceneConverter(PluginManager::Manager<AbstractSceneConverter>& manager);

        /** @brief Plugin manager constructor */
        explicit AnySceneConverter(PluginManager::AbstractManager& manager, const Containers::StringView& plugin);

        ~AnySceneConverter();

    private:
        MAGNUM_ANYSCENECONVERTER_LOCAL SceneConverterFeatures doFeatures() const override;
        MAGNUM_ANYSCENECONVERTER_LOCAL bool doConvertToFile(const MeshData& mesh, Containers::StringView filename) override;

        MAGNUM_ANYSCENECONVERTER_LOCAL void doAbort() override;
        MAGNUM_ANYSCENECONVERTER_LOCAL bool doBeginFile(Containers::StringView filename) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL bool doEndFile(Containers::StringView filename) override;

        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const SceneData& scene, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL void doSetSceneFieldName(SceneField field, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL void doSetObjectName(UnsignedLong object, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL void doSetDefaultScene(UnsignedInt id) override;

        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const AnimationData& animation, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL void doSetAnimationTrackTargetName(AnimationTrackTarget target, Containers::StringView name) override;

        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const LightData& light, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const CameraData& camera, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const SkinData2D& skin, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const SkinData3D& skin, Containers::StringView name) override;

        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const MeshData& mesh, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const Containers::Iterable<const MeshData>& meshLevels, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL void doSetMeshAttributeName(MeshAttribute attribute, Containers::StringView name) override;

        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const MaterialData& material, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const TextureData& texture, Containers::StringView name) override;

        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const ImageData1D& image, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const Containers::Iterable<const ImageData1D>& imageLevels, Containers::StringView name) override;

        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const ImageData2D& image, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const Containers::Iterable<const ImageData2D>& imageLevels, Containers::StringView name) override;

        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const ImageData3D& image, Containers::StringView name) override;
        MAGNUM_ANYSCENECONVERTER_LOCAL bool doAdd(UnsignedInt id, const Containers::Iterable<const ImageData3D>& imageLevels, Containers::StringView name) override;

        Containers::Pointer<AbstractSceneConverter> _converter;
};

}}

#endif
