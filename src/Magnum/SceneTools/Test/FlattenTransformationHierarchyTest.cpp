/*
    This file is part of Magnum.

    Copyright © 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019,
                2020, 2021, 2022 Vladimír Vondruš <mosra@centrum.cz>

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

#include <sstream>
#include <Corrade/Containers/Triple.h>
#include <Corrade/TestSuite/Tester.h>
#include <Corrade/TestSuite/Compare/Container.h>
#include <Corrade/Utility/DebugStl.h>

#include "Magnum/Math/Matrix3.h"
#include "Magnum/Math/Matrix4.h"
#include "Magnum/SceneTools/FlattenTransformationHierarchy.h"
#include "Magnum/Trade/SceneData.h"

namespace Magnum { namespace SceneTools { namespace Test { namespace {

struct FlattenTransformationHierarchyTest: TestSuite::Tester {
    explicit FlattenTransformationHierarchyTest();

    void test2D();
    void test3D();

    void fieldNotFound();
    void not2DNot3D();
    void noParentField();

    void into2D();
    void into3D();
    void intoInvalidSize();
};

using namespace Math::Literals;

const struct {
    const char* name;
    Matrix3 globalTransformation2D;
    Matrix4 globalTransformation3D;
    bool fieldIdInsteadOfName;
    std::size_t transformationsToExclude, meshesToExclude;
    std::size_t expectedOutputSize;
} TestData[]{
    {"", {}, {}, false,
        2, 0,
        5},
    {"field ID", {}, {}, true,
        2, 0,
        5},
    {"global transformation",
        Matrix3::scaling(Vector2{0.5f}), Matrix4::scaling(Vector3{0.5f}), false,
        2, 0,
        5},
    {"global transformation, field ID",
        Matrix3::scaling(Vector2{0.5f}), Matrix4::scaling(Vector3{0.5f}), true,
        2, 0,
        5},
    {"transformations not part of the hierarchy", {}, {}, false,
        0, 0,
        5},
    {"empty field", {}, {}, false,
        2, 5,
        0},
};

const struct {
    const char* name;
    Matrix3 globalTransformation2D;
    Matrix4 globalTransformation3D;
    bool fieldIdInsteadOfName;
    std::size_t expectedOutputSize;
} IntoData[]{
    {"", {}, {}, false,
        5},
    {"field ID", {}, {}, true,
        5},
    {"global transformation",
        Matrix3::scaling(Vector2{0.5f}), Matrix4::scaling(Vector3{0.5f}), false,
        5},
    {"global transformation, field ID",
        Matrix3::scaling(Vector2{0.5f}), Matrix4::scaling(Vector3{0.5f}), true,
        5},
};

FlattenTransformationHierarchyTest::FlattenTransformationHierarchyTest() {
    addInstancedTests({&FlattenTransformationHierarchyTest::test2D,
                       &FlattenTransformationHierarchyTest::test3D},
        Containers::arraySize(TestData));

    addTests({&FlattenTransformationHierarchyTest::fieldNotFound,
              &FlattenTransformationHierarchyTest::not2DNot3D,
              &FlattenTransformationHierarchyTest::noParentField});

    addInstancedTests({&FlattenTransformationHierarchyTest::into2D,
                       &FlattenTransformationHierarchyTest::into3D},
        Containers::arraySize(IntoData));

    addTests({&FlattenTransformationHierarchyTest::intoInvalidSize});
}

const struct Scene {
    /* Using smaller types to verify we don't have unnecessarily hardcoded
       32-bit types */
    struct Parent {
        UnsignedShort object;
        Byte parent;
    } parents[9];

    struct Transformation {
        UnsignedShort object;
        Matrix3 transformation2D;
        Matrix4 transformation3D;
    } transforms[7];

    struct Mesh {
        UnsignedShort object;
        UnsignedShort mesh;
    } meshes[5];
} Data[]{{
    /*
        Cases to test:

        -   leaf paths with no attachments which don't contribute to the
            output in any way
        -   nodes with transforms but no meshes
        -   nodes with meshes but no transforms
        -   nodes with multiple meshes
        -   nodes with neither transforms nor meshes
        -   object 4 has a mesh with identity transform (or, rather, no
            transformation entry at all)
        -   objects 2 and 16 have the same mesh attached with the exact
            same transform -- this is a nonsense (they would overlap) and
            as such isn't deduplicated in any way
        -   objects 0, 32 and 17 have transformations/meshes, but not part
            of the hierarchy; these are cut away from the views in the
            first test case to keep it simple

            1T       4M
           / \       |              32M 0MM
          5T 2TM     11
         / \   \     |               32T 17T
       3MM  7T  6   16TM
    */
    {{3, 5},
     {11, 4},
     {5, 1},
     {1, -1},
     {7, 5},
     {6, 2},
     {2, 1},
     {4, -1},
     {16, 11}},
    {{2, Matrix3::scaling({3.0f, 5.0f}),
         Matrix4::scaling({3.0f, 5.0f, 2.0f})},
     {1, Matrix3::translation({1.0f, -1.5f}),
         Matrix4::translation({1.0f, -1.5f, 0.5f})},
     /* Same absolute transform as node 2 */
     {16, Matrix3::translation({1.0f, -1.5f})*
          Matrix3::scaling({3.0f, 5.0f}),
          Matrix4::translation({1.0f, -1.5f, 0.5f})*
          Matrix4::scaling({3.0f, 5.0f, 2.0f})},
     {7, Matrix3::scaling({2.0f, 1.0f}),
         Matrix4::scaling({2.0f, 1.0f, 0.5f})},
     {5, Matrix3::rotation(35.0_degf),
         Matrix4::rotationZ(35.0_degf)},
     /* These are not part of the hierarchy */
     {32, Matrix3::translation({1.0f, 0.5f}),
          Matrix4::translation({1.0f, 0.5f, 2.0f})},
     {17, Matrix3::translation({2.0f, 1.0f}),
          Matrix4::translation({2.0f, 1.0f, 4.0f})},
    },
    /* The mesh IDs aren't used for anything, just setting them to something
       random (and high) to avoid their misuses as some offsets / IDs */
    {{2, 113},
     {3, 266},
     {4, 525},
     {3, 422},
     {16, 113}}
}};

