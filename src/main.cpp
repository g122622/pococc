#include "pococc/application/Application.hpp"
#include "pococc/core/Logger.hpp"
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i)
        arguments.emplace_back(argv[i]);
    auto result = pococc::application::Application::execute(arguments);
    if (!result.success()) {
        pococc::Logger::error(result.error().message);
        return 127;
    }
    return result.value();
}
