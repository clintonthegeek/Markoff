// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-parser/YamlValue.h>

#include <ryml.hpp>
#include <ryml_std.hpp>
#include <c4/format.hpp>

#include <QByteArray>

namespace Markoff {

// ---------------------------------------------------------------------------
// Private data
// ---------------------------------------------------------------------------

struct YamlValue::Private {
    std::shared_ptr<ryml::Tree> tree;
    ryml::id_type nodeId = ryml::NONE;
};

// ---------------------------------------------------------------------------
// YAML 1.2 Core Schema type resolution
// ---------------------------------------------------------------------------

static bool isYaml12Bool(ryml::csubstr s)
{
    return s == "true" || s == "True" || s == "TRUE"
        || s == "false" || s == "False" || s == "FALSE";
}

static bool isYaml12Null(ryml::csubstr s)
{
    return s == "null" || s == "Null" || s == "NULL" || s == "~" || s.empty();
}

static bool isYaml12Int(ryml::csubstr s, int64_t *out)
{
    if (s.empty()) return false;

    // Handle sign
    ryml::csubstr digits = s;
    bool negative = false;
    if (digits.begins_with('-')) { negative = true; digits = digits.sub(1); }
    else if (digits.begins_with('+')) { digits = digits.sub(1); }
    if (digits.empty()) return false;

    int64_t val = 0;
    bool ok = false;

    if (digits.begins_with("0x") || digits.begins_with("0X")) {
        // Hex
        digits = digits.sub(2);
        if (digits.empty()) return false;
        for (auto c : digits) {
            if (c >= '0' && c <= '9') { val = val * 16 + (c - '0'); ok = true; }
            else if (c >= 'a' && c <= 'f') { val = val * 16 + (c - 'a' + 10); ok = true; }
            else if (c >= 'A' && c <= 'F') { val = val * 16 + (c - 'A' + 10); ok = true; }
            else return false;
        }
    } else if (digits.begins_with("0o") || digits.begins_with("0O")) {
        // Octal
        digits = digits.sub(2);
        if (digits.empty()) return false;
        for (auto c : digits) {
            if (c >= '0' && c <= '7') { val = val * 8 + (c - '0'); ok = true; }
            else return false;
        }
    } else {
        // Decimal
        for (auto c : digits) {
            if (c >= '0' && c <= '9') { val = val * 10 + (c - '0'); ok = true; }
            else return false;
        }
    }

    if (ok && out) *out = negative ? -val : val;
    return ok;
}

static bool isYaml12Double(ryml::csubstr s, double *out)
{
    if (s.empty()) return false;
    if (s == ".inf" || s == ".Inf" || s == ".INF") {
        if (out) *out = std::numeric_limits<double>::infinity();
        return true;
    }
    if (s == "-.inf" || s == "-.Inf" || s == "-.INF") {
        if (out) *out = -std::numeric_limits<double>::infinity();
        return true;
    }
    if (s == ".nan" || s == ".NaN" || s == ".NAN") {
        if (out) *out = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    // Must contain a dot or 'e'/'E' to be a float (distinguish from int)
    bool hasDot = false, hasE = false;
    for (auto c : s) {
        if (c == '.') hasDot = true;
        if (c == 'e' || c == 'E') hasE = true;
    }
    if (!hasDot && !hasE) return false;

    // Parse via QString for robustness
    QString qs = QString::fromUtf8(s.str, static_cast<int>(s.len));
    bool ok = false;
    double val = qs.toDouble(&ok);
    if (ok && out) *out = val;
    return ok;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static ryml::csubstr qstringToCsubstr(const QByteArray &utf8)
{
    return ryml::csubstr(utf8.constData(), static_cast<size_t>(utf8.size()));
}

static QString csubstrToQString(ryml::csubstr s)
{
    return QString::fromUtf8(s.str, static_cast<int>(s.len));
}

/// Find or create a child key in a map node. Returns child id.
static ryml::id_type findOrCreateKey(ryml::Tree &tree, ryml::id_type parent, ryml::csubstr key)
{
    for (ryml::id_type ch = tree.first_child(parent); ch != ryml::NONE; ch = tree.next_sibling(ch)) {
        if (tree.has_key(ch) && tree.key(ch) == key)
            return ch;
    }
    // Not found — append a new keyval child
    ryml::id_type ch = tree.append_child(parent);
    tree.to_keyval(ch, key, {});
    return ch;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

YamlValue::YamlValue()
    : d(std::make_shared<Private>())
{
}

YamlValue::~YamlValue() = default;
YamlValue::YamlValue(const YamlValue &other) = default;
YamlValue &YamlValue::operator=(const YamlValue &other) = default;
YamlValue::YamlValue(YamlValue &&other) noexcept = default;
YamlValue &YamlValue::operator=(YamlValue &&other) noexcept = default;

YamlValue::YamlValue(std::shared_ptr<Private> priv, std::size_t nodeId)
    : d(std::move(priv))
{
    d->nodeId = static_cast<ryml::id_type>(nodeId);
}

// ---------------------------------------------------------------------------
// Type queries
// ---------------------------------------------------------------------------

static bool nodeValid(const std::shared_ptr<ryml::Tree> &tree, ryml::id_type nodeId)
{
    return tree && nodeId != ryml::NONE && nodeId < tree->capacity();
}

YamlValue::Kind YamlValue::kind() const
{
    if (!nodeValid(d->tree, d->nodeId)) return Kind::Null;

    const auto &tree = *d->tree;
    auto id = d->nodeId;

    if (tree.is_map(id)) return Kind::Map;
    if (tree.is_seq(id)) return Kind::Seq;

    // Scalar node
    if (!tree.has_val(id)) return Kind::Null;

    ryml::csubstr val = tree.val(id);

    // Quoted scalars are always String
    if (tree.is_val_quoted(id)) return Kind::String;

    if (isYaml12Null(val)) return Kind::Null;
    if (isYaml12Bool(val)) return Kind::Bool;

    int64_t dummy_i;
    if (isYaml12Int(val, &dummy_i)) return Kind::Int;

    double dummy_d;
    if (isYaml12Double(val, &dummy_d)) return Kind::Double;

    return Kind::String;
}

bool YamlValue::isNull() const { return kind() == Kind::Null; }
bool YamlValue::isBool() const { return kind() == Kind::Bool; }
bool YamlValue::isInt() const { return kind() == Kind::Int; }
bool YamlValue::isDouble() const { return kind() == Kind::Double; }
bool YamlValue::isString() const { return kind() == Kind::String; }
bool YamlValue::isScalar() const
{
    auto k = kind();
    return k == Kind::Bool || k == Kind::Int || k == Kind::Double || k == Kind::String;
}
bool YamlValue::isSeq() const { return kind() == Kind::Seq; }
bool YamlValue::isMap() const { return kind() == Kind::Map; }

// ---------------------------------------------------------------------------
// Scalar access
// ---------------------------------------------------------------------------

bool YamlValue::asBool() const
{
    if (!nodeValid(d->tree, d->nodeId) || !d->tree->has_val(d->nodeId)) return false;
    ryml::csubstr val = d->tree->val(d->nodeId);
    return val == "true" || val == "True" || val == "TRUE";
}

int64_t YamlValue::asInt() const
{
    if (!nodeValid(d->tree, d->nodeId) || !d->tree->has_val(d->nodeId)) return 0;
    int64_t result = 0;
    isYaml12Int(d->tree->val(d->nodeId), &result);
    return result;
}

double YamlValue::asDouble() const
{
    if (!nodeValid(d->tree, d->nodeId) || !d->tree->has_val(d->nodeId)) return 0.0;
    double result = 0.0;
    isYaml12Double(d->tree->val(d->nodeId), &result);
    return result;
}

QString YamlValue::asString() const
{
    if (!nodeValid(d->tree, d->nodeId)) return {};
    if (!d->tree->has_val(d->nodeId)) return {};
    return csubstrToQString(d->tree->val(d->nodeId));
}

// ---------------------------------------------------------------------------
// Container access
// ---------------------------------------------------------------------------

int YamlValue::size() const
{
    if (!nodeValid(d->tree, d->nodeId)) return 0;
    return static_cast<int>(d->tree->num_children(d->nodeId));
}

YamlValue YamlValue::at(int index) const
{
    if (!nodeValid(d->tree, d->nodeId)) return {};
    auto id = d->tree->child(d->nodeId, static_cast<ryml::id_type>(index));
    if (id == ryml::NONE) return {};
    auto priv = std::make_shared<Private>();
    priv->tree = d->tree;
    priv->nodeId = id;
    return YamlValue(priv, id);
}

bool YamlValue::contains(const QString &key) const
{
    if (!nodeValid(d->tree, d->nodeId) || !d->tree->is_map(d->nodeId)) return false;
    QByteArray utf8 = key.toUtf8();
    return d->tree->find_child(d->nodeId, qstringToCsubstr(utf8)) != ryml::NONE;
}

YamlValue YamlValue::get(const QString &key) const
{
    if (!nodeValid(d->tree, d->nodeId) || !d->tree->is_map(d->nodeId)) return {};
    QByteArray utf8 = key.toUtf8();
    auto id = d->tree->find_child(d->nodeId, qstringToCsubstr(utf8));
    if (id == ryml::NONE) return {};
    auto priv = std::make_shared<Private>();
    priv->tree = d->tree;
    priv->nodeId = id;
    return YamlValue(priv, id);
}

QStringList YamlValue::keys() const
{
    if (!nodeValid(d->tree, d->nodeId) || !d->tree->is_map(d->nodeId)) return {};
    QStringList result;
    for (auto ch = d->tree->first_child(d->nodeId); ch != ryml::NONE; ch = d->tree->next_sibling(ch)) {
        if (d->tree->has_key(ch))
            result.append(csubstrToQString(d->tree->key(ch)));
    }
    return result;
}

void YamlValue::forEach(std::function<void(const QString &, const YamlValue &)> fn) const
{
    if (!nodeValid(d->tree, d->nodeId) || !d->tree->is_map(d->nodeId) || !fn) return;
    for (auto ch = d->tree->first_child(d->nodeId); ch != ryml::NONE; ch = d->tree->next_sibling(ch)) {
        if (!d->tree->has_key(ch)) continue;
        QString k = csubstrToQString(d->tree->key(ch));
        auto priv = std::make_shared<Private>();
        priv->tree = d->tree;
        priv->nodeId = ch;
        fn(k, YamlValue(priv, ch));
    }
}

// ---------------------------------------------------------------------------
// Convenience
// ---------------------------------------------------------------------------

QStringList YamlValue::asStringList() const
{
    if (isSeq()) {
        QStringList result;
        int n = size();
        for (int i = 0; i < n; ++i)
            result.append(at(i).asString());
        return result;
    }
    if (isString())
        return {asString()};
    return {};
}

// ---------------------------------------------------------------------------
// Clone
// ---------------------------------------------------------------------------

YamlValue YamlValue::clone() const
{
    if (!nodeValid(d->tree, d->nodeId)) return emptyMap();

    // Deep-copy via duplicate_contents into a fresh tree
    auto newTree = std::make_shared<ryml::Tree>();
    newTree->reserve(d->tree->capacity());
    newTree->reserve_arena(d->tree->arena_size());

    ryml::id_type newRoot = newTree->root_id();
    newTree->duplicate_contents(d->tree.get(), d->nodeId, newRoot);

    auto priv = std::make_shared<Private>();
    priv->tree = std::move(newTree);
    priv->nodeId = newRoot;
    return YamlValue(priv, newRoot);
}

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

static void ensureMap(std::shared_ptr<ryml::Tree> &tree, ryml::id_type &nodeId)
{
    if (!tree) {
        tree = std::make_shared<ryml::Tree>();
        nodeId = tree->root_id();
        tree->to_map(nodeId);
    }
    if (!tree->is_map(nodeId)) {
        tree->to_map(nodeId);
    }
}

void YamlValue::setString(const QString &key, const QString &value)
{
    ensureMap(d->tree, d->nodeId);
    QByteArray keyUtf8 = key.toUtf8();
    QByteArray valUtf8 = value.toUtf8();
    ryml::csubstr keyView = d->tree->copy_to_arena(qstringToCsubstr(keyUtf8));
    ryml::csubstr valView = d->tree->copy_to_arena(qstringToCsubstr(valUtf8));
    auto ch = findOrCreateKey(*d->tree, d->nodeId, keyView);
    d->tree->to_keyval(ch, keyView, valView);
}

void YamlValue::setInt(const QString &key, int64_t value)
{
    setString(key, QString::number(value));
}

void YamlValue::setDouble(const QString &key, double value)
{
    setString(key, QString::number(value, 'g', 15));
}

void YamlValue::setBool(const QString &key, bool value)
{
    setString(key, value ? QStringLiteral("true") : QStringLiteral("false"));
}

void YamlValue::setNull(const QString &key)
{
    ensureMap(d->tree, d->nodeId);
    QByteArray keyUtf8 = key.toUtf8();
    ryml::csubstr keyView = d->tree->copy_to_arena(qstringToCsubstr(keyUtf8));
    auto ch = findOrCreateKey(*d->tree, d->nodeId, keyView);
    d->tree->to_keyval(ch, keyView, {});
}

void YamlValue::setSeq(const QString &key, const QStringList &values)
{
    ensureMap(d->tree, d->nodeId);
    QByteArray keyUtf8 = key.toUtf8();
    ryml::csubstr keyView = d->tree->copy_to_arena(qstringToCsubstr(keyUtf8));
    auto ch = findOrCreateKey(*d->tree, d->nodeId, keyView);
    // Must clear node type before converting val→seq
    d->tree->_clear_type(ch);
    d->tree->to_seq(ch, keyView);
    for (const auto &v : values) {
        QByteArray vUtf8 = v.toUtf8();
        ryml::csubstr vView = d->tree->copy_to_arena(qstringToCsubstr(vUtf8));
        auto item = d->tree->append_child(ch);
        d->tree->to_val(item, vView);
    }
}

YamlValue YamlValue::setMap(const QString &key)
{
    ensureMap(d->tree, d->nodeId);
    QByteArray keyUtf8 = key.toUtf8();
    ryml::csubstr keyView = d->tree->copy_to_arena(qstringToCsubstr(keyUtf8));
    auto ch = findOrCreateKey(*d->tree, d->nodeId, keyView);
    d->tree->_clear_type(ch);
    d->tree->to_map(ch, keyView);
    auto priv = std::make_shared<Private>();
    priv->tree = d->tree;
    priv->nodeId = ch;
    return YamlValue(priv, ch);
}

YamlValue YamlValue::setSeqNode(const QString &key)
{
    ensureMap(d->tree, d->nodeId);
    QByteArray keyUtf8 = key.toUtf8();
    ryml::csubstr keyView = d->tree->copy_to_arena(qstringToCsubstr(keyUtf8));
    auto ch = findOrCreateKey(*d->tree, d->nodeId, keyView);
    d->tree->_clear_type(ch);
    d->tree->to_seq(ch, keyView);
    auto priv = std::make_shared<Private>();
    priv->tree = d->tree;
    priv->nodeId = ch;
    return YamlValue(priv, ch);
}

void YamlValue::remove(const QString &key)
{
    if (!nodeValid(d->tree, d->nodeId) || !d->tree->is_map(d->nodeId)) return;
    QByteArray utf8 = key.toUtf8();
    auto ch = d->tree->find_child(d->nodeId, qstringToCsubstr(utf8));
    if (ch != ryml::NONE)
        d->tree->remove(ch);
}

YamlValue YamlValue::appendMap()
{
    if (!nodeValid(d->tree, d->nodeId)) return {};
    auto ch = d->tree->append_child(d->nodeId);
    d->tree->to_map(ch);
    auto priv = std::make_shared<Private>();
    priv->tree = d->tree;
    priv->nodeId = ch;
    return YamlValue(priv, ch);
}

void YamlValue::appendString(const QString &value)
{
    if (!nodeValid(d->tree, d->nodeId)) return;
    QByteArray utf8 = value.toUtf8();
    ryml::csubstr valView = d->tree->copy_to_arena(qstringToCsubstr(utf8));
    auto ch = d->tree->append_child(d->nodeId);
    d->tree->to_val(ch, valView);
}

// ---------------------------------------------------------------------------
// Stringify
// ---------------------------------------------------------------------------

QString YamlValue::stringify() const
{
    if (!nodeValid(d->tree, d->nodeId)) return {};

    std::string buf;
    ryml::ConstNodeRef node = d->tree->cref(d->nodeId);
    buf = ryml::emitrs_yaml<std::string>(node);

    // Trim trailing newline for consistency (caller adds delimiters)
    while (!buf.empty() && buf.back() == '\n')
        buf.pop_back();

    return QString::fromUtf8(buf.data(), static_cast<int>(buf.size()));
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

YamlValue YamlValue::parse(const QString &yaml, QString *errorOut)
{
    if (yaml.isEmpty()) return {};

    QByteArray utf8 = yaml.toUtf8();
    ryml::csubstr src(utf8.constData(), static_cast<size_t>(utf8.size()));

    try {
        auto tree = std::make_shared<ryml::Tree>();
        ryml::parse_in_arena("frontmatter", src, tree.get());

        // Must be a map at root
        if (!tree->is_map(tree->root_id())) {
            if (errorOut) *errorOut = QStringLiteral("Frontmatter root is not a YAML mapping");
            return {};
        }

        auto priv = std::make_shared<Private>();
        priv->tree = std::move(tree);
        priv->nodeId = priv->tree->root_id();
        return YamlValue(priv, priv->nodeId);
    } catch (const std::exception &e) {
        if (errorOut) *errorOut = QString::fromUtf8(e.what());
        return {};
    }
}

YamlValue YamlValue::emptyMap()
{
    auto tree = std::make_shared<ryml::Tree>();
    tree->to_map(tree->root_id());
    auto priv = std::make_shared<Private>();
    priv->tree = std::move(tree);
    priv->nodeId = priv->tree->root_id();
    return YamlValue(priv, priv->nodeId);
}

} // namespace Markoff
