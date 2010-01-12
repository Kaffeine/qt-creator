/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "qmlexpressionundercursor.h"
#include "qmllookupcontext.h"
#include "qmlresolveexpression.h"

#include <qml/metatype/qmltypesystem.h>
#include <qml/parser/qmljsast_p.h>
#include <qml/parser/qmljsengine_p.h>

#include <QDebug>

using namespace Qml;
using namespace QmlEditor;
using namespace QmlEditor::Internal;
using namespace QmlJS;
using namespace QmlJS::AST;

QmlLookupContext::QmlLookupContext(const QStack<QmlSymbol *> &scopes,
                                   const QmlDocument::Ptr &doc,
                                   const Snapshot &snapshot,
                                   QmlTypeSystem *typeSystem):
        _scopes(scopes),
        _doc(doc),
        _snapshot(snapshot),
        m_typeSystem(typeSystem)
{
    Q_ASSERT(typeSystem != 0);
}

static inline int findFirstQmlObjectScope(const QStack<QmlSymbol*> &scopes, int startIdx)
{
    if (startIdx < 0 || startIdx >= scopes.size())
        return -1;

    for (int i = startIdx; i >= 0; --i) {
        QmlSymbol *current = scopes.at(i);

        if (current->isSymbolFromFile()) {
            Node *node = current->asSymbolFromFile()->node();

            if (cast<UiObjectBinding*>(node) || cast<UiObjectDefinition*>(node)) {
                return i;
            }
        }
    }

    return -1;
}

static inline QmlSymbol *resolveParent(const QStack<QmlSymbol*> &scopes)
{
    int idx = findFirstQmlObjectScope(scopes, scopes.size() - 1);
    if (idx < 1)
        return 0;

    idx = findFirstQmlObjectScope(scopes, idx - 1);

    if (idx < 0)
        return 0;
    else
        return scopes.at(idx);
}

QmlSymbol *QmlLookupContext::resolve(const QString &name)
{
    // look at property definitions
    if (!_scopes.isEmpty())
        if (QmlSymbol *propertySymbol = resolveProperty(name, _scopes.top(), _doc->fileName()))
            return propertySymbol;

    if (name == "parent") {
        return resolveParent(_scopes);
    }

    // look at the ids.
    const QmlDocument::IdTable ids = _doc->ids();

    if (ids.contains(name))
        return ids[name];
    else
        return resolveType(name);
}

QmlSymbol *QmlLookupContext::resolveType(const QString &name, const QString &fileName)
{
    // TODO: handle import-as.
    QmlDocument::Ptr document = _snapshot[fileName];
    if (document.isNull())
        return 0;

    UiProgram *prog = document->program();
    if (!prog)
        return 0;

    UiImportList *imports = prog->imports;
    if (!imports)
        return 0;

    for (UiImportList *iter = imports; iter; iter = iter->next) {
        UiImport *import = iter->import;
        if (!import)
            continue;

        // TODO: handle Qt imports

        if (!(import->fileName))
            continue;

        const QString path = import->fileName->asString();

        const QMap<QString, QmlDocument::Ptr> importedTypes = _snapshot.componentsDefinedByImportedDocuments(document, path);
        if (importedTypes.contains(name)) {
            QmlDocument::Ptr importedDoc = importedTypes.value(name);

            return importedDoc->symbols().at(0);
        }
    }

    // TODO: handle Qt imports, hack for now:
    return resolveBuildinType(name);
}

QmlSymbol *QmlLookupContext::resolveBuildinType(const QString &name)
{
    QList<Qml::PackageInfo> packages;
    // FIXME:
    packages.append(PackageInfo("Qt", 4, 6));
    return m_typeSystem->resolve(name, packages);
}

QmlSymbol *QmlLookupContext::resolveProperty(const QString &name, QmlSymbol *scope, const QString &fileName)
{
    foreach (QmlSymbol *symbol, scope->members())
        if (symbol->isProperty() && symbol->name() == name)
            return symbol;

    UiQualifiedId *typeName = 0;

    if (scope->isSymbolFromFile()) {
        Node *ast = scope->asSymbolFromFile()->node();

        if (UiObjectBinding *binding = cast<UiObjectBinding*>(ast)) {
            typeName = binding->qualifiedTypeNameId;
        } else if (UiObjectDefinition *definition = cast<UiObjectDefinition*>(ast)) {
            typeName = definition->qualifiedTypeNameId;
        } // TODO: extend this to handle (JavaScript) block scopes.
    }

    if (typeName == 0)
        return 0;

    QmlSymbol *typeSymbol = resolveType(toString(typeName), fileName);
    if (!typeSymbol)
        return 0;

    if (typeSymbol->isSymbolFromFile()) {
        return resolveProperty(name, typeSymbol->asSymbolFromFile(), typeSymbol->asSymbolFromFile()->fileName());
    } // TODO: internal types

    return 0;
}

QString QmlLookupContext::toString(UiQualifiedId *id)
{
    QString str;

    for (UiQualifiedId *iter = id; iter; iter = iter->next) {
        if (!(iter->name))
            continue;

        str.append(iter->name->asString());

        if (iter->next)
            str.append('.');
    }

    return str;
}

QList<QmlSymbol*> QmlLookupContext::visibleSymbolsInScope()
{
    QList<QmlSymbol*> result;

    if (!_scopes.isEmpty()) {
        QmlSymbol *scope = _scopes.top();

        // add members defined in this symbol:
        result.append(scope->members());

        // add the members of the type of this scope (= object):
        result.append(expandType(scope));
    }

    return result;
}

QList<QmlSymbol*> QmlLookupContext::visibleTypes()
{
    QList<QmlSymbol*> result;

    UiProgram *program = _doc->program();
    if (!program)
        return result;

    for (UiImportList *iter = program->imports; iter; iter = iter->next) {
        UiImport *import = iter->import;
        if (!import)
            continue;

        if (!(import->fileName))
            continue;
        const QString path = import->fileName->asString();

        // TODO: handle "import as".

        const QMap<QString, QmlDocument::Ptr> types = _snapshot.componentsDefinedByImportedDocuments(_doc, path);
        foreach (const QmlDocument::Ptr typeDoc, types) {
            result.append(typeDoc->symbols().at(0));
        }
    }

    result.append(m_typeSystem->availableTypes("Qt", 4, 6));

    return result;
}

QList<QmlSymbol*> QmlLookupContext::expandType(Qml::QmlSymbol *symbol)
{
    if (symbol == 0) {
        return QList<QmlSymbol*>();
    } else if (QmlBuildInSymbol *buildInSymbol = symbol->asBuildInSymbol()) {
        return buildInSymbol->members(true);
    } else if (QmlSymbolFromFile *symbolFromFile = symbol->asSymbolFromFile()){
        QList<QmlSymbol*> result;

        if (QmlSymbol *superTypeSymbol = resolveType(symbolFromFile->name(), symbolFromFile->fileName())) {
            result.append(superTypeSymbol->members());
            result.append(expandType(superTypeSymbol));
        }

        return result;
    } else {
        return QList<QmlSymbol*>();
    }
}