void FlattenTransformationHierarchyTest::test2D() {
    auto&& data = TestData[testCaseInstanceId()];
    setTestCaseDescription(data.name);

    Trade::SceneData scene{Trade::SceneMappingType::UnsignedShort, 33, {}, Data, {
        /* To verify it doesn't just pick the first field ever */
        Trade::SceneFieldData{Trade::SceneField::Camera, Trade::SceneMappingType::UnsignedShort, nullptr, Trade::SceneFieldType::UnsignedInt, nullptr},
        Trade::SceneFieldData{Trade::SceneField::Parent,
            Containers::stridedArrayView(Data->parents)
                .slice(&Scene::Parent::object),
            Containers::stridedArrayView(Data->parents)
                .slice(&Scene::Parent::parent)},
        Trade::SceneFieldData{Trade::SceneField::Mesh,
            Containers::stridedArrayView(Data->meshes)
                .slice(&Scene::Mesh::object)
                .exceptSuffix(data.meshesToExclude),
            Containers::stridedArrayView(Data->meshes)
                .slice(&Scene::Mesh::mesh)
                .exceptSuffix(data.meshesToExclude)},
        Trade::SceneFieldData{Trade::SceneField::Transformation,
            Containers::stridedArrayView(Data->transforms)
                .slice(&Scene::Transformation::object)
                .exceptSuffix(data.transformationsToExclude),
            Containers::stridedArrayView(Data->transforms)
                .slice(&Scene::Transformation::transformation2D)
                .exceptSuffix(data.transformationsToExclude)},
    }};

    Containers::Array<Matrix3> out;
    /* To test all overloads */
    if(data.globalTransformation2D != Matrix3{}) {
        if(data.fieldIdInsteadOfName)
            out = flattenTransformationHierarchy2D(scene, 2, data.globalTransformation2D);
        else
            out = flattenTransformationHierarchy2D(scene, Trade::SceneField::Mesh, data.globalTransformation2D);
    } else {
        if(data.fieldIdInsteadOfName)
            out = flattenTransformationHierarchy2D(scene, 2);
        else
            out = flattenTransformationHierarchy2D(scene, Trade::SceneField::Mesh);
    }

    CORRADE_COMPARE_AS(out, Containers::arrayView({
        data.globalTransformation2D*
            Matrix3::translation({1.0f, -1.5f})*
            Matrix3::scaling({3.0f, 5.0f}),
        data.globalTransformation2D*
            Matrix3::translation({1.0f, -1.5f})*
            Matrix3::rotation(35.0_degf),
        data.globalTransformation2D,
        data.globalTransformation2D*
            Matrix3::translation({1.0f, -1.5f})*
            Matrix3::rotation(35.0_degf),
        data.globalTransformation2D*
            Matrix3::translation({1.0f, -1.5f})*
            Matrix3::scaling({3.0f, 5.0f})
    }).prefix(data.expectedOutputSize), TestSuite::Compare::Container);
}

