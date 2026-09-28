// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

namespace QindaPortage::App {

struct TreemapItem
{
    QString id;
    QString label;
    double value = 0;
    int count = 0;
    double x = 0, y = 0, w = 0, h = 0;
};

// Squarified treemap (Bruls, Huizing, van Wijk 2000): lays `items` out in a
// width x height rectangle with areas proportional to value and aspect
// ratios close to 1. Items with value <= 0 are dropped.
QVector<TreemapItem> squarify(QVector<TreemapItem> items, double width, double height);

} // namespace QindaPortage::App
