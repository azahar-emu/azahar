// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Top-level build file where you can add configuration options common to all sub-projects/modules.
plugins {
    id("com.android.application") version "8.11.1" apply false
    id("com.android.library") version "8.11.1" apply false
    id("org.jetbrains.kotlin.android") version "2.0.20" apply false
    id("org.jetbrains.kotlin.plugin.serialization") version "2.0.20"
}

tasks.register("clean").configure {
    delete(rootProject.layout.buildDirectory)
}

buildscript {
    repositories {
        google()
    }
    dependencies {
        classpath("androidx.navigation:navigation-safe-args-gradle-plugin:2.8.0")
    }
}
