plugins {
    id("com.android.application")
}

android {
    namespace = "pebble.mycasio.notifications"
    compileSdk = 37

    defaultConfig {
        applicationId = "pebble.mycasio.notifications" // must be in companionApp of the watchface's package.json
        minSdk = 26
        targetSdk = 36
        versionCode = ((System.currentTimeMillis() / 1000 - 1767225600) / 60).toInt() // minutes since 2026-01-01: every build is newer, so the installed app can be updated
        versionName = "1.0.$versionCode"
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.getByName("debug") // the same key as the debug builds already on the phone: installs as an update, keeping the permissions, settings and the crash history
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    implementation("io.rebble.pebblekit2:client:1.3.2")
    implementation("androidx.core:core-ktx:1.17.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
    implementation("com.google.android.gms:play-services-location:21.3.0") // Activity Recognition: where the bike ride ended, when on the train (TrainService)
}
