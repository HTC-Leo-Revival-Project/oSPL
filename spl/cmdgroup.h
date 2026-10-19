/*
 * command group - a table of commands the menu can dispatch to.
 *
 * The menu has one of these; so does everything else that adds commands, so
 * that the menu never has to know what any of them do.
 */
#ifndef SPL_COMMAND_GROUP_H
#define SPL_COMMAND_GROUP_H

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

#endif /* SPL_COMMAND_GROUP_H */