void FlattenTransformationHierarchyTest::test3D() {
    auto&& data = TestData[testCaseInstanceId()];
    setTestCaseDescription(data.name);

    Trade::SceneData scene{Trade::SceneMappingType::UnsignedShort, 33, {}, Data, {
        /* To verify it doesn't just pick the first field ever */
        Trade::SceneFieldData{Trade::SceneField::Camera, Trade::SceneMappingType::UnsignedShort, nullptr, Trade::SceneFieldType::UnsignedInt, nullptr},
        Trade::SceneFieldData{Trade::SceneField::Parent,
            Containers::stridedArrayView(Data->parents)
                .slice(&Scene::Parent::object),
            Containers::stridedArrayView(Data->parents)
                .slice(&Scene::Parent::parent)},
        Trade::SceneFieldData{Trade::SceneField::Mesh,
            Containers::stridedArrayView(Data->meshes)
                .slice(&Scene::Mesh::object)
                .exceptSuffix(data.meshesToExclude),
            Containers::stridedArrayView(Data->meshes)
                .slice(&Scene::Mesh::mesh)
                .exceptSuffix(data.meshesToExclude)},
        Trade::SceneFieldData{Trade::SceneField::Transformation,
            Containers::stridedArrayView(Data->transforms)
                .slice(&Scene::Transformation::object)
                .exceptSuffix(data.transformationsToExclude),
            Containers::stridedArrayView(Data->transforms)
                .slice(&Scene::Transformation::transformation3D)
                .exceptSuffix(data.transformationsToExclude)},
    }};

    Containers::Array<Matrix4> out;
    /* To test all overloads */
    if(data.globalTransformation3D != Matrix4{}) {
        if(data.fieldIdInsteadOfName)
            out = flattenTransformationHierarchy3D(scene, 2, data.globalTransformation3D);
        else
            out = flattenTransformationHierarchy3D(scene, Trade::SceneField::Mesh, data.globalTransformation3D);
    } else {
        if(data.fieldIdInsteadOfName)
            out = flattenTransformationHierarchy3D(scene, 2);
        else
            out = flattenTransformationHierarchy3D(scene, Trade::SceneField::Mesh);
    }

    CORRADE_COMPARE_AS(out, Containers::arrayView({
        data.globalTransformation3D*
            Matrix4::translation({1.0f, -1.5f, 0.5f})*
            Matrix4::scaling({3.0f, 5.0f, 2.0f}),
        data.globalTransformation3D*
            Matrix4::translation({1.0f, -1.5f, 0.5f})*
            Matrix4::rotationZ(35.0_degf),
        data.globalTransformation3D,
        data.globalTransformation3D*
            Matrix4::translation({1.0f, -1.5f, 0.5f})*
            Matrix4::rotationZ(35.0_degf),
        data.globalTransformation3D*
            Matrix4::translation({1.0f, -1.5f, 0.5f})*
            Matrix4::scaling({3.0f, 5.0f, 2.0f})
    }).prefix(data.expectedOutputSize), TestSuite::Compare::Container);
}

