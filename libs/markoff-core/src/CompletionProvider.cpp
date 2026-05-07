// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/CompletionProvider.h>

namespace Markoff {

CompletionProvider::CompletionProvider(QObject *parent) : QObject(parent) {}
CompletionProvider::~CompletionProvider() = default;

}  // namespace Markoff
