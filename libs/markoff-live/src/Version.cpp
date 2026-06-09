// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/Version.h>

#ifndef MARKOFF_LIVE_VERSION_MAJOR
#define MARKOFF_LIVE_VERSION_MAJOR 0
#endif
#ifndef MARKOFF_LIVE_VERSION_MINOR
#define MARKOFF_LIVE_VERSION_MINOR 0
#endif
#ifndef MARKOFF_LIVE_VERSION_PATCH
#define MARKOFF_LIVE_VERSION_PATCH 0
#endif

namespace Markoff::Live {

quint32 version() noexcept
{
    return MARKOFF_LIVE_VERSION_MAJOR * 10000u
         + MARKOFF_LIVE_VERSION_MINOR * 100u
         + MARKOFF_LIVE_VERSION_PATCH;
}

}  // namespace Markoff::Live
