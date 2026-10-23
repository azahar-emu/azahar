// Copyright 2020 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

namespace Core {

template <class T>
T& Global();

// Declare explicit specialisation to prevent automatic instantiation
class System;
template <>
System& Global();

class Timing;
template <>
Timing& Global();

} // namespace Core
