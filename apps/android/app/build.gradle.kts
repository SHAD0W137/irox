plugins {
    id("com.android.application")
}

android {
    namespace = "com.irox.engine"
    compileSdk = 34
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "com.irox.engine"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters.addAll(listOf("arm64-v8a"))
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DCMAKE_CXX_STANDARD=23"
                )
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    // Включаем Prefab для автоматической интеграции AAR с CMake
    buildFeatures {
        prefab = true
    }

    externalNativeBuild {
        cmake {
            path = file("../../../CMakeLists.txt")
            version = "3.22.1+"
        }
    }

    sourceSets {
        getByName("main") {
            assets.srcDirs("../../../assets")
        }
    }
    androidResources {
        noCompress += listOf("gltf", "bin", "png", "jpeg", "jpg", "ktx", "ktx2", "spv", "slang")
    }
}

dependencies {
    // Подключаем скачанный локальный SDL3.aar
    implementation(files("libs/SDL3.aar"))
}