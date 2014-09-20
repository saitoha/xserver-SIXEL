#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
#include <stdio.h>
#include <pthread.h>
#include "kdrive.h"
#include "opaque.h"

extern char *kdExecuteCommand;

static void *child_command(void *unused)
{
    FILE *cmd;
    char buf[512];

    sprintf(buf, ":%s", display);
    fprintf(stderr, "setenv DISPLAY=%s", buf);
    setenv("DISPLAY", buf, 1);
    fprintf(stderr, "Starting child command: %s", kdExecuteCommand);
    cmd = popen(kdExecuteCommand, "r");
    if (!cmd) {
        fprintf(stderr, "Error while starting child command: %s", kdExecuteCommand);
        return NULL;
    }
    while (fgets(buf, sizeof(buf), cmd)) {
        fprintf(stderr, "> %s", buf);
    }
    fprintf(stderr, "Child command returned with status %d", pclose (cmd));
    return NULL;
}

void KdExecuteChildCommand(void)
{
    pthread_t thread_id;
    if (!kdExecuteCommand)
        return;
    pthread_create(&thread_id, NULL, &child_command, NULL);
}
