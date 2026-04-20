// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "char_iterator.h"

namespace Qutepart {

CharIterator::CharIterator(const TextPosition &position) : position_(position) {}

QChar CharIterator::step() {
    if (!atEnd()) {
        auto s = position_.block.text();
        auto i = position_.column;
        QChar retVal = QChar::Null;
        if (i < s.length()) {
            retVal = s[i];
        }
        previousPosition_ = position_;
        movePosition();
        return retVal;
    } else {
        return QChar::Null;
    }
}

TextPosition CharIterator::currentPosition() const { return position_; }

TextPosition CharIterator::previousPosition() const { return previousPosition_; }

bool CharIterator::atEnd() const { return (!position_.block.isValid()); }

void ForwardCharIterator::movePosition() {
    int blockLength = position_.block.text().length();

    while (1) {
        if (position_.column < (blockLength - 1)) {
            position_.column++;
            break;
        } else {
            position_.block = position_.block.next();

            while (position_.block.isValid() && position_.block.text().isEmpty()) {
                position_.block = position_.block.next();
            }

            if (!position_.block.isValid()) {
                break;
            }

            position_.column = -1;
            /* move block backward, but the block might be empty
               Go to next while loop iteration and move back
               more blocks if necessary
             */
        }
    }
}

void BackwardCharIterator::movePosition() {
    while (1) {
        if (position_.column > 0) {
            position_.column--;
            break;
        } else {
            position_.block = position_.block.previous();
            if (!position_.block.isValid()) {
                break;
            }

            position_.column = position_.block.length() - 1;
            /* move block backward, but the block might be empty
               Go to next while loop iteration and move back
               more blocks if necessary
             */
        }
    }
}

} // namespace Qutepart
