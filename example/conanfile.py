from conan import ConanFile


class Recipe(ConanFile):
    settings = ["os", "compiler", "build_type", "arch"]
    generators = ["CMakeToolchain", "CMakeDeps"]
    requires = [
        "fmt/10.2.1",
        "perlinnoise/3.0.0",
        "range-v3/0.12.0",
        "stb/cci.20230920",
        "cli11/2.4.1",
    ]

    def layout(self):
        self.folders.generators = "conan"
