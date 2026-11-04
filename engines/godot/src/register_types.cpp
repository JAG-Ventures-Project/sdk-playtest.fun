#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "playtestfun_node.h"

using namespace godot;

static void initialize_playtestfun(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(PlaytestFun);
}

static void uninitialize_playtestfun(ModuleInitializationLevel) {}

extern "C" GDExtensionBool GDE_EXPORT playtestfun_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                               GDExtensionClassLibraryPtr library,
                                                               GDExtensionInitialization* initialization) {
    GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_playtestfun);
    init.register_terminator(uninitialize_playtestfun);
    init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
