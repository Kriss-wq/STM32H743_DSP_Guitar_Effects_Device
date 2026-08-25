SET(SOURCES screens/ui_Screen1.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    fonts/ui_font_Font1.c
    fonts/ui_font_chinese48.c
        ui_buttons.c
        ui_center_labels.c
)

add_library(ui ${SOURCES})
