#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "core/PeacePreset.h"

namespace dreamdsp {

class EqBandModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        FrequencyRole = Qt::UserRole + 1,
        GainRole,
        QRole,
        TypeRole,        // int, indexes FilterType
        TypeTokenRole,   // "PK", "LSCQ", ...
        EnabledRole,
        LabelRole,       // "125" / "1.6k" -- pre-formatted slider caption
        HasGainRole,     // false for LP/HP/BP/NO/AP: the gain slider is inert
    };
    Q_ENUM(Role)

    explicit EqBandModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setGain(int row, double gainDb);
    Q_INVOKABLE void setFrequency(int row, double hz);
    Q_INVOKABLE void setQ(int row, double q);
    Q_INVOKABLE void setType(int row, int typeIndex);
    Q_INVOKABLE void setBandEnabled(int row, bool on);
    Q_INVOKABLE double gain(int row) const;

    // Zeroes every gain but keeps the band layout -- what "reset" means to a
    // user staring at an equalizer.
    Q_INVOKABLE void zeroGains();
    // Back to the stock 10-band ISO octave layout.
    Q_INVOKABLE void restoreDefaults();

    // [{ label: "PK — 峰值", value: 0 }, ...] for a HusSelect.
    Q_INVOKABLE QVariantList filterTypeOptions() const;

    // New bands land halfway (logarithmically) between their neighbours, which
    // is almost always where you want one.
    Q_INVOKABLE void insertBandAfter(int row);
    Q_INVOKABLE void removeBand(int row);

    const QVector<PresetBand> &bands() const { return m_bands; }
    void setBands(const QVector<PresetBand> &bands);
    void setBandCount(int count);

signals:
    void countChanged();
    void bandsChanged();

private:
    void emitChanged(int row, const QList<int> &roles);

    QVector<PresetBand> m_bands;
};

} // namespace dreamdsp
