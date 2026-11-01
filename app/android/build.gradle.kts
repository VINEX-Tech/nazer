// android/build.gradle.kts  (ROOT: not android/app/build.gradle.kts)
//
// VS Code may show "Gradle build daemon disappeared unexpectedly" here.
// This is a Gradle tooling-extension warning triggered when the JVM runs
// low on memory during indexing: NOT a real build failure.
// It is resolved by the heap settings in android/gradle.properties:
//   org.gradle.jvmargs=-Xmx4g -XX:MaxMetaspaceSize=512m
// flutter run / flutter build are unaffected by this VS Code warning.

allprojects {
    repositories {
        google()
        mavenCentral()
    }
}

val newBuildDir: Directory =
    rootProject.layout.buildDirectory
        .dir("../../build")
        .get()
rootProject.layout.buildDirectory.value(newBuildDir)

subprojects {
    val newSubprojectBuildDir: Directory = newBuildDir.dir(project.name)
    project.layout.buildDirectory.value(newSubprojectBuildDir)
}
subprojects {
    project.evaluationDependsOn(":app")
}

tasks.register<Delete>("clean") {
    delete(rootProject.layout.buildDirectory)
}