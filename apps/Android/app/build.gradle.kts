plugins {
    id("com.android.application")
}

// 版本号的唯一来源是根 CMakeLists.txt 的 MINICAD_VERSION（三段或四段）；versionCode = 主 × 10000 + 次 × 100 + 修订
val repoRoot = rootDir.resolve("../..").canonicalFile
val minicadVersion: String = Regex("""set\(MINICAD_VERSION\s+([0-9.]+)\)""")
    .find(repoRoot.resolve("CMakeLists.txt").readText())?.groupValues?.get(1) ?: "0.0.0"
val minicadVersionCode: Int = minicadVersion.split(".").map { it.toInt() }.let { p ->
    p.getOrElse(0) { 0 } * 10000 + p.getOrElse(1) { 0 } * 100 + p.getOrElse(2) { 0 }
}

// 输出目录：APK 在 out/android/app/outputs/apk/<debug|release>/；CMake 的中间文件在并列的 out/android/cxx（放进构建目录里的话 clean 会删掉，又要全量编译）
layout.buildDirectory.set(repoRoot.resolve("out/android/app"))

// 资源清单 minicad-assets.lst（每行"相对路径<Tab>字节数"）：原生代码按它把 assets 解到私有目录，清单变了就重新解
val assetListDir = layout.buildDirectory.dir("generated/minicadAssetList")
val minicadAssetList = tasks.register("minicadAssetList") {
    val src = repoRoot.resolve("assets")
    inputs.dir(src)
    outputs.dir(assetListDir)
    doLast {
        val lines = src.walkTopDown().filter { it.isFile }.sortedBy { it.relativeTo(src).invariantSeparatorsPath }
            .joinToString("\n") { "${it.relativeTo(src).invariantSeparatorsPath}\t${it.length()}" }
        assetListDir.get().asFile.resolve("minicad-assets.lst").apply { parentFile.mkdirs(); writeText(lines + "\n") }
    }
}
tasks.named("preBuild") { dependsOn(minicadAssetList) }

// 发布版的签名：CI 从 secrets 解出密钥库并设这几个环境变量；没设时用本机的调试密钥（各台机器不同，装过别处签的包要先卸载）
val releaseKeystore: String? = System.getenv("MINICAD_KEYSTORE")?.takeIf { it.isNotBlank() }

android {
    namespace = "io.github.minicad"
    compileSdk = 36
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "io.github.minicad"
        minSdk = 29
        targetSdk = 36
        versionCode = minicadVersionCode
        versionName = minicadVersion

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DMINICAD_BUILD_TESTS=OFF",
                )
                targets += "MiniCADAndroid"
            }
        }
        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    signingConfigs {
        if (releaseKeystore != null) {
            create("release") {
                storeFile = file(releaseKeystore)
                storePassword = System.getenv("MINICAD_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("MINICAD_KEY_ALIAS")
                keyPassword = System.getenv("MINICAD_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        debug {
            ndk {
                abiFilters += "x86_64"      // 模拟器
            }
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName(if (releaseKeystore != null) "release" else "debug")
        }
    }

    externalNativeBuild {
        cmake {
            path = repoRoot.resolve("CMakeLists.txt")
            version = "3.31.6"
            buildStagingDirectory = repoRoot.resolve("out/android/cxx")
        }
    }

    // 字体、图标、界面描述文件等直接取仓库的 assets/，启动时解到应用私有目录（AndroidApp::GetResourceDir）
    sourceSets {
        getByName("main") {
            assets.directories += repoRoot.resolve("assets").path
            assets.directories += assetListDir.get().asFile.path
        }
    }

    androidResources {
        noCompress += listOf("ttf", "otf", "shx", "json")
    }
}
