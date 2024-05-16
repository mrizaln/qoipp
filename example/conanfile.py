from conan import ConanFile


class Recipe(ConanFile):
    settings = ["os", "compiler", "build_type", "arch"]
    generators = ["CMakeToolchain", "CMakeDeps"]
    requires = [
        "fmt/10.2.1",
        "perlinnoise/3.0.0",
        "range-v3/0.12.0",
        "stb/cci.20230920",
    ]

    def layout(self):
        self.folders.generators = "conan"
