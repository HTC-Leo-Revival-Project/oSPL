/*
 * flash.h - the commands that touch the NAND.
 */
#ifndef SPL_FLASH_H
#define SPL_FLASH_H

/*
 * These live apart from the menu because there are more of them every time the
 * flash grows, and the menu should not have to know what any of them do.
 */
struct bl_flash_command {
    const char *name;
    void (*handler)(char **args, int nargs);
    const char *help;
};

extern const struct bl_flash_command flash_commands[];
extern const int flash_command_count;

#endif /* SPL_FLASH_H */