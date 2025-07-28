#include "app.hpp"
#include "runtime.hpp"

namespace LuaApps {

    App::App(const String &name, const String &fromPath, const std::vector<String> &args)
        : path(name), origin(fromPath), arguments(args) {}

    int App::run() {
        result = Runtime::runApp(path.c_str(), arguments);
        return result;
    }

    int App::exitCode() const {
        return result;
    }

}
