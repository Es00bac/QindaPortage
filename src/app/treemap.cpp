// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/treemap.h"

#include <algorithm>
#include <limits>

namespace QindaPortage::App {

namespace {

double worst(const QVector<double> &row, double side)
{
    double sum = 0, mx = 0, mn = std::numeric_limits<double>::max();
    for (double a : row) { sum += a; mx = std::max(mx, a); mn = std::min(mn, a); }
    if (sum <= 0 || side <= 0) return std::numeric_limits<double>::max();
    const double s2 = sum * sum, w2 = side * side;
    return std::max(w2 * mx / s2, s2 / (w2 * mn));
}

} // namespace

QVector<TreemapItem> squarify(QVector<TreemapItem> items, double width, double height)
{
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto &i) { return i.value <= 0; }), items.end());
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.value > b.value; });
    double total = 0;
    for (const auto &i : items) total += i.value;
    if (total <= 0 || width <= 0 || height <= 0) return {};
    const double scale = width * height / total;

    double x = 0, y = 0, w = width, h = height;
    int start = 0;
    while (start < items.size()) {
        const double side = std::min(w, h);
        QVector<double> row;
        int end = start;
        while (end < items.size()) {
            QVector<double> candidate = row;
            candidate.append(items.at(end).value * scale);
            if (!row.isEmpty() && worst(candidate, side) > worst(row, side)) break;
            row = candidate;
            ++end;
        }
        double rowArea = 0;
        for (double a : row) rowArea += a;
        const bool horizontal = w >= h; // lay the row along the short side
        const double thickness = rowArea / side;
        double offset = 0;
        for (int i = start; i < end; ++i) {
            const double length = row.at(i - start) / thickness;
            TreemapItem &it = items[i];
            if (horizontal) {
                it.x = x; it.y = y + offset; it.w = thickness; it.h = length;
            } else {
                it.x = x + offset; it.y = y; it.w = length; it.h = thickness;
            }
            offset += length;
        }
        if (horizontal) { x += thickness; w -= thickness; }
        else { y += thickness; h -= thickness; }
        start = end;
    }
    return items;
}

} // namespace QindaPortage::App
