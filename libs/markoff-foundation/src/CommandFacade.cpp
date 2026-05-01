// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/CommandFacade.h>

#include <markoff-foundation/Cmd.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>

#include "AnchorConversion.h"

namespace Markoff {

namespace {
/// Cmd:: anchor-taking receivers (toggleCheckbox, insertTable, ...) take
/// CollabText::Crdt::Anchor and have callers in foundation tests that pass
/// raw doc.anchorAt(...) results. We keep their signatures unchanged and
/// convert at the Selection boundary here.
inline CollabText::Crdt::Anchor crdt(const TextAnchor &t) noexcept
{
    return Detail::toCrdtAnchor(t);
}
}  // namespace


CommandFacade::CommandFacade(QObject *parent) : QObject(parent) {}
CommandFacade::~CommandFacade() = default;

MarkoffDocument *CommandFacade::document() const { return m_doc; }
void CommandFacade::setDocument(MarkoffDocument *d)
{ if (m_doc != d) { m_doc = d; Q_EMIT documentChanged(); } }

Session *CommandFacade::session() const { return m_sess; }
void CommandFacade::setSession(Session *s)
{ if (m_sess != s) { m_sess = s; Q_EMIT sessionChanged(); } }

void CommandFacade::toggleBold()
{ if (m_doc && m_sess) Cmd::toggleBold(*m_doc, m_sess->primarySelection()); }
void CommandFacade::toggleItalic()
{ if (m_doc && m_sess) Cmd::toggleItalic(*m_doc, m_sess->primarySelection()); }
void CommandFacade::toggleStrikethrough()
{ if (m_doc && m_sess) Cmd::toggleStrikethrough(*m_doc, m_sess->primarySelection()); }
void CommandFacade::toggleInlineCode()
{ if (m_doc && m_sess) Cmd::toggleInlineCode(*m_doc, m_sess->primarySelection()); }
void CommandFacade::setHeading(int level)
{ if (m_doc && m_sess) Cmd::setHeading(*m_doc, m_sess->primarySelection(), level); }
void CommandFacade::toggleCheckbox()
{ if (m_doc && m_sess) Cmd::toggleCheckbox(*m_doc, crdt(m_sess->primarySelection().active)); }
void CommandFacade::blockQuote()
{ if (m_doc && m_sess) Cmd::blockQuote(*m_doc, m_sess->primarySelection()); }
void CommandFacade::insertTable(int rows, int cols, bool hasHeader)
{ if (m_doc && m_sess) Cmd::insertTable(*m_doc, crdt(m_sess->primarySelection().active),
                                          rows, cols, hasHeader); }
void CommandFacade::insertLink(const QString &t, const QString &u)
{ if (m_doc && m_sess) Cmd::insertLink(*m_doc, crdt(m_sess->primarySelection().active), t, u); }
void CommandFacade::insertImage(const QString &a, const QString &u)
{ if (m_doc && m_sess) Cmd::insertImage(*m_doc, crdt(m_sess->primarySelection().active), a, u); }
void CommandFacade::insertHorizontalRule()
{ if (m_doc && m_sess) Cmd::insertHorizontalRule(*m_doc,
                                                   crdt(m_sess->primarySelection().active)); }
void CommandFacade::undo() { if (m_doc) Cmd::undo(*m_doc); }
void CommandFacade::redo() { if (m_doc) Cmd::redo(*m_doc); }

}  // namespace Markoff