void FlattenTransformationHierarchyTest::fieldNotFound() {
    CORRADE_SKIP_IF_NO_ASSERT();

    Trade::SceneData scene{Trade::SceneMappingType::UnsignedInt, 0, nullptr, {
        Trade::SceneFieldData{Trade::SceneField::Parent, Trade::SceneMappingType::UnsignedInt, nullptr, Trade::SceneFieldType::Int, nullptr},
        Trade::SceneFieldData{Trade::SceneField::Transformation, Trade::SceneMappingType::UnsignedInt, nullptr, Trade::SceneFieldType::Matrix3x3, nullptr}
    }};

    std::ostringstream out;
    Error redirectError{&out};
    flattenTransformationHierarchy2D(scene, Trade::SceneField::Mesh);
    flattenTransformationHierarchy3D(scene, Trade::SceneField::Mesh);
    flattenTransformationHierarchy2D(scene, 2);
    flattenTransformationHierarchy3D(scene, 2);
    CORRADE_COMPARE(out.str(),
        "SceneTools::flattenTransformationHierarchy(): field Trade::SceneField::Mesh not found\n"
        "SceneTools::flattenTransformationHierarchy(): field Trade::SceneField::Mesh not found\n"
        "SceneTools::flattenTransformationHierarchy(): index 2 out of range for 2 fields\n"
        "SceneTools::flattenTransformationHierarchy(): index 2 out of range for 2 fields\n");
}

void FlattenTransformationHierarchyTest::not2DNot3D() {
    CORRADE_SKIP_IF_NO_ASSERT();

    Trade::SceneData scene{Trade::SceneMappingType::UnsignedInt, 0, nullptr, {
        Trade::SceneFieldData{Trade::SceneField::Parent, Trade::SceneMappingType::UnsignedInt, nullptr, Trade::SceneFieldType::Int, nullptr}
    }};

    std::ostringstream out;
    Error redirectError{&out};
    flattenTransformationHierarchy2D(scene, Trade::SceneField::Parent);
    flattenTransformationHierarchy2D(scene, 0);
    flattenTransformationHierarchy3D(scene, Trade::SceneField::Parent);
    flattenTransformationHierarchy3D(scene, 0);
    CORRADE_COMPARE(out.str(),
        "SceneTools::flattenTransformationHierarchy(): the scene is not 2D\n"
        "SceneTools::flattenTransformationHierarchy(): the scene is not 2D\n"
        "SceneTools::flattenTransformationHierarchy(): the scene is not 3D\n"
        "SceneTools::flattenTransformationHierarchy(): the scene is not 3D\n");
}

void FlattenTransformationHierarchyTest::noParentField() {
    CORRADE_SKIP_IF_NO_ASSERT();

    Trade::SceneData scene{Trade::SceneMappingType::UnsignedInt, 0, nullptr, {
        Trade::SceneFieldData{Trade::SceneField::Transformation, Trade::SceneMappingType::UnsignedInt, nullptr, Trade::SceneFieldType::Matrix3x3, nullptr}
    }};

    std::ostringstream out;
    Error redirectError{&out};
    flattenTransformationHierarchy2D(scene, Trade::SceneField::Transformation);
    flattenTransformationHierarchy2D(scene, 0);
    CORRADE_COMPARE(out.str(),
        "SceneTools::flattenTransformationHierarchy(): the scene has no hierarchy\n"
        "SceneTools::flattenTransformationHierarchy(): the scene has no hierarchy\n");
}

