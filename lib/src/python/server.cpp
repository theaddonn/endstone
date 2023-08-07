//
// Created by Vincent on 31/07/2023.
//

#include "python/server.h"

Server::Server()
{
    try
    {
        py::gil_scoped_acquire lock{};
        auto module = py::module_::import("endstone.plugin");
        auto cls = module.attr("PluginManager");
        pluginManager_ = cls(this);

        //        pluginManager_.attr("load_plugins")("abc");
        //        pluginManager_.attr("enable_plugins")();
    }
    catch (py::error_already_set &e)
    {
        printf("Initializing PluginManager failed with Python exceptions: %s\n", e.what());
        throw e;
    }
    catch (const std::exception &e)
    {
        printf("Initializing PluginManager failed with Python C API exceptions: %s\n", e.what());
        throw e;
    }
}
void Server::loadPlugins()
{
    py::gil_scoped_acquire lock{};
    printf("Loading plugins...\n");
    pluginManager_.attr("load_plugins")((std::filesystem::current_path() / "plugins").string());
}

void Server::enablePlugins()
{
    py::gil_scoped_acquire lock{};
    printf("Enabling plugins...\n");
    pluginManager_.attr("enable_plugins")();
}

PYBIND11_MODULE(_server, m)
{
    py::class_<Server>(m, "Server");
}
