#include "tests.h"

#include "common/car.h"
#include "common/graphics.h"
#include "dr_types.h"

extern tGraf_spec gGraf_specs[2];

void test_graphics_defaults() {
    TEST_ASSERT_EQUAL_INT(320, gGraf_specs[0].total_width);
    TEST_ASSERT_EQUAL_INT(200, gGraf_specs[0].total_height);
    TEST_ASSERT_EQUAL_INT(640, gGraf_specs[1].total_width);
    TEST_ASSERT_EQUAL_INT(480, gGraf_specs[1].total_height);

    TEST_ASSERT_EQUAL_INT(0, gCar_simplification_level);
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

void test_graphics_suite() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_graphics_defaults);
    RUN_TEST(test_graphics_loadfont);
}
