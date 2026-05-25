// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_YAMLVALUE_H
#define MARKOFF_YAMLVALUE_H

#include <memory>
#include <functional>
#include <cstdint>
#include <QString>
#include <QStringList>

// Forward-declare ryml types to keep them out of the public header
namespace c4 { namespace yml { class Tree; } }

namespace Markoff {

/// Lightweight view into a parsed YAML tree (backed by ryml).
///
/// Holds a shared_ptr<ryml::Tree> + node index. Scalar access converts
/// ryml::csubstr → QString on demand; no eager copies.
///
/// For mutation, call clone() first — the Document's original tree is
/// immutable after construction. Mutation methods operate on the cloned tree.
class YamlValue
{
public:
    enum class Kind { Null, Bool, Int, Double, String, Seq, Map };

    /// Constructs an empty (Null) value.
    YamlValue();
    ~YamlValue();

    YamlValue(const YamlValue &other);
    YamlValue &operator=(const YamlValue &other);
    YamlValue(YamlValue &&other) noexcept;
    YamlValue &operator=(YamlValue &&other) noexcept;

    // --- Type queries ---

    Kind kind() const;
    bool isNull() const;
    bool isBool() const;
    bool isInt() const;
    bool isDouble() const;
    bool isString() const;
    bool isScalar() const;
    bool isSeq() const;
    bool isMap() const;

    // --- Scalar access ---

    bool asBool() const;
    int64_t asInt() const;
    double asDouble() const;
    QString asString() const;

    // --- Container access ---

    int size() const;
    YamlValue at(int index) const;
    bool contains(const QString &key) const;
    YamlValue get(const QString &key) const;
    QStringList keys() const;
    void forEach(std::function<void(const QString &, const YamlValue &)> fn) const;

    // --- Convenience ---

    QStringList asStringList() const;

    // --- Mutation (use on clone()d copies) ---

    YamlValue clone() const;
    void setString(const QString &key, const QString &value);
    void setInt(const QString &key, int64_t value);
    void setDouble(const QString &key, double value);
    void setBool(const QString &key, bool value);
    void setNull(const QString &key);
    void setSeq(const QString &key, const QStringList &values);
    YamlValue setMap(const QString &key);
    YamlValue setSeqNode(const QString &key);
    void remove(const QString &key);
    YamlValue appendMap();
    void appendString(const QString &value);

    // --- Emit ---

    /// Emit the YAML body (no --- delimiters). Preserves node styles for
    /// round-trip fidelity. For null values, emits "key:" (empty) matching
    /// Obsidian convention.
    QString stringify() const;

    // --- Factory (used internally by Document) ---

    /// Parse a raw YAML string (no --- delimiters) into a YamlValue.
    /// On parse error, returns an empty (Null) value and sets errorOut.
    static YamlValue parse(const QString &yaml, QString *errorOut = nullptr);

    /// Construct an empty map suitable for building frontmatter from scratch.
    static YamlValue emptyMap();

private:
    struct Private;
    std::shared_ptr<Private> d;

    YamlValue(std::shared_ptr<Private> priv, std::size_t nodeId);
};

} // namespace Markoff

#endif // MARKOFF_YAMLVALUE_H
