#pragma once

#include "utils/EditAction.h"
#include <QImage>

namespace yingtu {

/**
 * Apply a mosaic effect to the region defined by action.bounds.
 * The effect style and block size are taken from action.mosaicStyle
 * and action.mosaicSize.
 */
QImage applyMosaic(const QImage& source, const EditAction& action);

} // namespace yingtu
