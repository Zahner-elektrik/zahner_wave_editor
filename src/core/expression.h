// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <memory>
#include <optional>

namespace zwe {

struct ExpressionError {
    QString message;
    int position = -1;  // character offset into the expression; -1 if not applicable

    bool operator==(const ExpressionError&) const = default;
};

// Wraps a muParser expression of a single variable `t`, with the constants
// `pi` and `e` predefined (muParser's own `sin cos tan asin acos atan sinh
// cosh tanh exp ln log log2 log10 sqrt abs sign min max rint` stay
// available, registered by muParser itself). Each instance owns its own
// parser, so separate instances can be used from separate threads, but a
// single instance cannot. One Expression per segment is enough for that.
class Expression {
public:
    Expression();
    ~Expression();
    Expression(Expression&&) noexcept;
    Expression& operator=(Expression&&) noexcept;
    Expression(const Expression&)            = delete;
    Expression& operator=(const Expression&) = delete;

    // Parses `expression`. On success, evaluate() becomes usable and returns
    // std::nullopt; on failure, returns the error (message + position) and
    // evaluate() returns 0.0 until compile() succeeds. Unknown identifiers
    // (neither `t`, `pi`, `e` nor a known function) are a compile error.
    std::optional<ExpressionError> compile(const QString& expression);

    // Evaluates the last successfully compiled expression at the given `t`.
    // Never throws: 0.0 if nothing has been compiled successfully yet, or if
    // evaluation itself fails at runtime.
    double evaluate(double t) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace zwe
