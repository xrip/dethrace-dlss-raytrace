#include "tests.h"

#include "common/car.h"
#include "common/graphics.h"
#include "common/globvrbm.h"
#include "common/utility.h"
#include "dr_types.h"
#include "harness/config.h"

extern tGraf_spec gGraf_specs[2];
extern int gGraf_spec_index;

void test_graphics_defaults() {
    TEST_ASSERT_EQUAL_INT(320, gGraf_specs[0].total_width);
    TEST_ASSERT_EQUAL_INT(200, gGraf_specs[0].total_height);
    TEST_ASSERT_EQUAL_INT(640, gGraf_specs[1].total_width);
    TEST_ASSERT_EQUAL_INT(480, gGraf_specs[1].total_height);

    TEST_ASSERT_EQUAL_INT(0, gCar_simplification_level);
    TEST_ASSERT_EQUAL_INT(1, gGraf_spec_index);
    TEST_ASSERT_EQUAL_INT(harness_game_info.data_dir_has_3dfx_assets, harness_game_config.opengl_3dfx_mode);
    TEST_ASSERT_EQUAL_INT(1920, harness_game_config.window_width);
    TEST_ASSERT_EQUAL_INT(1080, harness_game_config.window_height);
    TEST_ASSERT_EQUAL_INT(0, harness_game_config.msaa_samples);
    TEST_ASSERT_EQUAL_INT(1, harness_game_config.vsync);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, harness_game_config.anisotropy_limit);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 700.0f, harness_game_config.draw_distance);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, harness_game_config.car_lod_distance_scale);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, gCar_simplification_factor[0][0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, gCar_simplification_factor[0][1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, gCar_simplification_factor[1][1]);
}

void test_graphics_loadfont() {
    REQUIRES_DATA_DIRECTORY();

    TEST_ASSERT_EQUAL_INT(0, gFonts[kFont_TYPEABLE].file_read_once);
    LoadFont(kFont_TYPEABLE);
    TEST_ASSERT_EQUAL_INT(1, gFonts[kFont_TYPEABLE].file_read_once);
    TEST_ASSERT_NOT_NULL(gFonts[kFont_TYPEABLE].images);
}

void test_graphics_transparent_materials_keep_base_texture() {
    br_pixelmap* map;
    br_material* material;
    int old_opengl_mode;
    int old_mip_maps;
    int old_interpolate_textures;

    map = BrPixelmapAllocate(BR_PMT_INDEX_8, 2, 2, NULL, 0);
    material = BrMaterialAllocate(NULL);
    TEST_ASSERT_NOT_NULL(map);
    TEST_ASSERT_NOT_NULL(material);
    ((tU8*)map->pixels)[0] = 0;
    material->flags |= BR_MATF_MAP_ANTIALIASING;
    material->colour_map = map;

    old_opengl_mode = harness_game_config.opengl_3dfx_mode;
    old_mip_maps = gUse_mip_maps;
    old_interpolate_textures = gInterpolate_textures;
    harness_game_config.opengl_3dfx_mode = 1;
    gUse_mip_maps = 1;
    gInterpolate_textures = 1;
    GlorifyMaterial(&material, 1);

    TEST_ASSERT_EQUAL_INT(0, material->flags & BR_MATF_MAP_ANTIALIASING);

    ((tU8*)map->pixels)[0] = 1;
    ((tU8*)map->pixels)[1] = 1;
    ((tU8*)map->pixels)[map->row_bytes] = 1;
    ((tU8*)map->pixels)[map->row_bytes + 1] = 1;
    material->flags = 0;
    GlorifyMaterial(&material, 1);
    TEST_ASSERT_NOT_EQUAL(0, material->flags & BR_MATF_MAP_ANTIALIASING);

    harness_game_config.opengl_3dfx_mode = old_opengl_mode;
    gUse_mip_maps = old_mip_maps;
    gInterpolate_textures = old_interpolate_textures;
    BrMaterialFree(material);
    BrPixelmapFree(map);
}

void test_graphics_suite() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_graphics_defaults);
    RUN_TEST(test_graphics_loadfont);
    RUN_TEST(test_graphics_transparent_materials_keep_base_texture);
}
