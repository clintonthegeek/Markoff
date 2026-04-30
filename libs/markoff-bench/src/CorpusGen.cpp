// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/CorpusGen.h>

#include <QByteArray>
#include <QString>

#include <random>
#include <array>

namespace Markoff::Bench {

namespace {

struct ProfileSpec {
    const char *name;
    qsizetype   targetBytes;
    double      inlineDensity;
    double      codeShare;
    double      tableShare;
    int         footnoteCount;
    int         maxNestingDepth;
};

constexpr std::array<ProfileSpec, kCorpusProfileCount> kProfiles{{
    {"tiny",               1024,        0.05, 0.00, 0.00,   0, 1},
    {"mid_prose",          16  * 1024,  0.30, 0.00, 0.00,   0, 1},
    {"mid_mixed",          16  * 1024,  0.30, 0.20, 0.10,  25, 4},
    {"big_prose",          100 * 1024,  0.30, 0.00, 0.00,   0, 1},
    {"big_code_heavy",     100 * 1024,  0.05, 0.60, 0.00,   0, 1},
    {"big_table_heavy",    100 * 1024,  0.05, 0.00, 0.40,   0, 1},
    {"big_footnote_heavy",  92 * 1024,  0.30, 0.10, 0.00, 200, 1},
    {"huge",               500 * 1024,  0.30, 0.20, 0.10,  50, 4},
    {"pathological",       2000 * 1024, 0.60, 0.30, 0.20, 200, 8},
}};

constexpr const char *kWords[] = {
    "alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel",
    "india", "juliet", "kilo", "lima", "mike", "november", "oscar", "papa",
    "quebec", "romeo", "sierra", "tango", "uniform", "victor", "whiskey",
    "xray", "yankee", "zulu", "tree", "stone", "river", "cloud", "ember",
    "lattice", "iron", "frost", "moss", "harbour", "thread", "ribbon",
};
constexpr int kWordCount = sizeof(kWords) / sizeof(kWords[0]);

class Rng {
public:
    explicit Rng(quint64 seed) : m_engine(seed) {}
    int     uniformInt(int lo, int hi) { std::uniform_int_distribution<int> d(lo, hi); return d(m_engine); }
    double  uniformDbl()               { std::uniform_real_distribution<double> d(0.0, 1.0); return d(m_engine); }
    const char *word()                 { return kWords[uniformInt(0, kWordCount - 1)]; }
private:
    std::mt19937_64 m_engine;
};

void emitWords(QByteArray &out, Rng &rng, int n) {
    for (int i = 0; i < n; ++i) {
        if (i) out.append(' ');
        out.append(rng.word());
    }
}

void emitParagraph(QByteArray &out, Rng &rng, double inlineDensity,
                   int footnoteRemaining, int &footnotesEmitted)
{
    const int wordCount = rng.uniformInt(40, 120);
    int wordsEmitted = 0;
    while (wordsEmitted < wordCount) {
        const int chunk = std::min(rng.uniformInt(6, 12), wordCount - wordsEmitted);
        if (rng.uniformDbl() < inlineDensity) {
            const int kind = rng.uniformInt(0, 3);
            if (kind == 0) { out.append("**"); emitWords(out, rng, chunk); out.append("**"); }
            else if (kind == 1) { out.append('*'); emitWords(out, rng, chunk); out.append('*'); }
            else if (kind == 2) { out.append('`'); emitWords(out, rng, chunk); out.append('`'); }
            else {
                out.append('['); emitWords(out, rng, chunk);
                out.append("](https://example.invalid/");
                out.append(rng.word()); out.append(')');
            }
        } else {
            emitWords(out, rng, chunk);
        }
        wordsEmitted += chunk;
        if (wordsEmitted < wordCount) out.append(' ');
        if (footnotesEmitted < footnoteRemaining && rng.uniformDbl() < 0.05) {
            ++footnotesEmitted;
            out.append("[^"); out.append(QByteArray::number(footnotesEmitted)); out.append(']');
        }
    }
    out.append("\n\n");
}

void emitCodeBlock(QByteArray &out, Rng &rng) {
    out.append("```cpp\n");
    const int lines = rng.uniformInt(6, 20);
    for (int i = 0; i < lines; ++i) {
        const int n = rng.uniformInt(3, 8);
        emitWords(out, rng, n);
        out.append(";\n");
    }
    out.append("```\n\n");
}

void emitTable(QByteArray &out, Rng &rng) {
    const int cols = rng.uniformInt(3, 5);
    const int rows = rng.uniformInt(4, 12);
    out.append("|");
    for (int c = 0; c < cols; ++c) { out.append(' '); out.append(rng.word()); out.append(" |"); }
    out.append("\n|");
    for (int c = 0; c < cols; ++c) out.append("---|");
    out.append('\n');
    for (int r = 0; r < rows; ++r) {
        out.append("|");
        for (int c = 0; c < cols; ++c) { out.append(' '); out.append(rng.word()); out.append(" |"); }
        out.append('\n');
    }
    out.append('\n');
}

void emitFootnoteDefinitions(QByteArray &out, Rng &rng, int count) {
    for (int i = 1; i <= count; ++i) {
        out.append("[^"); out.append(QByteArray::number(i)); out.append("]: ");
        emitWords(out, rng, rng.uniformInt(8, 16));
        out.append('\n');
    }
    if (count > 0) out.append('\n');
}

void emitNestedBlockquote(QByteArray &out, Rng &rng, int depth, double inlineDensity) {
    QByteArray prefix;
    for (int i = 0; i < depth; ++i) prefix.append("> ");
    const int lines = rng.uniformInt(2, 5);
    for (int i = 0; i < lines; ++i) {
        out.append(prefix);
        emitWords(out, rng, rng.uniformInt(6, 12));
        if (rng.uniformDbl() < inlineDensity) {
            out.append(" *"); out.append(rng.word()); out.append('*');
        }
        out.append('\n');
    }
    out.append('\n');
}

}  // namespace

const char *profileName(CorpusProfile p) {
    return kProfiles[static_cast<int>(p)].name;
}

QByteArray generate(CorpusProfile profile, quint64 seed) {
    const ProfileSpec &spec = kProfiles[static_cast<int>(profile)];
    Rng rng(seed);
    QByteArray out;
    out.reserve(static_cast<int>(spec.targetBytes + 4096));

    out.append("---\ntitle: bench corpus ");
    out.append(spec.name);
    out.append("\n---\n\n# Synthetic Markdown — ");
    out.append(spec.name);
    out.append("\n\n");

    int footnotesEmitted = 0;
    while (out.size() < spec.targetBytes) {
        const double r = rng.uniformDbl();
        if (r < spec.codeShare) {
            emitCodeBlock(out, rng);
        } else if (r < spec.codeShare + spec.tableShare) {
            emitTable(out, rng);
        } else if (spec.maxNestingDepth > 1 && rng.uniformDbl() < 0.10) {
            const int depth = rng.uniformInt(1, spec.maxNestingDepth);
            emitNestedBlockquote(out, rng, depth, spec.inlineDensity);
        } else {
            emitParagraph(out, rng, spec.inlineDensity, spec.footnoteCount, footnotesEmitted);
        }
    }
    emitFootnoteDefinitions(out, rng, footnotesEmitted);
    return out;
}

}  // namespace Markoff::Bench
