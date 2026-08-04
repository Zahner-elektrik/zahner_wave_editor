// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "expression.h"

#include <muParser.h>

#include <numbers>

namespace zwe {
namespace {

mu::string_type toParserString(const QString& text) {
#if defined(_UNICODE)
    return text.toStdWString();
#else
    return text.toStdString();
#endif
}

QString fromParserString(const mu::string_type& text) {
#if defined(_UNICODE)
    return QString::fromStdWString(text);
#else
    return QString::fromStdString(text);
#endif
}

}  // namespace

struct Expression::Impl {
    mu::Parser parser;
    double tVariable = 0.0;
    bool compiledOk  = false;

    Impl() {
        parser.DefineVar(toParserString(QStringLiteral("t")), &tVariable);
        parser.DefineConst(toParserString(QStringLiteral("pi")), std::numbers::pi);
        parser.DefineConst(toParserString(QStringLiteral("e")), std::numbers::e);
    }
};

Expression::Expression() : m_impl(std::make_unique<Impl>()) {
}

Expression::~Expression()                                = default;

Expression::Expression(Expression&&) noexcept            = default;

Expression& Expression::operator=(Expression&&) noexcept = default;

std::optional<ExpressionError> Expression::compile(const QString& expression) {
    m_impl->compiledOk = false;
    m_impl->parser.SetExpr(toParserString(expression));

    try {
        // muParser compiles to bytecode lazily on the first Eval(), so force
        // one now: syntax errors and unknown identifiers then surface here
        // instead of on the first evaluate() call.
        m_impl->parser.Eval();
    } catch (const mu::ParserError& error) {
        return ExpressionError{fromParserString(error.GetMsg()), error.GetPos()};
    }

    m_impl->compiledOk = true;
    return std::nullopt;
}

double Expression::evaluate(double t) const {
    if (! m_impl->compiledOk) {
        return 0.0;
    }

    m_impl->tVariable = t;
    try {
        return m_impl->parser.Eval();
    } catch (const mu::ParserError&) {
        return 0.0;
    }
}

}  // namespace zwe
