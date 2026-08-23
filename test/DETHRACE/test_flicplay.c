#include "brender.h"
#include "common/flicplay.h"
#include "common/graphics.h"
#include "tests.h"

int nbr_frames_rendered;

void frame_render_callback() {
    nbr_frames_rendered++;
}
void test_flicplay_playflic() {
    REQUIRES_DATA_DIRECTORY();
    int pIndex = 31; // main menu swing in
    br_pixelmap* target;

    gCurrent_palette_pixels = malloc(0x400);
    FlicPaletteAllocate();
    TEST_ASSERT_EQUAL_INT(1, LoadFlic(pIndex));

    target = BrPixelmapAllocate(BR_MEMORY_PIXELS, 320, 200, NULL, 0);
    PlayFlic(
        pIndex,
        gMain_flic_list[pIndex].the_size,
        gMain_flic_list[pIndex].data_ptr,
        target,
        gMain_flic_list[pIndex].x_offset,
        gMain_flic_list[pIndex].y_offset,
        frame_render_callback,
        gMain_flic_list[pIndex].interruptable,
        gMain_flic_list[pIndex].frame_rate);

    TEST_ASSERT_EQUAL_INT(3, nbr_frames_rendered);
}

void test_flicplay_deltax_unaligned_destination() {
    br_pixelmap pixelmap = { 0 };
    tFlic_descriptor descriptor = { 0 };
    tU8 destination[8] = { 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa };
    tU8 encoded[9] = { 0 };

    pixelmap.row_bytes = 4;
    descriptor.data = (char*)&encoded[1];
    descriptor.first_pixel = &destination[1];
    descriptor.the_pixelmap = &pixelmap;

    // One line, one packet, one repeated two-byte pixel pair.
    encoded[1] = 1;
    encoded[3] = 1;
    encoded[5] = 0;
    encoded[6] = (tU8)-1;
    encoded[7] = 0x11;
    encoded[8] = 0x22;

    DoDeltaX(&descriptor, sizeof(encoded));

    TEST_ASSERT_EQUAL_HEX8(0xaa, destination[0]);
    TEST_ASSERT_EQUAL_HEX8(0x11, destination[1]);
    TEST_ASSERT_EQUAL_HEX8(0x22, destination[2]);
    TEST_ASSERT_EQUAL_HEX8(0xaa, destination[3]);
}

void test_flicplay_suite() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_flicplay_playflic);
    RUN_TEST(test_flicplay_deltax_unaligned_destination);
}
