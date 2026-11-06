#ifndef PROGRAM_RUNTIME_H
#define PROGRAM_RUNTIME_H

#include <Arduino.h>
#include <stdint.h>

// Reports that NARM launching is deferred in this GPU-terminal milestone.
int8_t launch_program_sd(const String &program_path);
int8_t launch_program_fs(const String &program_path);

#endif
