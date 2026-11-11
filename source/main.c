#include "app.h"

int main(void)
{
    sd_init();
    sd_load_cfg();
    ui_init();
    audio_init();

    touchPosition tp;
    memset(&tp, 0, sizeof tp);

    for (;;) {
        scanKeys();
        u32 down = keysDown();
        u32 held = keysHeld();
        if (held & KEY_TOUCH) touchRead(&tp);

        ui_input(down, held, &tp);
        audio_update();
        ui_draw();

        swiWaitForVBlank();
    }
    return 0;
}
