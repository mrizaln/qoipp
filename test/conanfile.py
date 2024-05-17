from conan import ConanFile


class Recipe(ConanFile):
    settings   = ["os", "compiler", "build_type", "arch"]
    generators = ["CMakeToolchain", "CMakeDeps"]
    requires   = ["fmt/10.2.1", "boost-ext-ut/1.1.9", "range-v3/0.12.0"]

    def layout(self):
        self.folders.generators = "conan"
