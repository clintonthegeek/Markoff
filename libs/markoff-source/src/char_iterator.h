// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include "text_pos.h"

namespace Qutepart {

class CharIterator {
  public:
    // create iterator and make first step
    CharIterator(const TextPosition &position);
    virtual ~CharIterator() = default;

    QChar step(); // return current character and then make step back
    TextPosition previousPosition() const;
    TextPosition currentPosition() const;
    bool atEnd() const;

  protected:
    TextPosition previousPosition_;
    TextPosition position_;

    virtual void movePosition() = 0;
};

class ForwardCharIterator : public CharIterator {
    using CharIterator::CharIterator;

  private:
    virtual void movePosition() override;
};

class BackwardCharIterator : public CharIterator {
    using CharIterator::CharIterator;

  private:
    virtual void movePosition() override;
};

} // namespace Qutepart
