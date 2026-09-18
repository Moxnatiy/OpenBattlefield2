#pragma once
// `--calibrate <capture>`: matching the server's template numbers to names.
//
// The server sends objects by template number, and the number is the order of a
// template's creation (`ObjectTemplateManager::createTemplate`), so the name
// cannot be got from the number itself. But the positions match: we read the
// level ourselves and know what stands where, and the server gives numbers for
// those same places. The run also checks the guess that the number **is** the
// creation order, against our own list.
//
// The packet file is made by `tools/linuxded/capture.py --stage world --out`.
#include "obf2/app/command_line.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

int runCalibrate(const Args& args, FileSystem& files);

}  // namespace obf2::app
