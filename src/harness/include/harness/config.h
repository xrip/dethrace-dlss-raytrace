#ifndef HARNESS_CONFIG_H
#define HARNESS_CONFIG_H

#define MAX_PATH 1024

typedef enum tHarness_game_type {
    eGame_none,
    eGame_carmageddon,
    eGame_splatpack,
    eGame_carmageddon_demo,
    eGame_splatpack_demo,
    eGame_splatpack_xmas_demo,
} tHarness_game_type;

typedef enum {
    eGameLocalization_none,
    eGameLocalization_german,
    eGameLocalization_polish,
    eGameLocalization_french,
} tHarness_game_localization;

typedef struct tHarness_game_info {
    tHarness_game_type mode;
    tHarness_game_localization localization;
    struct {
        // different between carmageddon and splatpack
        char* INTRO_SMK_FILE;
        // different between demo and full game
        char* GERMAN_LOADSCRN;
        // some versions have an ascii table built-in, others provide it through KEYBOARD.COK
        int requires_ascii_table;
        // built-in keyboard look-up table for certain localized Carmageddon releases
        int* ascii_table;
        // built-in shifted keyboard look-up table for certain localized Carmageddon releases
        int* ascii_shift_table;
    } defines;
    int data_dir_has_3dfx_assets;
} tHarness_game_info;

typedef struct tHarness_game_dir {
    char name[256];
    char directory[MAX_PATH];
} tHarness_game_dir;

typedef struct tHarness_game_config {
    int enable_cd_check;
    int physics_per_frame;
    float fps;
    int freeze_timer;
    unsigned demo_timeout;
    int enable_diagnostics;
    float volume_multiplier;
    int start_full_screen;
    int window_width;
    int window_height;
    int msaa_samples;
    int vsync;
    float anisotropy_limit;
    float draw_distance;
    float car_lod_distance_scale;
    int gore_check;
    int sound_options;

    int verbose;
    int opengl_3dfx_mode;
    // Use the Vulkan renderer (vkrend) instead of OpenGL.
    int vulkan_mode;
    int game_completed;

    // Skip the front-end and drop straight into a race. -1 disables it, otherwise
    // it is the index of the race to start. Used for renderer benchmarking and for
    // capturing reproducible reference screenshots.
    int quick_race;
    int quick_race_skill;
    // Slot to restore before the quick race starts, so a test run gets the car,
    // credits, power-ups and opponents from a saved career instead of a fresh
    // start. -1 disables it.
    int quick_race_save_slot;
    // Set when --quick-race named a race explicitly. Without it a restored save
    // races wherever the career had got to, which is usually what you want.
    int quick_race_index_explicit;

    int install_signalhandler;
    int no_bind;
    char network_adapter_name[256];
    char platform_name[256];

    char selected_dir[MAX_PATH];
    int game_dirs_count;
    tHarness_game_dir game_dirs[10];
    char default_game[256];
} tHarness_game_config;

extern tHarness_game_info harness_game_info;
extern tHarness_game_config harness_game_config;

#endif
