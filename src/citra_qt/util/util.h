// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <filesystem>
#include <QFont>
#include <QString>
#include "common/common_types.h"

/// Returns a QFont object appropriate to use as a monospace font for debugging widgets, etc.
QFont GetMonospaceFont();

/// Convert a size in bytes into a readable format (KiB, MiB, etc.)
QString ReadableByteSize(qulonglong size);

// Converts a length of time in seconds into a readable format
QString ReadableDuration(qulonglong time_seconds);

/**
 * Creates a circle pixmap from a specified color
 * @param color The color the pixmap shall have
 * @return QPixmap circle pixmap
 */
QPixmap CreateCirclePixmapFromColor(const QColor& color);

/**
 * Gets the game icon from SMDH data.
 * @param smdh_data SMDH data
 * @return QPixmap game icon
 */
QPixmap GetQPixmapFromSMDH(const std::vector<u8>& smdh_data);

/**
 * Saves a windows icon to a file
 * @param path The icons path
 * @param image The image to save
 * @return bool If the operation succeeded
 */
[[nodiscard]] bool SaveIconToFile(const std::filesystem::path& icon_path, const QImage& image);

/**
 * @return The user’s applications directory
 */
[[nodiscard]] const std::string GetApplicationsDirectory();

/**
 * Imitates the deprecated `QImage::mirrored` function in a forwards-compatible manner
 * @param flip_horizontal Whether the image should be flipped horizontally
 * @param flip_vertical Whether the image should be flipped vertically
 * @return QImage The mirrored image
 */
QImage GetMirroredImage(QImage source_image, bool flip_horizontal, bool flip_vertical);
