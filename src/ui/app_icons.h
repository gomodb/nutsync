#pragma once

#include <QIcon>
#include <QPalette>

namespace NutSync::AppIcons {

bool isDarkTheme();
bool isDarkPalette(const QPalette &palette);

QIcon colored();
QIcon monochrome(bool dark);
QIcon syncing(bool dark);

} // namespace NutSync::AppIcons
