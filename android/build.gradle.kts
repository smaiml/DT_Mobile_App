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

subprojects {
    plugins.withType<com.android.build.gradle.api.AndroidBasePlugin> {
        val android = project.extensions.getByName("android")
        if (android is com.android.build.gradle.BaseExtension) {
            if (android.namespace == null) {
                if (project.name == "telephony") {
                    android.namespace = "com.shounakmulay.telephony"
                } else {
                    android.namespace = "com.example.${project.name.replace("-", "_")}"
                }
            }
        }
    }
}

tasks.register<Delete>("clean") {
    delete(rootProject.layout.buildDirectory)
}
