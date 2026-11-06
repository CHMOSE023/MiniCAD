// MiniCAD 安卓版的 Gradle 工程（docs/Android/PLAN.md）。原生代码由 app 模块经 externalNativeBuild 调用仓库根的 CMakeLists.txt
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "MiniCAD"
include(":app")
