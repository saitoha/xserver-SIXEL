/* Minimal entry point for Xsixel, delegating to dix_main */
#include "xorg-server.h"

int dix_main(int argc, char *argv[], char *envp[]);

int main(int argc, char *argv[], char *envp[])
{
    return dix_main(argc, argv, envp);
}

