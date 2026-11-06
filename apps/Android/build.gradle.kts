plugins {
    id("com.android.application") version "9.5.0" apply false
}

// 构建输出都放仓库的 out/android（与桌面版的 out/debug、out/release 并列），不留在源码目录里
layout.buildDirectory.set(rootDir.resolve("../../out/android/root"))
