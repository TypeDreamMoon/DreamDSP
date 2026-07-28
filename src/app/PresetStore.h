#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "core/PeacePreset.h"

namespace dreamdsp {

// Lists presets from two places:
//   * DreamDSP's own directory (%APPDATA%/DreamDSP/presets) -- writable;
//   * Equalizer APO's config directory -- the .peace files an existing Peace
//     install left behind, exposed read-only so they can be imported.
//
// Both use the same on-disk format, so a preset saved here also opens in Peace.
class PresetStore : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        DescriptionRole,
        PathRole,
        ImportedRole,   // true == lives in APO's config dir, not ours
        BandCountRole,
    };
    Q_ENUM(Role)

    explicit PresetStore(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Where to also look for .peace files (APO's config dir). Empty disables it.
    void setImportDirectory(const QString &dir);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool remove(int row, QString *error = nullptr);

    // HusSelect wants a plain [{label, value}] array rather than an item
    // model, and reading roles out of a QAbstractListModel from QML is
    // awkward -- so hand it one directly.
    Q_INVOKABLE QVariantList entries() const;

    bool load(int row, Preset *out, QString *error = nullptr) const;
    bool save(const QString &name, const Preset &preset, QString *error = nullptr);

    // Directory DreamDSP writes its own presets and session state into.
    static QString userDirectory();

signals:
    void countChanged();

private:
    struct Entry {
        QString name;
        QString description;
        QString path;
        bool imported = false;
        int bandCount = 0;
    };

    void scan(const QString &dir, bool imported);

    QVector<Entry> m_entries;
    QString m_importDir;
};

} // namespace dreamdsp
