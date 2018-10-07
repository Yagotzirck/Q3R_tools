#ifndef SSH_UTILS_H
#define SSH_UTILS_H

#include <stdbool.h>

#include "types.h"

// functions' prototypes
bool init_sshHandle(sshHandle_t *sshHandle, const char *sshPath);
bool ssh_convertAndSave(sshHandle_t *sshHandle, outFormat_t outFormat);
void free_sshHandleBuffers(sshHandle_t *sshHandle);

#endif // SSH_UTILS_H
