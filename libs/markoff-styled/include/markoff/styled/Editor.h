// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkdownView.h>
#include <markoff/styled/MarkoffStyledExport.h>

namespace Markoff::Styled {

class MARKOFF_STYLED_EXPORT Editor : public Markoff::MarkdownView {
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;
};

}  // namespace Markoff::Styled
