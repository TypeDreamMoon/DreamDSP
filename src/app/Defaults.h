#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariant>

namespace dreamdsp {

// The value a control would have shown if nothing had ever been changed.
//
// Read off a default-constructed instance of the model itself, not from a table
// of numbers kept beside it. A table would be a second statement of every
// default -- one in the constructor and one for the reset button -- and the two
// would disagree the first time anyone edited only one of them. This cannot
// disagree, because it *is* the constructor.
//
// The prototype is constructed once, on first use, and never touched again. The
// four models this is used with have constructors that do nothing but assign
// their own fields, which is what makes that safe; anything that reached out to
// a device or a file in its constructor would not belong here.
template <class T>
QVariant defaultPropertyOf(const QString &name)
{
    static const T prototype;
    const QByteArray key = name.toUtf8();
    if (prototype.metaObject()->indexOfProperty(key.constData()) < 0)
        return {};
    return prototype.property(key.constData());
}

} // namespace dreamdsp
