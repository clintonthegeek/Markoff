// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>
#include "qutepart.h"

namespace Qutepart {

class Qutepart;

class CompletionList;
class CompletionModel;

class Completer : public QObject {
    Q_OBJECT

  public:
    Completer(Qutepart *qpart);
    ~Completer();

    void setKeywords(const QSet<QString> &keywords);
    void setCustomCompletions(const QSet<CompletionItem> &wordSet);

    bool isVisible() const;
    bool invokeCompletionIfAvailable(bool requestedByUser);

  public slots:
    void invokeCompletion();

  private slots:
    void onTextChanged();
    void onModificationChanged(bool modified);
    void onCompletionListItemSelected(int index);
    void onCompletionListTabPressed();

  private:
    void updateWordSet();
    bool shouldShowModel(CompletionModel *model, bool forceShow);
    void createWidget(CompletionModel *model);
    void closeCompletion();
    QString getWordBeforeCursor() const;
    QString getWordAfterCursor() const;

    Qutepart *qpart_;
    CompletionList *widget_ = nullptr;
    bool completionOpenedManually_;
    QSet<QString> keywords_;
    QSet<CompletionItem> customCompletions_;
    QSet<CompletionItem> wordSet_;
    QTimer updateWordSetTimer_;
};


} // namespace Qutepart
