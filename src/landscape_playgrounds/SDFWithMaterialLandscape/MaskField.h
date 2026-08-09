// The mask field graduated into the shared library (highground_core) for
// the editor's mask3d assets; this shim keeps the playground's call sites
// unchanged. New code should use mask:: directly.
#pragma once

#include <highground_core/mask_field.h>

namespace maskfield = mask;