void FlattenTransformationHierarchyTest::into2D() {
    auto&& data = IntoData[testCaseInstanceId()];
    setTestCaseDescription(data.name);

    /* The *Into() variant is the actual base implementation, so just verify
       that the data get correctly propagated through. Everything else is
       tested above already. */

    Trade::SceneData scene{Trade::SceneMappingType::UnsignedShort, 33, {}, Data, {
        Trade::SceneFieldData{Trade::SceneField::Parent,
            Containers::stridedArrayView(Data->parents)
                .slice(&Scene::Parent::object),
            Containers::stridedArrayView(Data->parents)
                .slice(&Scene::Parent::parent)},
        Trade::SceneFieldData{Trade::SceneField::Transformation,
            Containers::stridedArrayView(Data->transforms)
                .slice(&Scene::Transformation::object),
            Containers::stridedArrayView(Data->transforms)
                .slice(&Scene::Transformation::transformation2D)},
        Trade::SceneFieldData{Trade::SceneField::Mesh,
            Containers::stridedArrayView(Data->meshes)
                .slice(&Scene::Mesh::object),
            Containers::stridedArrayView(Data->meshes)
                .slice(&Scene::Mesh::mesh)}
    }};

    Containers::Array<Matrix3> out{NoInit, scene.fieldSize(Trade::SceneField::Mesh)};
    /* To test all overloads */
    if(data.globalTransformation2D != Matrix3{}) {
        if(data.fieldIdInsteadOfName)
            flattenTransformationHierarchy2DInto(scene, 2, out, data.globalTransformation2D);
        else
            flattenTransformationHierarchy2DInto(scene, Trade::SceneField::Mesh, out, data.globalTransformation2D);
    } else {
        if(data.fieldIdInsteadOfName)
            flattenTransformationHierarchy2DInto(scene, 2, out);
        else
            flattenTransformationHierarchy2DInto(scene, Trade::SceneField::Mesh, out);
    }

    CORRADE_COMPARE_AS(out, Containers::arrayView<Matrix3>({
        data.globalTransformation2D*
            Matrix3::translation({1.0f, -1.5f})*
            Matrix3::scaling({3.0f, 5.0f}),
        data.globalTransformation2D*
            Matrix3::translation({1.0f, -1.5f})*
            Matrix3::rotation(35.0_degf),
        data.globalTransformation2D,
        data.globalTransformation2D*
            Matrix3::translation({1.0f, -1.5f})*
            Matrix3::rotation(35.0_degf),
        data.globalTransformation2D*
            Matrix3::translation({1.0f, -1.5f})*
            Matrix3::scaling({3.0f, 5.0f})
    }), TestSuite::Compare::Container);
}

void FlattenTransformationHierarchyTest::into3D() {
    auto&& data = IntoData[testCaseInstanceId()];
    setTestCaseDescription(data.name);

    /* The *Into() variant is the actual base implementation, so just verify
       that the data get correctly propagated through. Everything else is
       tested above already. */

    Trade::SceneData scene{Trade::SceneMappingType::UnsignedShort, 33, {}, Data, {
        Trade::SceneFieldData{Trade::SceneField::Parent,
            Containers::stridedArrayView(Data->parents)
                .slice(&Scene::Parent::object),
            Containers::stridedArrayView(Data->parents)
                .slice(&Scene::Parent::parent)},
        Trade::SceneFieldData{Trade::SceneField::Transformation,
            Containers::stridedArrayView(Data->transforms)
                .slice(&Scene::Transformation::object),
            Containers::stridedArrayView(Data->transforms)
                .slice(&Scene::Transformation::transformation3D)},
        Trade::SceneFieldData{Trade::SceneField::Mesh,
            Containers::stridedArrayView(Data->meshes)
                .slice(&Scene::Mesh::object),
            Containers::stridedArrayView(Data->meshes)
                .slice(&Scene::Mesh::mesh)}
    }};

    Containers::Array<Matrix4> out{NoInit, scene.fieldSize(Trade::SceneField::Mesh)};
    /* To test all overloads */
    if(data.globalTransformation3D != Matrix4{}) {
        if(data.fieldIdInsteadOfName)
            flattenTransformationHierarchy3DInto(scene, 2, out, data.globalTransformation3D);
        else
            flattenTransformationHierarchy3DInto(scene, Trade::SceneField::Mesh, out, data.globalTransformation3D);
    } else {
        if(data.fieldIdInsteadOfName)
            flattenTransformationHierarchy3DInto(scene, 2, out);
        else
            flattenTransformationHierarchy3DInto(scene, Trade::SceneField::Mesh, out);
    }

    CORRADE_COMPARE_AS(out, Containers::arrayView<Matrix4>({
        data.globalTransformation3D*
            Matrix4::translation({1.0f, -1.5f, 0.5f})*
            Matrix4::scaling({3.0f, 5.0f, 2.0f}),
        data.globalTransformation3D*
            Matrix4::translation({1.0f, -1.5f, 0.5f})*
            Matrix4::rotationZ(35.0_degf),
        data.globalTransformation3D,
        data.globalTransformation3D*
            Matrix4::translation({1.0f, -1.5f, 0.5f})*
            Matrix4::rotationZ(35.0_degf),
        data.globalTransformation3D*
            Matrix4::translation({1.0f, -1.5f, 0.5f})*
            Matrix4::scaling({3.0f, 5.0f, 2.0f})
    }), TestSuite::Compare::Container);
}

