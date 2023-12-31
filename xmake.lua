add_rules("mode.debug", "mode.release")
add_requires("elfio 3.11 ","libdwarf 0.9.0","magic_enum v0.9.5","nlohmann_json 3.11.3")

target("dwarf-param-dumper")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("elfio","libdwarf","magic_enum","nlohmann_json")
    set_languages("cxx20","c99")