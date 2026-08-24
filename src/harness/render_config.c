#include "include/harness/render_config.h"

#include "brender.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Declared here rather than by including the game's globvrkm.h: the harness
 * does not carry the game's include path, and pulling it in would invert the
 * dependency. See src/DETHRACE/common/globvrkm.h for the definition.
 */
extern br_actor* gCamera_list[2];

static int scene_checked = 0;
static int scene_width = 0;
static int scene_height = 0;

static void read_scene_size(void) {
    const char* value;
    int width = 0;
    int height = 0;

    scene_checked = 1;
    value = getenv("DETHRACE_VULKAN_SCENE_SIZE");
    if (value == NULL || *value == '\0') {
        return;
    }
    if (sscanf(value, "%dx%d", &width, &height) != 2) {
        fprintf(stderr, "DETHRACE_VULKAN_SCENE_SIZE: expected <width>x<height>, got '%s'\n", value);
        return;
    }
    /* Upper bound keeps a typo from asking for a target the GPU cannot hold. */
    if (width < 320 || height < 240 || width > 7680 || height > 4320) {
        fprintf(stderr, "DETHRACE_VULKAN_SCENE_SIZE: %dx%d is out of range, ignoring\n", width, height);
        return;
    }
    scene_width = width;
    scene_height = height;
}

int HarnessRenderSceneSize(int* width, int* height) {
    if (!scene_checked) {
        read_scene_size();
    }
    if (scene_width == 0 || scene_height == 0) {
        return 0;
    }
    if (width != NULL) {
        *width = scene_width;
    }
    if (height != NULL) {
        *height = scene_height;
    }
    return 1;
}

float HarnessRenderSceneAspect(void) {
    int width = 0;
    int height = 0;

    if (!HarnessRenderSceneSize(&width, &height)) {
        return 0.0f;
    }
    return (float)width / (float)height;
}

void HarnessRenderApplyCameraAspect(void) {
    float aspect = HarnessRenderSceneAspect();
    int i;

    if (aspect <= 0.0f) {
        return;
    }
    /*
     * Only the forward cameras. gRearview_camera is the wing mirror and takes
     * its aspect from the car's mirror rectangle, so it is left alone.
     */
    for (i = 0; i < 2; ++i) {
        if (gCamera_list[i] != NULL && gCamera_list[i]->type_data != NULL) {
            ((br_camera*)gCamera_list[i]->type_data)->aspect = aspect;
        }
    }
}