void FlattenTransformationHierarchyTest::intoInvalidSize() {
    CORRADE_SKIP_IF_NO_ASSERT();

    struct Data {
        UnsignedInt mapping;
        UnsignedInt mesh;
    } data[5]{};

    Trade::SceneData scene2D{Trade::SceneMappingType::UnsignedInt, 1, {}, data, {
        Trade::SceneFieldData{Trade::SceneField::Parent, Trade::SceneMappingType::UnsignedInt, nullptr, Trade::SceneFieldType::Int, nullptr},
        Trade::SceneFieldData{Trade::SceneField::Mesh,
            Containers::stridedArrayView(data).slice(&Data::mapping),
            Containers::stridedArrayView(data).slice(&Data::mesh)},
        Trade::SceneFieldData{Trade::SceneField::Transformation, Trade::SceneMappingType::UnsignedInt, nullptr, Trade::SceneFieldType::Matrix3x3, nullptr}
    }};
    Trade::SceneData scene3D{Trade::SceneMappingType::UnsignedInt, 1, {}, data, {
        Trade::SceneFieldData{Trade::SceneField::Parent, Trade::SceneMappingType::UnsignedInt, nullptr, Trade::SceneFieldType::Int, nullptr},
        Trade::SceneFieldData{Trade::SceneField::Mesh,
            Containers::stridedArrayView(data).slice(&Data::mapping),
            Containers::stridedArrayView(data).slice(&Data::mesh)},
        Trade::SceneFieldData{Trade::SceneField::Transformation, Trade::SceneMappingType::UnsignedInt, nullptr, Trade::SceneFieldType::Matrix4x4, nullptr}
    }};

    Matrix3 transformations2D[6];
    Matrix4 transformations3D[4];

    std::ostringstream out;
    Error redirectError{&out};
    flattenTransformationHierarchy2DInto(scene2D, Trade::SceneField::Mesh, transformations2D);
    flattenTransformationHierarchy2DInto(scene2D, 1, transformations2D);
    flattenTransformationHierarchy3DInto(scene3D, Trade::SceneField::Mesh, transformations3D);
    flattenTransformationHierarchy3DInto(scene3D, 1, transformations3D);
    CORRADE_COMPARE(out.str(),
        "SceneTools::flattenTransformationHierarchyInto(): bad output size, expected 5 but got 6\n"
        "SceneTools::flattenTransformationHierarchyInto(): bad output size, expected 5 but got 6\n"
        "SceneTools::flattenTransformationHierarchyInto(): bad output size, expected 5 but got 4\n"
        "SceneTools::flattenTransformationHierarchyInto(): bad output size, expected 5 but got 4\n");
}

}}}}

CORRADE_TEST_MAIN(Magnum::SceneTools::Test::FlattenTransformationHierarchyTest)
