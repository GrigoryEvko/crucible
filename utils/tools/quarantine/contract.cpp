// The contract plugin of GCC.
//
// It applies the contract rule of the tree, and nothing else: a P2900 contract
// specifier on a function declaration is an error, unless an opt-out region
// holds it.  plugin_core.h holds the rule, the region and the reasons.  Each
// build loads this plugin, except a build where CRUCIBLE_QUARANTINE is REPORT
// or ERROR: that build loads the quarantine plugin, which applies the same
// rule from the same header.
//
// THE ARGUMENTS (-fplugin-arg-crucible_contract-NAME=VALUE)
//     root=PATH      the source root (necessary)
//     build=PATH     a build directory under the root, whose files are generated
//     stamp=TEXT     ignored; a new value makes the build system compile again

#include "plugin_core.h"

int plugin_is_GPL_compatible;

namespace {

using namespace crucible_plugin;

void report() {
    if (core.was_reported) {
        return;
    }
    core.was_reported = true;
    report_open_regions();
    for (const Finding& finding : core.findings) {
        if (!is_opted_out(finding)) {
            report_contract(finding);
        }
    }
}

void on_finish_unit(void*, void*) {
    walk_contract_namespace(global_namespace);
    report();
}

// A unit that stopped on an error, or that -fsyntax-only compiled, does not
// reach PLUGIN_FINISH_UNIT.  Under -fsyntax-only the trees are complete, so
// the walk of declarations runs here.
void on_finish(void*, void*) {
    if (!core.was_reported && flag_syntax_only && !seen_error()) {
        walk_contract_namespace(global_namespace);
    }
    report();
}

void on_collection(void*, void*) { core.contract_walked.clear(); }

}  // namespace

int plugin_init(plugin_name_args* plugin_info, plugin_gcc_version* version) {
    if (!plugin_default_version_check(version, &gcc_version)) {
        error("quarantine: the plugin was built for GCC %s, and this compiler is a different version",
              gcc_version.basever);
        return 1;
    }
    // A preprocessing run builds no tree.
    if (flag_preprocess_only) {
        return 0;
    }
    core.plugin_name = plugin_info->base_name;
    std::string root_argument;
    for (int index = 0; index < plugin_info->argc; ++index) {
        std::string key = plugin_info->argv[index].key;
        std::string value = plugin_info->argv[index].value != nullptr ? plugin_info->argv[index].value : "";
        if (key == "root") {
            root_argument = value;
        } else if (key == "build") {
            if (!set_build(value)) {
                return 1;
            }
        } else if (key != "stamp") {
            error("quarantine: unknown argument %qs; the arguments are root, build and stamp", key.c_str());
            return 1;
        }
    }
    if (!set_root(root_argument)) {
        return 1;
    }
    register_contract_rule(plugin_info->base_name);
    register_callback(plugin_info->base_name, PLUGIN_FINISH_UNIT, on_finish_unit, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_FINISH, on_finish, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_GGC_START, on_collection, nullptr);
    return 0;
}
